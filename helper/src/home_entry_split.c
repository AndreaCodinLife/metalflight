#include "home_entry_split.h"
#include <limits.h>
#include <string.h>

_Static_assert(sizeof(((split_detour *)0)->original)>=23,
               "Use the private extended-preimage header, not the frozen 16-byte core ABI");
static const BYTE shape[23]={
    0x48,0x89,0x54,0x24,0x10, /* mov [rsp+10h],rdx */
    0x4c,0x89,0x44,0x24,0x18, /* mov [rsp+18h],r8  */
    0x4c,0x89,0x4c,0x24,0x20, /* mov [rsp+20h],r9  */
    0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x38
};
static int jump(BYTE *from,BYTE *to)
{
    intptr_t delta=to-(from+5);if(delta<INT32_MIN||delta>INT32_MAX)return 0;
    int32_t rel=(int32_t)delta;from[0]=0xe9;memcpy(from+1,&rel,4);return 1;
}
static void *nearby(BYTE *entry)
{
    uintptr_t base=(uintptr_t)entry&~(uintptr_t)65535;
    for(unsigned i=1;i<32760;i++){
        intptr_t delta=(intptr_t)((i+1)/2)*65536*(i&1?1:-1);
        uintptr_t at=base+delta;MEMORY_BASIC_INFORMATION m;
        if(at<65536)continue;
        if(VirtualQuery((void *)at,&m,sizeof(m))&&m.State==MEM_FREE&&
           (uintptr_t)m.BaseAddress<=at&&(uintptr_t)m.BaseAddress+m.RegionSize>=at+4096){
            void *p=VirtualAlloc((void *)at,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(p)return p;
        }
    }
    return NULL;
}
int home_entry_prepare(split_detour *d,void *entry,const BYTE preimage[23])
{
    memset(d,0,sizeof(*d));
    if(((uintptr_t)entry&7)||memcmp(preimage,shape,sizeof(shape)))return 9;
    if(memcmp(entry,preimage,sizeof(shape)))return 10;
    d->entry=entry;d->match_length=23;d->span=19;d->count=4;
    memcpy(d->original,preimage,sizeof(shape));
    d->patch_base=entry;d->patch_size=8;d->patch_offset=0;
    memcpy(d->patch_original,entry,8);
    const BYTE regs[]={3,5,6,7};
    for(unsigned i=0;i<4;i++){d->registers[i]=regs[i];d->widths[i]=1;}
    d->stub_length=102;d->stub=nearby(entry);if(!d->stub)return 11;
    BYTE *p=d->stub;memset(p,0xcc,4096);
    /* Each home store and PUSH occupies its own block. All seven instructions
     * retain the original order; the home stores leave RSP, flags and GPRs alone. */
    for(unsigned i=0;i<7;i++){
        unsigned width=i<3?5:1,source=i<3?i*5:15+i-3;
        memcpy(p+i*16,shape+source,width);
        if(!jump(p+i*16+width,i==6?d->entry+19:p+(i+1)*16))return 12;
    }
    d->table=(RUNTIME_FUNCTION *)(p+128);
    d->table->BeginAddress=0;d->table->EndAddress=d->stub_length;d->table->UnwindData=256;
    BYTE unwind[12]={1,102,4,0};
    for(unsigned i=0;i<4;i++){
        unsigned j=3-i;unwind[4+i*2]=(BYTE)((3+j)*16+1);unwind[5+i*2]=(BYTE)(regs[j]<<4);
    }
    memcpy(p+256,unwind,sizeof(unwind));
    DWORD old;if(!VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old))return 13;
    if(!RtlAddFunctionTable(d->table,1,(DWORD64)p))return 14;
    return 0;
}
int home_entry_unwind_check(split_detour *d)
{
    const DWORD64 values[]={0x1337,0x2448,0x3559,0x466a};
    for(unsigned op=0;op<7;op++)for(unsigned after=0;after<2;after++){
        DWORD64 stack[48]={0},*top=stack+16;top[0]=0x123456789abULL;
        unsigned done=op+after,n=done>3?done-3:0;
        for(unsigned i=0;i<n;i++)top[-(int)i-1]=values[i];
        CONTEXT c;memset(&c,0,sizeof(c));
        c.Rip=(DWORD64)d->stub+op*16+(after?(op<3?5:1):0);c.Rsp=(DWORD64)(top-n);
        c.Rdx=0xdead1111;c.R8=0xbeef2222;c.R9=0xcafe3333;
        if(done>0)top[2]=c.Rdx;if(done>1)top[3]=c.R8;if(done>2)top[4]=c.R9;
        DWORD64 *r=&c.Rax;for(unsigned i=0;i<4;i++)r[d->registers[i]]=n>i?0:values[i];
        PVOID handler=NULL;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,(DWORD64)d->stub,c.Rip,d->table,&c,&handler,&frame,NULL);
        if(c.Rip!=top[0]||c.Rsp!=(DWORD64)(top+1))return 20+(int)(op*2+after);
        for(unsigned i=0;i<4;i++)if(r[d->registers[i]]!=values[i])return 20+(int)(op*2+after);
        if(c.Rdx!=0xdead1111||c.R8!=0xbeef2222||c.R9!=0xcafe3333)return 20+(int)(op*2+after);
    }
    return 0;
}
/* ---- Generalized optional sites (catalog18b: rsp-based or r11-frame stores). The functions above stay as qualified. ---- */
static DWORD pre_count(const home_site *s){return (s->r11_frame?1:0)+s->stores;}           /* instructions before the PUSH run */
static DWORD pre_width(const home_site *s,unsigned i)                                        /* width of pre-push instruction i */
{if(s->r11_frame)return i==0?3:4;return 5;}
static DWORD site_span(const home_site *s)
{DWORD span=0;for(unsigned i=0;i<pre_count(s);i++)span+=pre_width(s,i);for(unsigned p=0;p<s->push_count;p++)span+=s->push_widths[p];return span;}
static int site_shape_valid(const home_site *s)
{
    DWORD at=0;
    if(s->stores<1||s->stores>4||s->push_count<1||s->push_count>8)return 0;
    if(s->r11_frame){
        const BYTE *b=s->preimage;if(b[0]!=0x4c||b[1]!=0x8b||b[2]!=0xdc)return 0;at=3;   /* mov r11,rsp */
        for(unsigned i=0;i<s->stores;i++,at+=4){
            b=s->preimage+at; /* mov [r11+disp8],reg64: REX.W+B (+R), 89, modrm mod=01 rm=011 */
            if((b[0]!=0x49&&b[0]!=0x4d)||b[1]!=0x89||(b[2]&0xc7)!=0x43||b[3]<8||(b[3]&7))return 0;
        }
    }else for(unsigned i=0;i<s->stores;i++,at+=5){
        const BYTE *b=s->preimage+at; /* mov [rsp+disp8],reg64 */
        if((b[0]!=0x48&&b[0]!=0x4c)||b[1]!=0x89||(b[2]&0xc7)!=0x44||b[3]!=0x24||b[4]<8||(b[4]&7))return 0;
    }
    for(unsigned p=0;p<s->push_count;p++){
        BYTE r=s->push_registers[p],w=s->push_widths[p];const BYTE *b=s->preimage+at;
        if(r==4||r>15||w!=(r>=8?2:1))return 0;
        if(w==1&&b[0]!=0x50+r)return 0;
        if(w==2&&(b[0]!=0x41||b[1]!=0x50+(r-8)))return 0;
        at+=w;
    }
    if(at!=site_span(s)||at<8||at+s->alloc_length!=s->match_length||s->match_length>32)return 0;
    const BYTE *a=s->preimage+at; /* sub rsp,imm8 / sub rsp,imm32 */
    if(s->alloc_length==4)return a[0]==0x48&&a[1]==0x83&&a[2]==0xec;
    if(s->alloc_length==7)return a[0]==0x48&&a[1]==0x81&&a[2]==0xec;
    return 0;
}
int home_entry_prepare_site(split_detour *d,void *entry,const home_site *s)
{
    memset(d,0,sizeof(*d));
    if(((uintptr_t)entry&7)||!site_shape_valid(s))return 9;
    if(memcmp(entry,s->preimage,s->match_length))return 10;
    DWORD span=site_span(s),pre=pre_count(s),n=pre+s->push_count;
    d->entry=entry;d->match_length=s->match_length;d->span=span;d->count=s->push_count;
    memcpy(d->original,s->preimage,s->match_length);
    d->patch_base=entry;d->patch_size=8;d->patch_offset=0;memcpy(d->patch_original,entry,8);
    for(unsigned i=0;i<s->push_count;i++){d->registers[i]=s->push_registers[i];d->widths[i]=s->push_widths[i];}
    DWORD last=s->push_widths[s->push_count-1];
    d->stub_length=(n-1)*16+last+5;if(d->stub_length>480)return 15; /* code must stay clear of the table at +512 */
    d->stub=nearby(entry);if(!d->stub)return 11;
    BYTE *p=d->stub;memset(p,0xcc,4096);
    /* One block per instruction, original order. mov r11,rsp and the stores leave RSP, flags
     * and the pushed registers alone; r11 equals RSP at entry exactly as in the original. */
    for(unsigned i=0,source=0;i<n;i++){
        unsigned width=i<pre?pre_width(s,i):s->push_widths[i-pre];
        memcpy(p+i*16,s->preimage+source,width);source+=width;
        if(!jump(p+i*16+width,i==n-1?d->entry+span:p+(i+1)*16))return 12;
    }
    d->table=(RUNTIME_FUNCTION *)(p+512);
    d->table->BeginAddress=0;d->table->EndAddress=d->stub_length;d->table->UnwindData=768;
    BYTE unwind[4+2*8+2]={1,(BYTE)d->stub_length,(BYTE)s->push_count,0};
    for(unsigned i=0;i<s->push_count;i++){
        unsigned j=s->push_count-1-i;
        unwind[4+i*2]=(BYTE)((pre+j)*16+s->push_widths[j]);unwind[5+i*2]=(BYTE)(s->push_registers[j]<<4);
    }
    memcpy(p+768,unwind,sizeof(unwind));
    DWORD old;if(!VirtualProtect(p,4096,PAGE_EXECUTE_READ,&old))return 13;
    if(!RtlAddFunctionTable(d->table,1,(DWORD64)p))return 14;
    return 0;
}
int home_entry_unwind_check_site(split_detour *d,const home_site *s)
{
    DWORD pre=pre_count(s),n=pre+s->push_count;
    for(unsigned op=0;op<n;op++)for(unsigned after=0;after<2;after++){
        DWORD64 stack[48]={0},*top=stack+24,values[8];top[0]=0x123456789abULL;
        unsigned done=op+after,pushed=done>pre?done-pre:0;
        for(unsigned i=0;i<8;i++)values[i]=0x1337+0x1111*i;
        for(unsigned i=0;i<pushed;i++)top[-(int)i-1]=values[i];
        CONTEXT c;memset(&c,0,sizeof(c));
        unsigned width=op<pre?pre_width(s,op):s->push_widths[op-pre];
        c.Rip=(DWORD64)d->stub+op*16+(after?width:0);c.Rsp=(DWORD64)(top-pushed);
        DWORD64 *r=&c.Rax;for(unsigned i=0;i<s->push_count;i++)r[s->push_registers[i]]=pushed>i?0:values[i];
        PVOID handler=NULL;DWORD64 frame=0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,(DWORD64)d->stub,c.Rip,d->table,&c,&handler,&frame,NULL);
        if(c.Rip!=top[0]||c.Rsp!=(DWORD64)(top+1))return 20+(int)(op*2+after);
        for(unsigned i=0;i<s->push_count;i++)if(r[s->push_registers[i]]!=values[i])return 40+(int)(op*2+after);
    }
    return 0;
}
