/* Wine/Rosetta-only experiment: an empty getter result at ONE verified caller
 * takes that caller's existing cleanup/return-6 path. No substitute object or
 * successful API result. The selected default never loads this build. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const BYTE getter_bytes[]={0x48,0x8b,0x41,0x20,0x48,0x85,0xc0,0x75,0x04,0x48,0x8b,0x41,0x10,0xc3};
static const BYTE game_caller[]={0x49,0x8b,0xcc,0xe8,0xdc,0x0c,0xbb,0xff,0x48,0x8d,0x50,0x10,0x48,0x8d,0x8d,0xa0,0x08,0x00,0x00,0xe8,0xfc,0xd9,0x90,0xff};
static const BYTE game_cleanup[]={0x33,0xd2,0x48,0x8d,0x4d,0xf0,0xe8,0x58,0x48,0x00,0x00,0x90,0xb8,0x06,0x00,0x00,0x00};
static volatile LONG guard_state;
static volatile LONG64 *guard_count;
static BOOL guard_flag(const char *name) {
 char v[2];return GetEnvironmentVariableA(name,v,sizeof(v))==1 && v[0]=='1';
}
static BOOL owned_name(void) {
 char path[MAX_PATH];DWORD n=GetModuleFileNameA(NULL,path,sizeof(path));
 if(!n || n>=sizeof(path))return FALSE;
 char *p=strrchr(path,'\\');return !_stricmp(p?p+1:path,"owned_mission_guard.exe") && guard_flag("MSFS24_OWNED_MISSION_GUARD");
}
static BOOL code_matches(BYTE *p,const BYTE *expected,SIZE_T size,BOOL image) {
 MEMORY_BASIC_INFORMATION m;BYTE bytes[64];SIZE_T got=0;
 if(size>sizeof(bytes) || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return FALSE;
 if(image && m.Type!=MEM_IMAGE)return FALSE;
 if((m.Protect&0xff)!=PAGE_EXECUTE_READ && (m.Protect&0xff)!=PAGE_EXECUTE_READWRITE && (m.Protect&0xff)!=PAGE_EXECUTE_WRITECOPY)return FALSE;
 SIZE_T offset=(SIZE_T)(p-(BYTE *)m.BaseAddress);if(offset>m.RegionSize || size>m.RegionSize-offset)return FALSE;
 return ReadProcessMemory(GetCurrentProcess(),p,bytes,size,&got) && got==size && !memcmp(bytes,expected,size);
}
static BYTE *near_pages(BYTE *entry) {
 SYSTEM_INFO info;GetSystemInfo(&info);ULONG_PTR step=info.dwAllocationGranularity,center=(ULONG_PTR)entry & ~(step-1);
 for(ULONG_PTR distance=step;distance<0x70000000;distance+=step) {
  for(unsigned side=0;side<2;side++) {
   if(!side && center<distance+0x10000)continue;
   ULONG_PTR at=side?center+distance:center-distance;MEMORY_BASIC_INFORMATION m;
   if(at<(ULONG_PTR)info.lpMinimumApplicationAddress || at>(ULONG_PTR)info.lpMaximumApplicationAddress-8192)continue;
   if(!VirtualQuery((void *)at,&m,sizeof(m)) || m.State!=MEM_FREE || m.RegionSize<8192)continue;
   BYTE *p=VirtualAlloc((void *)at,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
   if(p)return p;
  }
 }
 return NULL;
}
static LONG install_guard(BYTE *getter,BYTE *caller,SIZE_T caller_size,const BYTE *expected_caller,
                         BYTE *return_pc,BYTE *cleanup,SIZE_T cleanup_size,const BYTE *expected_cleanup,BOOL image) {
 if((ULONG_PTR)getter&7)return -10;
 if(!code_matches(getter,getter_bytes,sizeof(getter_bytes),image))return -11;
 if(!code_matches(caller,expected_caller,caller_size,image))return -12;
 if(!code_matches(cleanup,expected_cleanup,cleanup_size,image))return -13;
 if(return_pc<caller || return_pc>caller+caller_size || return_pc-caller<5 || return_pc[-5]!=0xe8)return -14;
 int32_t call;memcpy(&call,return_pc-4,4);if(return_pc+call!=getter)return -15;
 BYTE *pages=near_pages(getter);if(!pages)return -16;
 /* No stack allocation or register spills. The original primary-result path
  * preserves its TEST flags. On the fallback path CMP RAX,RAX restores the
  * original zero-TEST condition flags (AF was undefined). No GPR except the
  * original RAX result is changed. Windows ABI does not promise these flags,
  * but this also avoids changing them unnecessarily for unrelated callers.
  * The chosen empty call returns directly to its existing cleanup. This
  * continuation rewrite is not suitable for a CET shadow-stack process. */
 BYTE stub[]={
  0x48,0x8b,0x41,0x20,0x48,0x85,0xc0,0x75,0x36,0x48,0x8b,0x41,0x10,
  0x48,0x85,0xc0,0x75,0x2a,
  0x81,0x3c,0x24,0,0,0,0,0x75,0x21,
  0x81,0x7c,0x24,0x04,0,0,0,0,0x75,0x17,
  0xf0,0x48,0xff,0x05,0,0,0,0,
  0xc7,0x04,0x24,0,0,0,0,
  0xc7,0x44,0x24,0x04,0,0,0,0,
  0x48,0x39,0xc0,0xc3};
 if(sizeof(stub)!=64){VirtualFree(pages,0,MEM_RELEASE);return -17;}
 uint64_t ret=(ULONG_PTR)return_pc,fail=(ULONG_PTR)cleanup;
 uint32_t low=(uint32_t)ret,high=(uint32_t)(ret>>32);memcpy(stub+21,&low,4);memcpy(stub+31,&high,4);
 low=(uint32_t)fail;high=(uint32_t)(fail>>32);memcpy(stub+48,&low,4);memcpy(stub+56,&high,4);
 int32_t displacement=4096-45;memcpy(stub+41,&displacement,4);memcpy(pages,stub,sizeof(stub));
 DWORD old;
 if(!VirtualProtect(pages,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(pages,0,MEM_RELEASE);return -18;}
 if(!FlushInstructionCache(GetCurrentProcess(),pages,64)){VirtualFree(pages,0,MEM_RELEASE);return -24;}
 BYTE patch[8]={0xe9,0,0,0,0,0x90,0x90,0x90};int64_t relative=(intptr_t)pages-((intptr_t)getter+5);
 if(relative<INT32_MIN || relative>INT32_MAX){VirtualFree(pages,0,MEM_RELEASE);return -19;}
 displacement=(int32_t)relative;memcpy(patch+1,&displacement,4);LONG64 before,replacement;memcpy(&before,getter_bytes,8);memcpy(&replacement,patch,8);
 if(!VirtualProtect(getter,8,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(pages,0,MEM_RELEASE);return -20;}
 BOOL ok=InterlockedCompareExchange64((volatile LONG64 *)getter,replacement,before)==before;DWORD ignored;
 BOOL protected=VirtualProtect(getter,8,old,&ignored);
 BOOL flushed=FlushInstructionCache(GetCurrentProcess(),getter,8);
 if(!ok){VirtualFree(pages,0,MEM_RELEASE);return -21;}
 if(!protected || !flushed) {
  /* Once published, retain the stub even on rollback: another caller may
   * already be executing it. Never free potentially live executable code. */
  DWORD rollback_old;
  if(VirtualProtect(getter,8,PAGE_EXECUTE_READWRITE,&rollback_old)) {
   InterlockedCompareExchange64((volatile LONG64 *)getter,before,replacement);
   VirtualProtect(getter,8,old,&ignored);
   FlushInstructionCache(GetCurrentProcess(),getter,8);
  }
  return !protected?-22:-23;
 }
 guard_count=(volatile LONG64 *)(pages+4096);return 1;
}
__declspec(dllexport) LONG WINAPI MissionGuardState(void) {return InterlockedCompareExchange(&guard_state,0,0);}
__declspec(dllexport) LONG64 WINAPI MissionGuardCount(void) {return guard_count?InterlockedCompareExchange64(guard_count,0,0):0;}
__declspec(dllexport) LONG WINAPI MissionGuardInstallOwned(BYTE *getter,BYTE *caller,SIZE_T caller_size,const BYTE *caller_bytes,
 BYTE *return_pc,BYTE *cleanup,SIZE_T cleanup_size,const BYTE *cleanup_bytes) {
 if(!owned_name())return -100;
 if(InterlockedCompareExchange(&guard_state,-1,0))return -101;
 LONG result=install_guard(getter,caller,caller_size,caller_bytes,return_pc,cleanup,cleanup_size,cleanup_bytes,FALSE);
 InterlockedExchange(&guard_state,result);return result;
}
static DWORD WINAPI game_worker(void *unused) {
 (void)unused;char path[MAX_PATH];DWORD n=GetModuleFileNameA(NULL,path,sizeof(path));
 if(!n || n>=sizeof(path))return 0;char *name=strrchr(path,'\\');name=name?name+1:path;
 if(_stricmp(name,"FlightSimulator2024.exe")){InterlockedExchange(&guard_state,-100);return 0;}
 BYTE *base=(BYTE *)GetModuleHandleW(NULL);LONG state=-30;
 for(unsigned i=0;i<1800;i++) {
  if(code_matches(base+0x4714510,getter_bytes,sizeof(getter_bytes),TRUE) &&
     code_matches(base+0x4b6382c,game_caller,sizeof(game_caller),TRUE) &&
     code_matches(base+0x4b6384d,game_cleanup,sizeof(game_cleanup),TRUE)) {
   state=install_guard(base+0x4714510,base+0x4b6382c,sizeof(game_caller),game_caller,
     base+0x4b63834,base+0x4b6384d,sizeof(game_cleanup),game_cleanup,TRUE);break;
  }
  Sleep(100);
 }
 InterlockedExchange(&guard_state,state);char line[192];
 snprintf(line,sizeof(line),"[MISSION EMPTY GUARD] installed=%d state=%ld caller=4b63834 cleanup=4b6384d existing_return=6\n",state==1,state);OutputDebugStringA(line);
 LONG64 last=-1;unsigned emitted=0;
 if(state==1)for(unsigned i=0;i<1440 && emitted<32;i++) {
  Sleep(5000);LONG64 count=MissionGuardCount();if(count==last)continue;last=count;
  snprintf(line,sizeof(line),"[MISSION EMPTY GUARD] sample=%u empty_calls_redirected=%lld existing_return=6\n",++emitted,(long long)count);OutputDebugStringA(line);
 }
 return 0;
}
void MissionEmptyGuardStart(void) {
 if(!guard_flag("MSFS24_MISSION_EMPTY_GUARD"))return;
 HANDLE t=CreateThread(NULL,0,game_worker,NULL,0,NULL);if(t)CloseHandle(t);else InterlockedExchange(&guard_state,-31);
}
#ifdef MISSION_GUARD_STANDALONE
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
 (void)reserved;if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);MissionEmptyGuardStart();}return TRUE;
}
#endif
