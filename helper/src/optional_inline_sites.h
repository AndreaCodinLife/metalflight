#ifndef OPTIONAL_INLINE_SITES_H
#define OPTIONAL_INLINE_SITES_H
/* catalog18c: optional, module-aware inline sites, prepared by the frozen core primitive
 * (split_prepare_profile) at the PUSH-run start. Installed in phase 2 only, after the 22;
 * a failure is logged and skipped. Preimages are the module's own bytes (Wine's kernelbase
 * is unobfuscated; hash pinned in results/hw-progress-20261017/kernelbase-split-census.json). */
#include "split_push_detour.h"
typedef struct optional_inline_site {const char *owned_export,*module;DWORD rva;split_profile runtime;} optional_inline_site;
#define OPTIONAL_INLINE_COUNT 5u
static const optional_inline_site optional_inline[OPTIONAL_INLINE_COUNT]={
    /* kernelbase function 0x3de20: lea rsp,[rsp+0] pad; run at +8 (mission-start hang, run 194810) */
    {.owned_export="owned_kb_target",.module="kernelbase.dll",.rva=0x3de28u,
     .runtime={.original={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x57,0x56,0x53,0x48,0x81,0xec,0x98},
               .widths={2,2,2,2,1,1,1,1},.registers={15,14,13,12,5,7,6,3},.count=8,.span=12,.match_length=16}},
    /* Observed worker fault game+30a2665; jump includes the real RBX home save. */
    {.owned_export="owned_game30_target",.module=NULL,.rva=0x30a2660u,
     .runtime={.original={0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x48,0x81,0xec,0x40,0x04,0x00,0x00,0x48},
               .widths={1,1,1},.registers={5,6,7},.count=3,.span=8,.match_length=16,
               .prefix_length=5,.prefix_register=3,.prefix_stack_offset=16}},
    /* Observed fault mimalloc+10f6a (runs 195847, 104242): home stores RBX/R9 stay in place; run at +0xa. */
    {.owned_export="owned_mi_target",.module="mimalloc.dll",.rva=0x10f6au,
     .runtime={.original={0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30,0x48},
               .widths={1,1,1,2,2,2,2},.registers={5,6,7,12,13,14,15},.count=7,.span=11,.match_length=16}},
    /* Observed fault game+0x4107cb0 (v4 runs 140639, 141033): seven pushes, no home stores,
     * lea rbp,[rsp-27h] then sub rsp,0xD0. Same family as the mimalloc site. */
    {.owned_export="owned_gi_target",.module=NULL,.rva=0x4107cb0u,
     .runtime={.original={0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48},
               .widths={1,1,1,1,2,2,2},.registers={5,3,6,7,12,14,15},.count=7,.span=10,.match_length=16}},
    /* Observed fault ntdll+0x2b240 (gptk-d3dshared-01, during exit): RtlImageDirectoryEntryToData
     * pushes RBP,RDI,RSI,RBX (span 4, shorter than the jump); SUB RSP,0x28 is relocated as the suffix. */
    {.owned_export="owned_ri_target",.module="ntdll.dll",.rva=0x2b240u,
     .runtime={.original={0x55,0x57,0x56,0x53,0x48,0x83,0xec,0x28,0x31,0xc0,0x48,0x89,0xcb,0xf6,0xc1,0x01},
               .widths={1,1,1,1},.registers={5,7,6,3},.count=4,.span=4,.match_length=16,.suffix_length=4}},
};
#endif
