#include "split_push_padded.h"
#include <limits.h>
#include <string.h>

/* Duplicated from the frozen core rather than exported from it, exactly as
 * trap_split_detour.c does: the core's source hash is pinned by every qualified
 * lot (catalog16-controls, catalog-trap-controls), so it must not change. */
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

/* The two NOP forms a padding-aware Win64 toolchain emits before a hotpatch
 * point, both of which appear at the start of padded ntdll functions. The census
 * (tools/census_module_split_sites.py) strips exactly these two, so this decode
 * and the census agree on where a padded function's push run begins. */
DWORD split_prologue_pad(const void *function)
{
    const BYTE *p=(const BYTE*)function;
    if(!memcmp(p,"\x48\x8d\xa4\x24\x00\x00\x00\x00",8))return 8;
    if(!memcmp(p,"\x4d\x8b\xc0",3))return 3;
    return 0;
}

int split_prepare_padded(split_detour *d,void *function,const BYTE *preimage,DWORD match_length)
{
    memset(d,0,sizeof(*d));
    DWORD pad=split_prologue_pad(function);
    /* The jump lands at the function start, so only an 8-aligned start gives a
     * single eight-byte atomic replacement for its five bytes. Every padded ntdll
     * site is 8-aligned; an unaligned one is refused rather than half-written. */
    if(!pad || ((uintptr_t)function&7))return 9;
    if(match_length<5 || match_length>16)return 9;
    const BYTE *p=(const BYTE*)function;
    unsigned count=0,run_span=0;
    while(count<8 && pad+run_span<16)
    {
        unsigned at=pad+run_span,rex=0,width=0;
        if(p[at]>=0x40 && p[at]<=0x4f){rex=p[at];width=1;}
        if(at+width>=16 || p[at+width]<0x50 || p[at+width]>0x57)break;
        unsigned reg=p[at+width]-0x50+((rex&1)?8:0);
        if(reg>15 || !(0xf0e8u&(1u<<reg)))break;
        width++;
        d->widths[count]=(BYTE)width;d->registers[count]=(BYTE)reg;
        count++;run_span+=width;
    }
    if(count<2)return 9;
    unsigned seen=0;
    for(unsigned i=0;i<count;i++)
    {
        unsigned reg=d->registers[i];
        if(seen&(1u<<reg))return 9;
        seen|=1u<<reg;
    }
    /* The relocated span is pad plus the run: the padding is what carries the jump
     * over split_prepare_profile's five-byte floor, so if the two together still
     * do not reach five bytes there is nothing to cover. */
    if(pad+run_span<5 || pad+run_span>16 || pad+run_span!=match_length)return 9;
    d->entry=(BYTE*)function;
    d->count=count;d->span=pad+run_span;d->match_length=match_length;
    d->stub_length=16*(d->count-1)+d->widths[d->count-1]+5;
    /* One eight-byte atomic block at the function start, and its preimage held
     * here. split_install re-checks it against the live bytes, so a body that
     * changed between prepare and install is refused before any write. */
    d->patch_size=8;d->patch_base=(BYTE*)function;d->patch_offset=0;
    memcpy(d->original,preimage,16);
    /* The fixed preimage is enforced here, before any stub or patch is built: the
     * live pad+run must equal the pinned bytes, the same gate split_prepare_profile
     * applies at the run start. */
    if(memcmp(p,preimage,match_length))return 10;
    memcpy(d->patch_original,d->patch_base,d->patch_size);
    d->stub=allocate_near(d->entry);if(!d->stub)return 11;
    BYTE *q=d->stub;memset(q,0xcc,4096);
    /* The padding is NOT replayed: it is NOP, so dropping it executes the same
     * thing, and leaving it out keeps the stub's unwind record describing only the
     * pushes it actually performs. */
    unsigned source=pad;
    for(unsigned i=0;i<d->count;i++)
    {
        unsigned at=i*16;
        memcpy(q+at,p+source,d->widths[i]);source+=d->widths[i];
        if(!relative(q+at+d->widths[i],i==d->count-1?d->entry+d->span:q+at+16))return 12;
    }
    d->table=(RUNTIME_FUNCTION*)(q+128);
    d->table->BeginAddress=0;d->table->EndAddress=d->stub_length;d->table->UnwindData=256;
    BYTE unwind[20]={1,(BYTE)d->stub_length,(BYTE)d->count,0};
    for(unsigned i=0;i<d->count;i++)
    {unsigned reg=d->count-1-i;unwind[4+2*i]=(BYTE)(reg*16+d->widths[reg]);unwind[5+2*i]=(BYTE)(d->registers[reg]<<4);}
    memcpy(q+256,unwind,((4+2*d->count+3)/4)*4);
    DWORD previous;
    if(!VirtualProtect(q,4096,PAGE_EXECUTE_READ,&previous))return 13;
    if(!RtlAddFunctionTable(d->table,1,(DWORD64)q))return 14;
    return 0;
}
