/* Owned callers and heap-cleanup paths. No game or game assets loaded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {BYTE padding[16];int value;} Box;
typedef struct {BYTE pad[16];Box *fallback;uint64_t flags;Box *primary;void *allocation;} Context;
static volatile LONG cleanup_calls,errors;
static int WINAPI cleanup(Context *c) {
 if(!c->allocation || !HeapFree(GetProcessHeap(),0,c->allocation))InterlockedIncrement(&errors);
 else {c->allocation=NULL;InterlockedIncrement(&cleanup_calls);}return 6;
}
static BYTE *emit(BYTE *p,const void *bytes,SIZE_T n){memcpy(p,bytes,n);return p+n;}
#define E(p,...) do {const BYTE data[]={__VA_ARGS__};p=emit(p,data,sizeof(data));}while(0)
static BYTE *call_relative(BYTE *p,BYTE *target) {int32_t rel=(int32_t)(target-(p+5));*p++=0xe8;memcpy(p,&rel,4);return p+4;}
static BYTE *call_cleanup(BYTE *p) {uint64_t address=(ULONG_PTR)&cleanup;E(p,0x48,0xb8);p=emit(p,&address,8);E(p,0xff,0xd0);return p;}
typedef int (*Caller)(Context *);
typedef Box *(*Collector)(Context *,uint64_t *);
static Caller caller,other;
static Collector collector;
static Box primary={{0},77},fallback={{0},88};
static BOOL run_one(unsigned mode) {
 Context c={0};c.primary=(mode==0?&primary:NULL);c.fallback=(mode==1?&fallback:NULL);
 c.allocation=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,96);if(!c.allocation)return FALSE;
 int got=caller(&c),want=mode==0?77:mode==1?88:6;
 BOOL ok=got==want && !c.allocation;
 if(c.allocation)HeapFree(GetProcessHeap(),0,c.allocation);
 return ok;
}
static DWORD WINAPI worker(void *unused) {
 (void)unused;for(unsigned i=0;i<200;i++)if(!run_one(i%3))InterlockedIncrement(&errors);return 0;
}
static uint64_t baseline_flags[3];
static BOOL unchanged_flags(Collector collect) {
 Context c={0};uint64_t flags=0;
 c.primary=&primary;Box *got=collect(&c,&flags);if(got!=&primary || (flags&0x8c5)!=baseline_flags[0])return FALSE;
 c.primary=NULL;c.fallback=&fallback;got=collect(&c,&flags);if(got!=&fallback || (flags&0x8c5)!=baseline_flags[1])return FALSE;
 c.fallback=NULL;got=collect(&c,&flags);return !got && (flags&0x8c5)==baseline_flags[2];
}
static LONG CALLBACK expected_null(EXCEPTION_POINTERS *e) {
 if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
    e->ExceptionRecord->ExceptionInformation[0]==0 && e->ExceptionRecord->ExceptionInformation[1]==16) {
  printf("GUARD negative unguarded_null_read=16 observed=1\n");ExitProcess(0);
 }
 return EXCEPTION_CONTINUE_SEARCH;
}
static volatile LONG racing_ready,racing_stop;
static DWORD WINAPI racing_worker(void *unused) {
 (void)unused;InterlockedIncrement(&racing_ready);
 while(!InterlockedCompareExchange(&racing_stop,0,0))if(!unchanged_flags(collector))InterlockedIncrement(&errors);
 return 0;
}
int main(int argc,char **argv) {
 setvbuf(stdout,NULL,_IONBF,0);if(argc!=3)return 2;
 const char *mode=argv[2];BOOL disabled=!strcmp(mode,"off") || !strcmp(mode,"negative-null");
 if(!SetEnvironmentVariableA("MSFS24_OWNED_MISSION_GUARD","1"))return 2;
 BYTE *arena=VirtualAlloc(NULL,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
 static const BYTE getter[]={0x48,0x8b,0x41,0x20,0x48,0x85,0xc0,0x75,0x04,0x48,0x8b,0x41,0x10,0xc3};
 memcpy(arena,getter,sizeof(getter));BYTE *p=arena+0x100;
 E(p,0x48,0x83,0xec,0x38,0x48,0x89,0x4c,0x24,0x20);p=call_relative(p,arena);BYTE *return_pc=p;
 E(p,0x8b,0x40,0x10,0x89,0x44,0x24,0x28,0x48,0x8b,0x4c,0x24,0x20);p=call_cleanup(p);
 E(p,0x8b,0x44,0x24,0x28,0x48,0x83,0xc4,0x38,0xc3);caller=(Caller)(arena+0x100);
 p=arena+0x180;E(p,0x48,0x8b,0x4c,0x24,0x20);p=call_cleanup(p);E(p,0x48,0x83,0xc4,0x38,0xc3);SIZE_T failure_size=p-(arena+0x180);
 p=arena+0x200;E(p,0x48,0x83,0xec,0x28);p=call_relative(p,arena);E(p,0x48,0x85,0xc0,0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0x48,0x83,0xc4,0x28,0xc3);other=(Caller)(arena+0x200);
 p=arena+0x280;E(p,0x48,0x83,0xec,0x28,0x48,0x89,0x54,0x24,0x20);p=call_relative(p,arena);
 E(p,0x9c,0x59,0x48,0x8b,0x54,0x24,0x20,0x48,0x89,0x0a,0x48,0x83,0xc4,0x28,0xc3);collector=(Collector)(arena+0x280);
 BYTE before[14],expected_caller[32],expected_cleanup[32];memcpy(before,arena,sizeof(before));SIZE_T caller_size=return_pc-(arena+0x100);memcpy(expected_caller,arena+0x100,caller_size);memcpy(expected_cleanup,arena+0x180,failure_size);
 if(!strcmp(mode,"bad-getter"))arena[12]^=1;
 if(!strcmp(mode,"bad-caller"))expected_caller[0]^=1;
 if(!strcmp(mode,"bad-cleanup"))expected_cleanup[0]^=1;
 DWORD old;if(!VirtualProtect(arena,0x10000,PAGE_EXECUTE_READ,&old))return 2;FlushInstructionCache(GetCurrentProcess(),arena,0x10000);
 if(strcmp(mode,"bad-getter")) {
  Context baseline={0};uint64_t f=0;
  baseline.primary=&primary;collector(&baseline,&f);baseline_flags[0]=f&0x8c5;
  baseline.primary=NULL;baseline.fallback=&fallback;collector(&baseline,&f);baseline_flags[1]=f&0x8c5;
  baseline.fallback=NULL;collector(&baseline,&f);baseline_flags[2]=f&0x8c5;
 }
 HMODULE dll=LoadLibraryA(argv[1]);if(!dll){printf("GUARD load error=%lu\n",GetLastError());return 2;}
 LONG (WINAPI *install)(BYTE *,BYTE *,SIZE_T,const BYTE *,BYTE *,BYTE *,SIZE_T,const BYTE *)=(void *)GetProcAddress(dll,"MissionGuardInstallOwned");
 LONG64 (WINAPI *count)(void)=(void *)GetProcAddress(dll,"MissionGuardCount");if(!install || !count)return 2;
 HANDLE racers[8]={0};BOOL racing=!strcmp(mode,"install-race");
 if(racing) {
  for(unsigned i=0;i<8;i++){racers[i]=CreateThread(NULL,0,racing_worker,NULL,0,NULL);if(!racers[i])return 2;}
  while(InterlockedCompareExchange(&racing_ready,0,0)!=8)Sleep(1);
  Sleep(10);
 }
 LONG state=disabled?0:install(arena,arena+0x100,caller_size,expected_caller,return_pc,arena+0x180,failure_size,expected_cleanup);
 if(racing) {
  Sleep(20);InterlockedExchange(&racing_stop,1);
  if(WaitForMultipleObjects(8,racers,TRUE,30000)!=WAIT_OBJECT_0)return 2;
  for(unsigned i=0;i<8;i++)CloseHandle(racers[i]);
  printf("GUARD install_race errors=%ld state=%ld\n",errors,state);
 }
 if(!strcmp(mode,"bad-getter") || !strcmp(mode,"bad-caller") || !strcmp(mode,"bad-cleanup")) {
  LONG expected=!strcmp(mode,"bad-getter")?-11:!strcmp(mode,"bad-caller")?-12:-13;
  BYTE expected_getter[14];memcpy(expected_getter,before,14);
  if(!strcmp(mode,"bad-getter"))expected_getter[12]^=1;
  BOOL ok=state==expected && !memcmp(arena,expected_getter,14) && count()==0;
  printf("GUARD reject mode=%s state=%ld pass=%u\n",mode,state,ok);return ok?0:1;
 }
 if(!strcmp(mode,"negative-null")) {
  if(!AddVectoredExceptionHandler(1,expected_null))return 2;
  Context c={0};caller(&c);printf("GUARD negative unexpectedly_returned=1\n");return 1;
 }
 if(disabled) {
  Context c={0};BOOL ok=!memcmp(arena,before,14) && other(&c)==1 && unchanged_flags(collector) && count()==0;
  printf("GUARD disabled pass=%u\n",ok);return ok?0:1;
 }
 if(state!=1){printf("GUARD install state=%ld\n",state);return 1;}
 BOOL flags_ok=unchanged_flags(collector);Context empty={0};BOOL other_ok=other(&empty)==1 && count()==0;
 for(unsigned i=0;i<3;i++)if(!run_one(i))InterlockedIncrement(&errors);
 HANDLE threads[8];for(unsigned i=0;i<8;i++){threads[i]=CreateThread(NULL,0,worker,NULL,0,NULL);if(!threads[i])return 2;}
 if(WaitForMultipleObjects(8,threads,TRUE,30000)!=WAIT_OBJECT_0)return 2;
 for(unsigned i=0;i<8;i++)CloseHandle(threads[i]);
 LONG64 want=1+8*66;BOOL ok=!errors && flags_ok && other_ok && cleanup_calls==1603 && count()==want;
 printf("GUARD summary state=%ld errors=%ld flags_preserved=%u unrelated_empty_unchanged=%u cleanup_calls=%ld redirects=%lld expected_redirects=%lld pass=%u\n",state,errors,flags_ok,other_ok,cleanup_calls,(long long)count(),(long long)want,ok);
 return ok?0:1;
}
