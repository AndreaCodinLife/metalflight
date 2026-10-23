#include "split_push_detour.h"
#include <limits.h>
#include <string.h>

static const BYTE game_original[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x40};
static const BYTE mimalloc_original[]={0x40,0x55,0x56,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x50};
typedef LONG (WINAPI *next_thread_fn)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,HANDLE*);
typedef LONG (WINAPI *loader_lock_fn)(ULONG,ULONG*,ULONG_PTR*);
typedef LONG (WINAPI *loader_unlock_fn)(ULONG,ULONG_PTR);
static int relative(BYTE *at, BYTE *to)
{
    intptr_t delta=to-(at+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return 0;
    at[0]=0xe9;int32_t value=(int32_t)delta;memcpy(at+1,&value,4);return 1;
}
static void *allocate_near(BYTE *entry)
{
    uintptr_t base=(uintptr_t)entry & ~(uintptr_t)65535;
    for(unsigned i=1;i<32760;i++)
    {
        intptr_t distance=(intptr_t)((i+1)/2)*65536*(i&1 ? 1 : -1);
        uintptr_t at=base+distance;
        if(at<65536)continue;
        MEMORY_BASIC_INFORMATION m;
        if(VirtualQuery((void*)at,&m,sizeof(m)) && m.State==MEM_FREE &&
           (uintptr_t)m.BaseAddress<=at && (uintptr_t)m.BaseAddress+m.RegionSize>=at+4096)
        {
            void *p=VirtualAlloc((void*)at,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(p)return p;
        }
    }
    return NULL;
}
int split_prepare(split_detour *d,void *entry)
{return split_prepare_kind(d,entry,0);}
int split_prepare_kind(split_detour *d,void *entry,int mimalloc)
{
    split_profile profile={0};profile.count=5;
    profile.match_length=mimalloc ? sizeof(mimalloc_original):sizeof(game_original);
    memcpy(profile.original,mimalloc?mimalloc_original:game_original,profile.match_length);
    const BYTE game_widths[]={2,1,1,1,2},game_regs[]={3,5,6,7,14};
    const BYTE mi_widths[]={2,1,2,2,2},mi_regs[]={5,6,12,14,15};
    memcpy(profile.widths,mimalloc?mi_widths:game_widths,5);
    memcpy(profile.registers,mimalloc?mi_regs:game_regs,5);profile.span=mimalloc?9:7;
    return split_prepare_profile(d,entry,&profile);
}
int split_prepare_profile(split_detour *d,void *entry,const split_profile *profile)
{
    memset(d,0,sizeof(*d));d->entry=entry;
    unsigned suffix=profile->suffix_length;
    if(profile->count<2 || profile->count>8 || profile->span>16 ||
       profile->match_length<8 || profile->match_length>16 || profile->span+suffix>profile->match_length)return 9;
    /* A suffix only completes a run too short for the jump; it carries no prefix. */
    if(suffix?(suffix!=4 || profile->span>=5 || profile->span+suffix<5 || profile->count>7 || profile->prefix_length):
       profile->span<5)return 9;
    unsigned span=profile->prefix_length,seen=0;
    if(span && (span!=5 || profile->count>7 || profile->prefix_register!=3 ||
       !profile->prefix_stack_offset || profile->prefix_stack_offset>32 || (profile->prefix_stack_offset&7) ||
       memcmp(profile->original,"\x48\x89\x5c\x24",4) || profile->original[4]!=profile->prefix_stack_offset))return 9;
    for(unsigned i=0;i<profile->count;i++)
    {
        unsigned width=profile->widths[i],reg=profile->registers[i];
        if((width!=1 && width!=2) || reg>15 || !(0xf0e8u&(1u<<reg)) || (seen&(1u<<reg)) || span+width>16)return 9;
        BYTE rex=width==2?profile->original[span]:0,opcode=profile->original[span+width-1];
        if((width==2 && (rex&0xf0)!=0x40) || opcode<0x50 || opcode>0x57 ||
           (unsigned)(opcode-0x50+((rex&1)?8:0))!=reg)return 9;
        seen|=1u<<reg;span+=width;
    }
    if(span!=profile->span || (profile->prefix_length && (seen&(1u<<profile->prefix_register))))return 9;
    if(suffix && (memcmp(profile->original+span,"\x48\x83\xec",3) || !profile->original[span+3] ||
       (profile->original[span+3]&7) || profile->original[span+3]>0x78))return 9;
    d->match_length=profile->match_length;d->span=span;d->count=profile->count;
    memcpy(d->original,profile->original,16);memcpy(d->widths,profile->widths,8);memcpy(d->registers,profile->registers,8);
    d->prefix_length=profile->prefix_length;d->prefix_register=profile->prefix_register;d->prefix_stack_offset=profile->prefix_stack_offset;
    d->suffix_length=suffix;
    unsigned prefix=!!d->prefix_length;
    d->stub_length=suffix?16*(d->count+prefix)+suffix+5:16*(d->count+prefix-1)+d->widths[d->count-1]+5;
    /* A five-byte jump must fit wholly in one aligned atomic replacement. */
    d->patch_size=((uintptr_t)entry&7)?16:8;
    d->patch_base=(BYTE*)((uintptr_t)entry&~(uintptr_t)(d->patch_size-1));
    d->patch_offset=(DWORD)((BYTE*)entry-d->patch_base);
    if(d->patch_offset+5>d->patch_size || memcmp(entry,d->original,d->match_length))return 10;
    memcpy(d->patch_original,d->patch_base,d->patch_size);
    d->stub=allocate_near(entry);if(!d->stub)return 11;
    BYTE *p=d->stub;memset(p,0xcc,4096);
    unsigned source=d->prefix_length;
    if(prefix)
    {
        memcpy(p,d->original,d->prefix_length);
        if(!relative(p+d->prefix_length,p+16))return 12;
    }
    for(unsigned i=0;i<d->count;i++)
    {
        unsigned at=(i+prefix)*16;
        memcpy(p+at,d->original+source,d->widths[i]);source+=d->widths[i];
        if(!relative(p+at+d->widths[i],i==d->count-1 && !suffix?d->entry+d->span:p+at+16))return 12;
    }
    if(suffix)
    {
        unsigned at=(d->count+prefix)*16;memcpy(p+at,d->original+d->span,suffix);
        if(!relative(p+at+suffix,d->entry+d->span+suffix))return 12;
    }
    d->table=(RUNTIME_FUNCTION*)(p+128);
    d->table->BeginAddress=0;d->table->EndAddress=d->stub_length;d->table->UnwindData=256;
    /* Entire stub is prologue, including final jump. No epilogue recognition. */
    unsigned codes=d->count+!!suffix,k=!!suffix;
    BYTE unwind[20]={1,(BYTE)d->stub_length,(BYTE)codes,0};
    /* UWOP_ALLOC_SMALL for the relocated SUB, latest offset first. */
    if(suffix){unwind[4]=(BYTE)((d->count+prefix)*16+suffix);unwind[5]=(BYTE)(2|((d->original[d->span+3]/8-1)<<4));}
    for(unsigned i=0;i<d->count;i++)
    {unsigned reg=d->count-1-i;unwind[4+2*(i+k)]=(BYTE)((reg+prefix)*16+d->widths[reg]);unwind[5+2*(i+k)]=d->registers[reg]<<4;}
    /* The prefix only stores RBX; it never changes RBX or RSP. It therefore
     * needs no restore operation inside this stub. The original body's unwind
     * record describes the home save once its final stack frame exists. */
    memcpy(p+256,unwind,((4+2*codes+3)/4)*4);
    DWORD previous;
    if(!VirtualProtect(p,4096,PAGE_EXECUTE_READ,&previous))return 13;
    if(!RtlAddFunctionTable(d->table,1,(DWORD64)p))return 14;
    return 0;
}
int split_unwind_check(split_detour *d)
{
    const DWORD64 values[]={0x1337,0x2448,0x3559,0x466a,0x577b,0x688c,0x799d,0x8aae};
    unsigned prefix=!!d->prefix_length;const DWORD64 prefix_value=0xfedcba987654321ULL;
    for(unsigned i=0;i<2*(d->count+prefix);i++)
    {
        DWORD64 stack[32]={0};DWORD64 *top=stack+16;top[0]=0x123456789abULL;
        unsigned op=i/2,done=op+(i&1),saved=prefix && done;
        unsigned n=done-saved,width=prefix && !op?d->prefix_length:d->widths[op-prefix];
        unsigned offset=op*16+((i&1)?width:0);
        if(saved)top[d->prefix_stack_offset/8]=prefix_value;
        for(unsigned j=0;j<n;j++)top[-(int)j-1]=values[j];
        CONTEXT c;memset(&c,0,sizeof(c));c.Rip=(DWORD64)d->stub+offset;c.Rsp=(DWORD64)(top-n);
        DWORD64 *registers=&c.Rax;
        if(prefix)registers[d->prefix_register]=prefix_value;
        for(unsigned j=0;j<d->count;j++)registers[d->registers[j]]=n>j?0:values[j];
        PVOID handler=NULL;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,(DWORD64)d->stub,c.Rip,d->table,&c,&handler,&frame,NULL);
        if(c.Rip!=top[0] || c.Rsp!=(DWORD64)(top+1))return 20+(int)i;
        for(unsigned j=0;j<d->count;j++)if(registers[d->registers[j]]!=values[j])return 20+(int)i;
        if(prefix && registers[d->prefix_register]!=prefix_value)return 20+(int)i;
    }
    /* Suffix block: before and after the relocated SUB, all pushes complete. */
    for(unsigned after=0;d->suffix_length && after<2;after++)
    {
        unsigned alloc=d->original[d->span+3];
        DWORD64 stack[64]={0};DWORD64 *top=stack+48;top[0]=0x123456789abULL;
        for(unsigned j=0;j<d->count;j++)top[-(int)j-1]=values[j];
        CONTEXT c;memset(&c,0,sizeof(c));
        c.Rip=(DWORD64)d->stub+(d->count+prefix)*16+(after?d->suffix_length:0);
        c.Rsp=(DWORD64)(top-d->count)-(after?alloc:0);
        DWORD64 *registers=&c.Rax;
        for(unsigned j=0;j<d->count;j++)registers[d->registers[j]]=0;
        PVOID handler=NULL;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,(DWORD64)d->stub,c.Rip,d->table,&c,&handler,&frame,NULL);
        if(c.Rip!=top[0] || c.Rsp!=(DWORD64)(top+1))return 40+(int)after;
        for(unsigned j=0;j<d->count;j++)if(registers[d->registers[j]]!=values[j])return 40+(int)after;
    }
    return 0;
}
int split_install(split_detour *d,int restore)
{
    HMODULE nt=GetModuleHandleA("ntdll.dll");
    next_thread_fn next=(next_thread_fn)(void*)GetProcAddress(nt,"NtGetNextThread");
    loader_lock_fn lock=(loader_lock_fn)(void*)GetProcAddress(nt,"LdrLockLoaderLock");
    loader_unlock_fn unlock=(loader_unlock_fn)(void*)GetProcAddress(nt,"LdrUnlockLoaderLock");
    if(!next || !lock || !unlock || !d->stub)return 30;
    BYTE patched[16];memcpy(patched,d->patch_original,d->patch_size);
    intptr_t displacement=d->stub-(d->entry+5);
    if(displacement<INT32_MIN || displacement>INT32_MAX)return 31;
    patched[d->patch_offset]=0xe9;int32_t v=(int32_t)displacement;memcpy(patched+d->patch_offset+1,&v,4);
    LONG64 expected[2] __attribute__((aligned(16))),replacement[2];
    memcpy(expected,restore?patched:d->patch_original,d->patch_size);
    memcpy(replacement,restore?d->patch_original:patched,d->patch_size);
    HANDLE *threads=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,1024*sizeof(HANDLE));
    CONTEXT *context=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(CONTEXT));
    if(!threads || !context)
    {if(threads)HeapFree(GetProcessHeap(),0,threads);if(context)HeapFree(GetProcessHeap(),0,context);return 32;}
    DWORD old;
    if(!VirtualProtect(d->patch_base,d->patch_size,PAGE_EXECUTE_READWRITE,&old))
    {HeapFree(GetProcessHeap(),0,context);HeapFree(GetProcessHeap(),0,threads);return 33;}
    if(!restore)d->original_protect=old;
    int result=34;
    for(unsigned attempt=0;attempt<100;attempt++)
    {
        ULONG disposition=0;ULONG_PTR cookie=0;
        if(lock(2,&disposition,&cookie)<0){result=35;break;}
        if(disposition!=1){Sleep(1);continue;}
        unsigned count=0,suspended=0;HANDLE prior=NULL;int busy=0;LONG status;
        for(;;)
        {
            HANDLE current=NULL;
            status=next(GetCurrentProcess(),prior,THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,0,0,&current);
            /* keep enumeration handles open until no peer is paused */
            if(status==(LONG)0x8000001a)break;
            if(status<0 || !current){result=36;busy=1;break;}
            if(count==1023){CloseHandle(current);result=37;busy=1;break;}
            threads[count++]=current;prior=current;
        }
        if(!busy)for(unsigned i=0;i<count;i++)
        {
            if(GetThreadId(threads[i])==GetCurrentThreadId())continue;
            if(SuspendThread(threads[i])==(DWORD)-1){result=38;busy=1;break;}
            /* mark successfully suspended handles in place; context query waits for stop */
            suspended=i+1;context->ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(threads[i],context)){result=39;busy=1;break;}
            if((context->Rip>=(DWORD64)d->entry && context->Rip<(DWORD64)d->entry+d->match_length) ||
               (context->Rip>=(DWORD64)d->stub && context->Rip<(DWORD64)d->stub+d->stub_length))
            {result=40;busy=1;break;}
        }
        if(!busy)
        {
            int changed=0;
            for(DWORD j=d->patch_size-d->patch_offset;j<d->match_length;j++)
                if(d->entry[j]!=d->original[j])changed=1;
            int exchanged=0;
            if(!changed)
            {
                if(d->patch_size==16)
                {
                    unsigned __int128 old128,new128;
                    memcpy(&old128,expected,16);memcpy(&new128,replacement,16);
                    exchanged=__sync_bool_compare_and_swap((volatile unsigned __int128*)d->patch_base,old128,new128);
                }
                else exchanged=InterlockedCompareExchange64((volatile LONG64*)d->patch_base,replacement[0],expected[0])==expected[0];
            }
            if(changed || !exchanged)result=41;
            else
            {
                DWORD unused;d->state=restore?0:1;
                FlushInstructionCache(GetCurrentProcess(),d->patch_base,d->patch_size);
                if(!VirtualProtect(d->patch_base,d->patch_size,d->original_protect,&unused)){d->state=-1;result=42;}
                else result=0;
            }
        }
        for(unsigned i=0;i<suspended;i++)
            if(GetThreadId(threads[i])!=GetCurrentThreadId() && ResumeThread(threads[i])==(DWORD)-1)result=43;
        unlock(0,cookie);
        d->peers=count?count-1:0;
        for(unsigned i=0;i<count;i++){CloseHandle(threads[i]);threads[i]=NULL;}
        if(result!=40)break;
        d->retries++;Sleep(1);
    }
    if(result && d->state>=0){DWORD unused;VirtualProtect(d->patch_base,d->patch_size,d->original_protect,&unused);}
    HeapFree(GetProcessHeap(),0,context);HeapFree(GetProcessHeap(),0,threads);
    return result;
}
void split_discard(split_detour *d)
{
    if(d->stub && !d->state){RtlDeleteFunctionTable(d->table);VirtualFree(d->stub,0,MEM_RELEASE);d->stub=NULL;}
}
