/* Cover a split-push site from its FUNCTION START, relocating the leading
 * hotpatch padding with the push run.
 *
 * catalog16trap-lazy01 stopped at ntdll+0x27DA8 (RtlFreeHeap+0x8): bytes
 * 55 57 56 53 48 81 ec 88 00 00 00, a four-push run. Alignment is not the
 * blocker there -- the run starts at block offset 8, so a five-byte jump fits --
 * only split_prepare_profile's span>=5 floor refuses it, because a four-byte run
 * has no five-byte footprint of its own for the jump to cover.
 *
 * But RtlFreeHeap's run starts at function offset 8, not 0: the function begins
 * with the hotpatch NOP `lea rsp,[rsp+0]` (48 8d a4 24 00 00 00 00). The jump can
 * therefore be written at the FUNCTION START, which is 8-aligned in every padded
 * ntdll case, and the relocated span becomes pad+run -- twelve bytes here, well
 * over the floor. The padding is NOP, so relocating the pushes without it
 * executes exactly what the original did.
 *
 * This is the same idea as the home-save prefix the core already carries for
 * mimalloc (mov [rsp+18],rbx relocated with the run): the bytes before the pushes
 * are replayed so the jump lands somewhere it fits. The difference is that the
 * padding is NOP, so it is not replayed and not unwound -- it changes nothing an
 * unwind record could observe -- and only the pushes get per-instruction blocks.
 * The install, unwind-check and discard paths are the frozen core's, unchanged.
 *
 * Fixed preimage required, exactly as split_prepare_profile requires one: the
 * caller supplies the pinned pad+run bytes (for ntdll, straight from the census,
 * since the module is unobfuscated), and the live bytes must match them. */
#ifndef SPLIT_PUSH_PADDED_H
#define SPLIT_PUSH_PADDED_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "split_push_detour.h"

/* The constant pad length at a function start, or 0 if unpadded. Recognises the
 * two NOP forms the Win64 hotpatch spec and the module unwind records allow:
 * `lea rsp,[rsp+0]` (eight bytes) and `mov r8,r8` (three bytes). */
DWORD split_prologue_pad(const void *function);

/* Prepare an inline detour whose jump lands at the function start, relocating the
 * push run that follows the padding. `preimage`/`match_length` are the pinned
 * bytes at the function start (pad plus run); the live bytes must equal them.
 * Returns 0 prepared, 9 validation, 10 the live preimage did not match the pinned
 * one, 11..14 as split_prepare_profile. */
int split_prepare_padded(split_detour *d,void *function,const BYTE *preimage,DWORD match_length);
#endif
