#include <stdio.h>
#include "game_build.h"
int main(void) {
 unsigned old=game_build_from_metadata(1791392914u,239846912u),fresh=game_build_from_metadata(1792431803u,239855616u);
 if(old!=GAME_BUILD_1918 || fresh!=GAME_BUILD_1921)return 1;
 if(game_build_from_metadata(1792431803u,239855616u-4096) || game_build_from_metadata(1792431803u-1,239855616u))return 2;
 if(game_rva_for_build(0,0x3264f0) || game_rva_for_build(3,0x3264f0))return 3;
 if(game_rva_for_build(old,0x30a0075)!=0x30a0075 || game_rva_for_build(old,0x4b63834)!=0x4b63834)return 4;
 if(game_rva_for_build(fresh,0x3264f0)!=0x3264f0 || game_rva_for_build(fresh,0x31c9f0)!=0x31c9f0)return 5;
 puts("PASS known old/new metadata, mismatched-size and timestamp negatives, unsupported build refusal, old and unchanged entry preservation");
 return 0;
}
