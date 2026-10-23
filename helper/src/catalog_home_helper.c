/* Stacked fixed-build candidate: 17 inline sites, 4 ntdll padded sites,
 * and a direct entry jump preserving three real argument home stores.
 * catalog18: optional home-entry sites install in a second phase, only after the
 * 22-site set has installed and reported; a failing optional site is logged and
 * skipped and can never block, delay or roll back the 22.
 * No per-entry exception, trap counter, or altered API results. */
#include "split_push_detour.h"
#include "split_push_padded.h"
#include "home_entry_split.h"
#include "split_push_catalog.h"
#include "home_entry_site.h"
#include "ntdll_padded_sites.h"
#include "optional_inline_sites.h"
#include <stdio.h>
#include <string.h>
_Static_assert(CATALOG_COUNT<=32,"Bound the install set");
_Static_assert(NTDLL_PADDED_COUNT<=16,"Bound the padded set");
static split_detour detours[CATALOG_COUNT];
static split_detour padded[NTDLL_PADDED_COUNT];
static split_detour home;
static split_detour optional_detours[HOME_OPTIONAL_COUNT];
static volatile LONG state, optional_state[HOME_OPTIONAL_COUNT];
static split_detour optional_inline_detours[OPTIONAL_INLINE_COUNT];
static volatile LONG optional_inline_state[OPTIONAL_INLINE_COUNT];
_Static_assert(OPTIONAL_INLINE_COUNT<=8,"Bound the optional inline set");
_Static_assert(HOME_OPTIONAL_COUNT<=4,"Bound the optional set");
__declspec(dllexport) LONG WINAPI SplitStatus(void){return InterlockedCompareExchange(&state,0,0);}
__declspec(dllexport) unsigned WINAPI HomeEntryByte(void){return home.entry?home.entry[0]:0;}
/* 0 pending, 1 installed, negative = skipped with that result code */
__declspec(dllexport) LONG WINAPI OptionalState(unsigned i){return i<HOME_OPTIONAL_COUNT?InterlockedCompareExchange(&optional_state[i],0,0):-1000;}
__declspec(dllexport) LONG WINAPI OptionalInlineState(unsigned i){return i<OPTIONAL_INLINE_COUNT?InterlockedCompareExchange(&optional_inline_state[i],0,0):-1000;}
__declspec(dllexport) unsigned WINAPI OptionalEntryByte(unsigned i){return i<HOME_OPTIONAL_COUNT&&optional_detours[i].entry?optional_detours[i].entry[0]:0;}
static void note(HANDLE log,const char *text)
{if(log!=INVALID_HANDLE_VALUE){DWORD wrote;WriteFile(log,text,(DWORD)strlen(text),&wrote,NULL);FlushFileBuffers(log);}}
static DWORD WINAPI worker(void *unused)
{
    (void)unused;char image[MAX_PATH],path[1024],text[512];HANDLE log=INVALID_HANDLE_VALUE;
    GetModuleFileNameA(NULL,image,sizeof(image));char *name=strrchr(image,'\\');name=name?name+1:image;
    BOOL owned=!_stricmp(name,"owned_catalog_home.exe");
    if(!owned && _stricmp(name,"FlightSimulator2024.exe")){InterlockedExchange(&state,-100);return 0;}
    DWORD n=GetEnvironmentVariableA("MSFS24_LAB_SPLIT_LOG",path,sizeof(path));
    const char prefix[]="C:\\metalflight\\logs\\";
    if(n && n<sizeof(path) && !strncmp(path,prefix,sizeof(prefix)-1))
        log=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(!owned && log==INVALID_HANDLE_VALUE){InterlockedExchange(&state,-101);return 0;}
    note(log,"catalog-home started version=1\n");ULONGLONG began=GetTickCount64();int result=0;
    unsigned installed=0,padded_installed=0;
    /* Every target and its unwind table must validate before any source edit. */
    for(unsigned i=0;i<CATALOG_COUNT;i++)
    {
        const catalog_entry *e=&catalog[i];BYTE *base=(BYTE*)GetModuleHandleA(e->module?"mimalloc.dll":NULL);
        BYTE *entry=owned?(BYTE*)(void*)GetProcAddress(GetModuleHandleA(NULL),e->owned_export):base?base+e->rva:NULL;
        result=10;
        while(entry && GetTickCount64()-began<120000)
        {
            MEMORY_BASIC_INFORMATION m;
            if(VirtualQuery(entry,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
                (m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
            {
                result=split_prepare_profile(&detours[i],entry,owned?&e->owned:&e->runtime);
                if(result!=10)break;
            }
            Sleep(10);
        }
        int unwind=result?-1:split_unwind_check(&detours[i]);if(!result)result=unwind;
        snprintf(text,sizeof(text),"catalog-home prepared target=%u module=%u rva=%lx result=%d unwind=%d\n",i,e->module,e->rva,result,unwind);
        note(log,text);if(result)break;
    }
    /* The padded ntdll sites: same validation, but the preimage is the pinned
     * pad+run from the census and the jump lands at the function start. */
    if(!result)for(unsigned i=0;i<NTDLL_PADDED_COUNT;i++)
    {
        BYTE *base=(BYTE*)GetModuleHandleA("ntdll.dll");
        BYTE *entry=owned?(BYTE*)(void*)GetProcAddress(GetModuleHandleA(NULL),ntdll_padded[i].owned_export):base?base+ntdll_padded[i].rva:NULL;
        result=10;
        while(entry && GetTickCount64()-began<120000)
        {
            MEMORY_BASIC_INFORMATION m;
            if(VirtualQuery(entry,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
                (m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
            {
                result=split_prepare_padded(&padded[i],entry,ntdll_padded[i].preimage,ntdll_padded[i].match_length);
                if(result!=10)break;
            }
            Sleep(10);
        }
        int unwind=result?-1:split_unwind_check(&padded[i]);if(!result)result=unwind;
        snprintf(text,sizeof(text),"catalog-home padded prepared target=%u rva=%x result=%d unwind=%d span=%lu pad=%lu\n",
            i,ntdll_padded[i].rva,result,unwind,(unsigned long)ntdll_padded[i].span,(unsigned long)ntdll_padded[i].pad);
        note(log,text);if(result)break;
    }
    /* The captured prefix contains three argument stores, not padding. */
    if(!result)
    {
        BYTE *base=(BYTE*)GetModuleHandleA(NULL);
        BYTE *entry=owned?(BYTE*)(void*)GetProcAddress(GetModuleHandleA(NULL),"owned_home_target"):base?base+HOME_ENTRY_RVA:NULL;
        result=10;
        while(entry && GetTickCount64()-began<120000)
        {
            MEMORY_BASIC_INFORMATION m;
            if(VirtualQuery(entry,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
               (m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) &&
               (uintptr_t)m.BaseAddress+m.RegionSize>=(uintptr_t)entry+sizeof(home_entry_preimage))
            {
                result=home_entry_prepare(&home,entry,home_entry_preimage);
                if(result!=10)break;
            }
            Sleep(10);
        }
        int unwind=result?-1:home_entry_unwind_check(&home);if(!result)result=unwind;
        snprintf(text,sizeof(text),"catalog-home home prepared rva=%x result=%d unwind=%d stores=3 pushes=4 span=%lu match=%lu\n",
            HOME_ENTRY_RVA,result,unwind,home.span,home.match_length);note(log,text);
    }
    if(!result)for(unsigned i=0;i<CATALOG_COUNT;i++)
    {
        result=split_install(&detours[i],0);if(detours[i].state==1)installed++;
        snprintf(text,sizeof(text),"catalog-home installed target=%u result=%d state=%ld protect=%lu peers=%lu retries=%lu\n",
            i,result,detours[i].state,detours[i].original_protect,detours[i].peers,detours[i].retries);
        note(log,text);if(result)break;
    }
    if(!result)for(unsigned i=0;i<NTDLL_PADDED_COUNT;i++)
    {
        result=split_install(&padded[i],0);if(padded[i].state==1)padded_installed++;
        snprintf(text,sizeof(text),"catalog-home padded installed target=%u rva=%x result=%d state=%ld protect=%lu peers=%lu retries=%lu\n",
            i,ntdll_padded[i].rva,result,padded[i].state,padded[i].original_protect,padded[i].peers,padded[i].retries);
        note(log,text);if(result)break;
    }
    if(!result)
    {
        result=split_install(&home,0);
        snprintf(text,sizeof(text),"catalog-home home installed result=%d state=%ld byte=%02x peers=%lu retries=%lu\n",
            result,home.state,home.entry?home.entry[0]:0,home.peers,home.retries);note(log,text);
    }
    InterlockedExchange(&state,result?-result:1);
    snprintf(text,sizeof(text),"catalog-home complete requested=%u installed=%u inline=%u padded=%u home_state=%ld result=%d elapsed_ms=%llu\n",
        (unsigned)(CATALOG_COUNT+NTDLL_PADDED_COUNT+1),installed+padded_installed+(home.state==1),
        installed,padded_installed,home.state,result,(unsigned long long)(GetTickCount64()-began));note(log,text);
    /* Phase 2: optional sites. Runs only after the 22 succeeded; its outcome never changes `state`.
     * A mismatch can mean "not yet in memory", so it is retried for a bounded time, then skipped. */
    for(unsigned i=0;i<HOME_OPTIONAL_COUNT;i++)
    {
        const home_site *o=&home_optional[i];int r=-999; /* -999: phase 1 failed, optional not attempted */
        if(!result)
        {
            BYTE *base=(BYTE*)GetModuleHandleA(NULL);
            BYTE *entry=owned?(BYTE*)(void*)GetProcAddress(GetModuleHandleA(NULL),o->owned_export):base?base+o->rva:NULL;
            ULONGLONG limit=owned?3000:600000,start=GetTickCount64();r=10;
            while(entry && GetTickCount64()-start<limit)
            {
                MEMORY_BASIC_INFORMATION m;
                if(VirtualQuery(entry,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
                   (m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) &&
                   (uintptr_t)m.BaseAddress+m.RegionSize>=(uintptr_t)entry+o->match_length)
                {
                    r=home_entry_prepare_site(&optional_detours[i],entry,o);
                    if(r!=10)break;
                }
                Sleep(10);
            }
            int unwind=r?-1:home_entry_unwind_check_site(&optional_detours[i],o);if(!r)r=unwind;
            if(!r){r=split_install(&optional_detours[i],0);if(!r&&optional_detours[i].state!=1)r=50;}
            snprintf(text,sizeof(text),"catalog-home optional site=%u rva=%lx result=%d unwind=%d state=%ld stores=%lu pushes=%lu span=%lu match=%lu waited_ms=%llu\n",
                i,o->rva,r,unwind,optional_detours[i].state,o->stores,o->push_count,optional_detours[i].span,o->match_length,
                (unsigned long long)(GetTickCount64()-start));note(log,text);
        }
        InterlockedExchange(&optional_state[i],r==0?1:(r<0?r:-r));
        if(optional_detours[i].state!=1)split_discard(&optional_detours[i]);
    }
    /* Phase 2b: optional module-aware inline sites through the frozen core primitive. */
    for(unsigned i=0;i<OPTIONAL_INLINE_COUNT;i++)
    {
        const optional_inline_site *o=&optional_inline[i];int r=-999,unwind=-1;ULONGLONG start=GetTickCount64();
        if(!result)
        {
            BYTE *base=owned?NULL:(BYTE*)GetModuleHandleA(o->module);
            BYTE *entry=owned?(BYTE*)(void*)GetProcAddress(GetModuleHandleA(NULL),o->owned_export):base?base+o->rva:NULL;
            ULONGLONG limit=owned?3000:600000;r=10;
            while(entry && GetTickCount64()-start<limit)
            {
                MEMORY_BASIC_INFORMATION m;
                if(VirtualQuery(entry,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
                   (m.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
                {
                    r=split_prepare_profile(&optional_inline_detours[i],entry,&o->runtime);
                    if(r!=10)break;
                }
                Sleep(10);
            }
            if(!entry)r=60; /* module not loaded */
            unwind=r?-1:split_unwind_check(&optional_inline_detours[i]);if(!r)r=unwind;
            if(!r){r=split_install(&optional_inline_detours[i],0);if(!r&&optional_inline_detours[i].state!=1)r=50;}
            snprintf(text,sizeof(text),"catalog-home optional-inline site=%u module=%s rva=%lx result=%d unwind=%d state=%ld pushes=%lu span=%lu match=%lu waited_ms=%llu\n",
                i,o->module?o->module:"game.exe",o->rva,r,unwind,optional_inline_detours[i].state,o->runtime.count,o->runtime.span,o->runtime.match_length,
                (unsigned long long)(GetTickCount64()-start));note(log,text);
        }
        InterlockedExchange(&optional_inline_state[i],r==0?1:(r<0?r:-r));
        if(optional_inline_detours[i].state!=1)split_discard(&optional_inline_detours[i]);
    }
    snprintf(text,sizeof(text),"catalog-home optional complete requested=%u\n",(unsigned)(HOME_OPTIONAL_COUNT+OPTIONAL_INLINE_COUNT));note(log,text);
    if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
    for(unsigned i=0;i<CATALOG_COUNT;i++)if(!detours[i].state)split_discard(&detours[i]);
    for(unsigned i=0;i<NTDLL_PADDED_COUNT;i++)if(!padded[i].state)split_discard(&padded[i]);
    if(!home.state)split_discard(&home);
    return 0;
}
void MissionEmptyGuardStart(void);
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *reserved)
{
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);HANDLE t=CreateThread(NULL,0,worker,NULL,0,NULL);if(!t)return FALSE;CloseHandle(t);
        /* The mission-empty guard lives in this same DLL: the v3 frozen runner stages
         * one support file only, so the former second-slot bridge is gone. It stays
         * dormant unless MSFS24_MISSION_EMPTY_GUARD=1. */
        MissionEmptyGuardStart();
    }
    return TRUE;
}
