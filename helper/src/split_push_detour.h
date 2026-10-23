/* Compatibility experiment: preserve PUSH semantics, change basic-block layout. */
#ifndef SPLIT_PUSH_DETOUR_H
#define SPLIT_PUSH_DETOUR_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
typedef struct split_profile {
    BYTE original[16],widths[8],registers[8];
    DWORD count,span,match_length;
    DWORD prefix_length,prefix_register,prefix_stack_offset;
    DWORD suffix_length; /* 0, or 4: an exact SUB RSP,imm8 after a run shorter than the jump */
} split_profile;
typedef struct split_detour {
    BYTE *entry, *stub;
    BYTE *patch_base;
    BYTE patch_original[16];
    DWORD patch_size,patch_offset;
    DWORD original_protect;
    DWORD peers, retries;
    LONG state; /* 0 untouched, 1 installed, -1 source changed but protection failed */
    RUNTIME_FUNCTION *table;
    BYTE original[32];
    DWORD match_length,span,stub_length;
    BYTE widths[8],registers[8];
    DWORD count;
    DWORD prefix_length,prefix_register,prefix_stack_offset;
    DWORD suffix_length;
} split_detour;
/* Exact fixed preimages required; no decoding or fallback patterns. */
int split_prepare(split_detour *d, void *entry);
int split_prepare_kind(split_detour *d,void *entry,int mimalloc);
int split_prepare_profile(split_detour *d,void *entry,const split_profile *profile);
int split_unwind_check(split_detour *d);
/* All guest peers paused; loader lock prevents new guest entry during the edit.
 * Returns 0 only after restored page protection and all peers resumed. */
int split_install(split_detour *d, int restore);
void split_discard(split_detour *d); /* only legal while uninstalled */
#endif
