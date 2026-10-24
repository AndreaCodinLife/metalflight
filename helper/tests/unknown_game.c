#include <windows.h>
#include <stdio.h>
typedef LONG (WINAPI *get_status)(void);
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 HMODULE h=LoadLibraryA(argv[1]);if(!h)return 3;
 get_status get=(get_status)GetProcAddress(h,"SplitStatus");if(!get)return 4;
 for(unsigned i=0;i<200 && !get();i++)Sleep(10);
 printf("unknown image helper state=%ld\n",get());return get()==-102?0:5;
}
