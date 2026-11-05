#ifndef METALFLIGHT_GAME_BUILD_H
#define METALFLIGHT_GAME_BUILD_H
#include <windows.h>
/* Explicit build metadata and exact preimages are independent requirements.
 * Unknown images are refused; no live code scanning or guessed displacement. */
enum { GAME_BUILD_UNKNOWN=0, GAME_BUILD_1918=1, GAME_BUILD_1921=2, GAME_BUILD_1924=3 };
static inline unsigned game_build_from_metadata(DWORD timestamp,DWORD size) {
 if(timestamp==1791392914u && size==239846912u)return GAME_BUILD_1918;
 if(timestamp==1792431803u && size==239855616u)return GAME_BUILD_1921;
 if(timestamp==1793640088u && size==239855104u)return GAME_BUILD_1924;
 return GAME_BUILD_UNKNOWN;
}
static inline unsigned game_current_build(void) {
 BYTE *base=(BYTE *)GetModuleHandleW(NULL);
 IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)base;
 if(!dos || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<64 || dos->e_lfanew>0x100000)return GAME_BUILD_UNKNOWN;
 IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64 *)(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return GAME_BUILD_UNKNOWN;
 return game_build_from_metadata(nt->FileHeader.TimeDateStamp,nt->OptionalHeader.SizeOfImage);
}
static inline DWORD game_rva_for_build(unsigned build,DWORD rva) {
 if(build!=GAME_BUILD_1918 && build!=GAME_BUILD_1921 && build!=GAME_BUILD_1924)return 0;
 if(build==GAME_BUILD_1924)switch(rva) {
  /* Candidate 1.9.24 locations matched from 1.9.21 PE unwind records.
   * Ten adjacent function records on each side match for every relocated
   * target; exact live-code and unwind checks remain mandatory at install. */
  case 0x30a0075u:return 0x30a05e5u;
  case 0x473887au:return 0x473904au;
  case 0x253c015u:return 0x253c6b5u;
  case 0x5dd280u:return 0x5dd240u;
  case 0x30a2660u:return 0x30a2bd0u;
  case 0x4107cb0u:return 0x4108481u;
  /* Mission guard: exact live getter/caller/cleanup preimages remain required. */
  case 0x4714510u:return 0x4714ce0u;
  case 0x4b6382cu:return 0x4b63ffcu;
  case 0x4b63834u:return 0x4b64004u;
  case 0x4b6384du:return 0x4b6401du;
 }
 if(build==GAME_BUILD_1921)switch(rva) {
  case 0x30a0075u:return 0x30a0835u;
  case 0x473887au:return 0x473929au;
  case 0x253c015u:return 0x253c705u;
  case 0x5dd280u:return 0x5dd290u;
  case 0x30a2660u:return 0x30a2e20u;
  /* Build 1.9.21 has a redundant REX prefix (40) at 0x41086d0. The
   * verified split-push preimage starts at the next byte, 0x41086d1. Leaving
   * the prefix in place makes it prefix the E9 detour, which is a valid
   * ignored-REX near jump while preserving the exact checked push sequence. */
  case 0x4107cb0u:return 0x41086d1u;
  case 0x4714510u:return 0x4714f30u;
  case 0x4b6382cu:return 0x4b6424cu;
  case 0x4b63834u:return 0x4b64254u;
  case 0x4b6384du:return 0x4b6426du;
 }
 return rva;
}
#endif
