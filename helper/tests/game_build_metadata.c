#include <stdio.h>
#include "game_build.h"
int main(void) {
 unsigned old=game_build_from_metadata(1791392914u,239846912u),fresh=game_build_from_metadata(1792431803u,239855616u),newer=game_build_from_metadata(1793640088u,239855104u);
 if(old!=GAME_BUILD_1918 || fresh!=GAME_BUILD_1921 || newer!=GAME_BUILD_1924)return 1;
 if(game_build_from_metadata(1793640088u,239855104u-4096) || game_build_from_metadata(1793640088u-1,239855104u))return 2;
 if(game_rva_for_build(0,0x3264f0) || game_rva_for_build(4,0x3264f0))return 3;
 if(game_rva_for_build(old,0x30a0075)!=0x30a0075 || game_rva_for_build(old,0x4b63834)!=0x4b63834)return 4;
 if(game_rva_for_build(fresh,0x3264f0)!=0x3264f0 || game_rva_for_build(fresh,0x31c9f0)!=0x31c9f0)return 5;
 if(game_rva_for_build(newer,0x30a0075)!=0x30a05e5 || game_rva_for_build(newer,0x473887a)!=0x473904a || game_rva_for_build(newer,0x253c015)!=0x253c6b5)return 6;
 if(game_rva_for_build(newer,0x5dd280)!=0x5dd240 || game_rva_for_build(newer,0x30a2660)!=0x30a2bd0 || game_rva_for_build(newer,0x4107cb0)!=0x4108481)return 7;
 if(game_rva_for_build(newer,0x3264f0)!=0x3264f0 || game_rva_for_build(newer,0x31c9f0)!=0x31c9f0)return 8;
 if(game_rva_for_build(newer,0x4714510)!=0x4714ce0 || game_rva_for_build(newer,0x4b6382c)!=0x4b63ffc || game_rva_for_build(newer,0x4b63834)!=0x4b64004 || game_rva_for_build(newer,0x4b6384d)!=0x4b6401d)return 9;
 if(game_rva_for_build(fresh,0x4714510)!=0x4714f30 || game_rva_for_build(fresh,0x4b6382c)!=0x4b6424c || game_rva_for_build(fresh,0x4b63834)!=0x4b64254 || game_rva_for_build(fresh,0x4b6384d)!=0x4b6426d)return 10;
 if(game_rva_for_build(old,0x4714510)!=0x4714510 || game_rva_for_build(old,0x4b6382c)!=0x4b6382c || game_rva_for_build(old,0x4b63834)!=0x4b63834 || game_rva_for_build(old,0x4b6384d)!=0x4b6384d)return 11;
 if(game_rva_for_build(fresh,0x30a0075)!=0x30a0835 || game_rva_for_build(fresh,0x473887a)!=0x473929a || game_rva_for_build(fresh,0x253c015)!=0x253c705 || game_rva_for_build(fresh,0x5dd280)!=0x5dd290 || game_rva_for_build(fresh,0x30a2660)!=0x30a2e20 || game_rva_for_build(fresh,0x4107cb0)!=0x41086d1)return 12;
 puts("PASS known 1.9.18/1.9.21/1.9.24 metadata, mismatch negatives, unsupported build refusal, and all startup plus mission-guard RVA mappings");
 return 0;
}
