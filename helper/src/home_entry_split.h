/* Private, fixed-build entry relocation. The three argument home stores are
 * real instructions, not hotpatch padding; preserve them before split PUSHes. */
#ifndef HOME_ENTRY_SPLIT_H
#define HOME_ENTRY_SPLIT_H
#include "split_push_detour.h"
int home_entry_prepare(split_detour *d, void *entry, const BYTE preimage[23]);
int home_entry_unwind_check(split_detour *d);
/* Generalized optional sites: home stores, then a PUSH run, then a stack allocation
 * that stays in place. Stores are 5-byte `mov [rsp+disp8],reg`, or, with r11_frame,
 * `mov r11,rsp` (4c 8b dc) followed by 4-byte `mov [r11+disp8],reg`. Exact preimage required. */
typedef struct home_site {
    const char *owned_export;
    DWORD rva;
    BYTE preimage[32];
    DWORD stores, push_count, alloc_length, match_length, r11_frame;
    BYTE push_registers[8], push_widths[8];
} home_site;
int home_entry_prepare_site(split_detour *d, void *entry, const home_site *s);
int home_entry_unwind_check_site(split_detour *d, const home_site *s);
#endif
