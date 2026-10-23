/* Qualify the catalog18 helper (22 sites + optional home site) using authored bodies, never the game.
 * argv[1]: "stacked" | "guard-negative" (no helper) | "guard-positive" (helper). */
#include "split_push_detour.h"
#include "split_push_catalog.h"
#include "ntdll_padded_sites.h"
#include <stdio.h>
#include <string.h>
extern int catalog_stack_call(void *stack,void *target);
extern int home_stack_call(void *stack,void *target,ULONGLONG seed);
extern int owned_home_target(void);
extern int owned_home_target2(void);
extern int home2_stack_call(void *stack,void *target,ULONGLONG seed);
extern int owned_kb_function(void);
extern BYTE owned_kb_target[];
extern int owned_game30_target(void);
extern int owned_mi_function(void);
extern BYTE owned_mi_target[];
extern int owned_gi_function(void);
extern BYTE owned_gi_target[];
extern int owned_ri_function(void);
extern BYTE owned_ri_target[];
extern int kb_stack_call(void *stack,void *target);
volatile ULONGLONG home_flags;
static volatile LONG stopped,bad;
static void *selected;
static BYTE *guard;static SIZE_T page_size;static volatile LONG guards,unexpected;
static LONG CALLBACK guard_handler(EXCEPTION_POINTERS *e)
{
    if(e->ExceptionRecord->ExceptionCode==STATUS_GUARD_PAGE_VIOLATION && e->ExceptionRecord->NumberParameters>=2 &&
       e->ExceptionRecord->ExceptionInformation[1]>=(ULONG_PTR)guard && e->ExceptionRecord->ExceptionInformation[1]<(ULONG_PTR)guard+page_size)
    {InterlockedIncrement(&guards);return EXCEPTION_CONTINUE_EXECUTION;}
    InterlockedIncrement(&unexpected);return EXCEPTION_CONTINUE_SEARCH;
}
static DWORD WINAPI hot(void *unused)
{
    (void)unused;
    typedef int (*call_t)(ULONGLONG,ULONGLONG,ULONGLONG,ULONGLONG);
    while(!InterlockedCompareExchange(&stopped,0,0)){
        if(((int(*)(void))selected)()!=42)InterlockedIncrement(&bad);
        if(((call_t)(void *)owned_home_target)(1,2,3,4)!=42)InterlockedIncrement(&bad);
        if(((call_t)(void *)owned_home_target2)(1,2,3,4)!=42)InterlockedIncrement(&bad);
        if(owned_kb_function()!=42)InterlockedIncrement(&bad);
        if(owned_game30_target()!=42)InterlockedIncrement(&bad);
        if(owned_mi_function()!=42)InterlockedIncrement(&bad);
        if(owned_gi_function()!=42)InterlockedIncrement(&bad);
        if(owned_ri_function()!=42)InterlockedIncrement(&bad);
    }
    return 0;
}
static int guard_cases(int expect_helper,void *target,unsigned boundaries,int inline_site)
{
    SYSTEM_INFO system;GetSystemInfo(&system);page_size=system.dwPageSize;
    BYTE *region=VirtualAlloc(NULL,4*page_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!region)return 2;guard=region+page_size;
    if(!AddVectoredExceptionHandler(1,guard_handler))return 2;ULONG guarantee=32768;SetThreadStackGuarantee(&guarantee);
    if(expect_helper){
        HMODULE helper=LoadLibraryA("split_push_helper.dll");
        LONG (WINAPI *optional)(unsigned)=helper?(void*)GetProcAddress(helper,inline_site?"OptionalInlineState":"OptionalState"):NULL;if(!optional)return 3;
        ULONGLONG end=GetTickCount64()+20000;LONG o=0;while(!(o=optional(inline_site==5?4:inline_site==4?3:inline_site==3?2:inline_site==2?1:0))&&GetTickCount64()<end)Sleep(10);
        BYTE *patched=inline_site==1?owned_kb_target:inline_site==3?owned_mi_target:inline_site==4?owned_gi_target:inline_site==5?owned_ri_target:(BYTE*)target;
        printf("GUARD helper optional_state=%ld entry_byte=%02x\n",o,patched[0]);if(o!=1)return 3;
    }
    unsigned failures=0;
    for(unsigned boundary=1;boundary<=boundaries;boundary++){
        guards=0;DWORD old;if(!VirtualProtect(guard,page_size,PAGE_READWRITE|PAGE_GUARD,&old))return 2;
        if(!expect_helper)printf("GUARD negative unmodified home shape boundary=%u start\n",boundary);
        int value=catalog_stack_call(guard+page_size+8*boundary,target);
        MEMORY_BASIC_INFORMATION m;VirtualQuery(guard,&m,sizeof(m));
        int pass=value==42&&guards==1&&!unexpected&&!(m.Protect&PAGE_GUARD);failures+=!pass;
        printf("GUARD boundary=%u value=%d guards=%ld pass=%d\n",boundary,value,guards,pass);
    }
    printf("GUARD summary helper=%d boundaries=%u failures=%u unexpected=%ld\n",expect_helper,boundaries,failures,unexpected);
    return failures?1:0;
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc>1&&!strcmp(argv[1],"guard-negative"))return guard_cases(0,(void*)owned_home_target2,5,0);
    if(argc>1&&!strcmp(argv[1],"guard-positive"))return guard_cases(1,(void*)owned_home_target2,5,0);
    if(argc>1&&!strcmp(argv[1],"guard-kb-negative"))return guard_cases(0,(void*)owned_kb_function,8,1);
    if(argc>1&&!strcmp(argv[1],"guard-kb-positive"))return guard_cases(1,(void*)owned_kb_function,8,1);
    if(argc>1&&!strcmp(argv[1],"guard-new-negative"))return guard_cases(0,(void*)owned_game30_target,3,2);
    if(argc>1&&!strcmp(argv[1],"guard-new-positive"))return guard_cases(1,(void*)owned_game30_target,3,2);
    if(argc>1&&!strcmp(argv[1],"guard-mi-negative"))return guard_cases(0,(void*)owned_mi_function,7,3);
    if(argc>1&&!strcmp(argv[1],"guard-mi-positive"))return guard_cases(1,(void*)owned_mi_function,7,3);
    if(argc>1&&!strcmp(argv[1],"guard-ri-negative"))return guard_cases(0,(void*)owned_ri_function,4,5);
    if(argc>1&&!strcmp(argv[1],"guard-ri-positive"))return guard_cases(1,(void*)owned_ri_function,4,5);
    if(argc>1&&!strcmp(argv[1],"ri-reference")) {
        BYTE *arena=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
        unsigned wrong=0;for(unsigned i=0;i<200;i++)wrong+=kb_stack_call(arena+32768,(void*)owned_ri_function)!=42;
        printf("RIREF calls=200 wrong=%u run_byte=%02x\n",wrong,owned_ri_target[0]);return wrong!=0;
    }
    if(argc>1&&!strcmp(argv[1],"guard-gi-negative"))return guard_cases(0,(void*)owned_gi_function,7,4);
    if(argc>1&&!strcmp(argv[1],"guard-gi-positive"))return guard_cases(1,(void*)owned_gi_function,7,4);
    if(argc>1&&!strcmp(argv[1],"gi-reference")) {
        BYTE *arena=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
        unsigned wrong=0;for(unsigned i=0;i<200;i++)wrong+=kb_stack_call(arena+32768,(void*)owned_gi_function)!=42;
        printf("GIREF calls=200 wrong=%u run_byte=%02x\n",wrong,owned_gi_target[0]);return wrong!=0;
    }
    if(argc>1&&!strcmp(argv[1],"mi-reference")) {
        BYTE *arena=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
        unsigned wrong=0;for(unsigned i=0;i<200;i++)wrong+=kb_stack_call(arena+32768,(void*)owned_mi_function)!=42;
        printf("MIREF calls=200 wrong=%u run_byte=%02x\n",wrong,owned_mi_target[0]);return wrong!=0;
    }
    if(argc>1&&!strcmp(argv[1],"new-reference")) {
        BYTE *arena=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
        unsigned wrong=0;for(unsigned i=0;i<200;i++)wrong+=kb_stack_call(arena+32768,(void*)owned_game30_target)!=42;
        printf("NEWREF calls=200 wrong=%u entry=%02x\n",wrong,((BYTE*)(void*)owned_game30_target)[0]);return wrong!=0;
    }
    int expect_ri_skip=argc>1&&!strcmp(argv[1],"stacked-ri-mismatch");
    int expect_gi_skip=argc>1&&!strcmp(argv[1],"stacked-gi-mismatch");
    int expect_mi_skip=argc>1&&!strcmp(argv[1],"stacked-mi-mismatch");
    int expect_new_skip=argc>1&&!strcmp(argv[1],"stacked-new-mismatch");
    int expect_skip=argc>1&&!strcmp(argv[1],"stacked-mismatch");
    int expect_kb_skip=argc>1&&!strcmp(argv[1],"stacked-kb-mismatch");
    BYTE *arena=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!arena)return 2;
    void *targets[CATALOG_COUNT];
    for(unsigned i=0;i<CATALOG_COUNT;i++){
        targets[i]=(void*)GetProcAddress(GetModuleHandleA(NULL),catalog[i].owned_export);
        if(!targets[i])return 2;
    }
    selected=targets[0];HANDLE threads[8];
    for(unsigned i=0;i<8;i++){threads[i]=CreateThread(NULL,0,hot,NULL,0,NULL);if(!threads[i])return 2;}
    Sleep(20);HMODULE helper=LoadLibraryA("split_push_helper.dll");
    LONG (WINAPI *status)(void)=helper?(void*)GetProcAddress(helper,"SplitStatus"):NULL;
    unsigned (WINAPI *entry_byte)(void)=helper?(void*)GetProcAddress(helper,"HomeEntryByte"):NULL;
    LONG (WINAPI *optional)(unsigned)=helper?(void*)GetProcAddress(helper,"OptionalState"):NULL;
    LONG (WINAPI *optional_inline)(unsigned)=helper?(void*)GetProcAddress(helper,"OptionalInlineState"):NULL;
    if(!status||!entry_byte||!optional||!optional_inline)return 3;
    ULONGLONG end=GetTickCount64()+15000;LONG state;
    while(!(state=status())&&GetTickCount64()<end)Sleep(10);
    LONG ostate=0;ULONGLONG oend=GetTickCount64()+15000;while(!(ostate=optional(0))&&GetTickCount64()<oend)Sleep(10);
    LONG kstate=0;while(!(kstate=optional_inline(0))&&GetTickCount64()<oend+15000)Sleep(10);
    LONG newstate=0;while(!(newstate=optional_inline(1))&&GetTickCount64()<oend+15000)Sleep(10);
    LONG mistate=0;while(!(mistate=optional_inline(2))&&GetTickCount64()<oend+15000)Sleep(10);
    LONG gistate=0;while(!(gistate=optional_inline(3))&&GetTickCount64()<oend+15000)Sleep(10);
    LONG ristate=0;while(!(ristate=optional_inline(4))&&GetTickCount64()<oend+15000)Sleep(10);
    InterlockedExchange(&stopped,1);
    if(WaitForMultipleObjects(8,threads,TRUE,5000)!=WAIT_OBJECT_0)return 3;
    for(unsigned i=0;i<8;i++)CloseHandle(threads[i]);
    BYTE obyte=((BYTE*)(void*)owned_home_target2)[0];
    printf("HOMESET state=%ld bad_workers=%ld entry_byte=%02x optional_state=%ld optional_entry_byte=%02x\n",state,bad,entry_byte(),ostate,obyte);
    if(state!=1||bad||entry_byte()!=0xe9)return 3;
    if(expect_skip?(ostate!=-10||obyte==0xe9):(ostate!=1||obyte!=0xe9))return 4;
    BYTE kbyte=owned_kb_target[0];
    printf("HOMESET optional_inline_state=%ld kb_run_byte=%02x\n",kstate,kbyte);
    if(expect_kb_skip?(kstate!=-10||kbyte!=0x41):(kstate!=1||kbyte!=0xe9))return 6;
    printf("HOMESET new_inline_state=%ld new_entry_byte=%02x\n",newstate,((BYTE*)(void*)owned_game30_target)[0]);
    if(expect_new_skip?(newstate!=-10||((BYTE*)(void*)owned_game30_target)[0]==0xe9):(newstate!=1||((BYTE*)(void*)owned_game30_target)[0]!=0xe9))return 7;
    unsigned wrong=0;
    for(unsigned i=0;i<200;i++)wrong+=kb_stack_call(arena+32768,(void*)owned_game30_target)!=42;
    printf("HOMESET new_calls=200 new_wrong=%u\n",wrong);
    printf("HOMESET mi_inline_state=%ld mi_run_byte=%02x\n",mistate,owned_mi_target[0]);
    if(expect_mi_skip?(mistate!=-10||owned_mi_target[0]!=0x55):(mistate!=1||owned_mi_target[0]!=0xe9))return 8;
    unsigned wrong4=0;
    for(unsigned i=0;i<200;i++)wrong4+=kb_stack_call(arena+32768,(void*)owned_mi_function)!=42;
    wrong+=wrong4;printf("HOMESET mi_calls=200 mi_wrong=%u\n",wrong4);
    printf("HOMESET gi_inline_state=%ld gi_run_byte=%02x\n",gistate,owned_gi_target[0]);
    if(expect_gi_skip?(gistate!=-10||owned_gi_target[0]!=0x55):(gistate!=1||owned_gi_target[0]!=0xe9))return 9;
    unsigned wrong5=0;
    for(unsigned i=0;i<200;i++)wrong5+=kb_stack_call(arena+32768,(void*)owned_gi_function)!=42;
    wrong+=wrong5;printf("HOMESET gi_calls=200 gi_wrong=%u\n",wrong5);
    printf("HOMESET ri_inline_state=%ld ri_run_byte=%02x\n",ristate,owned_ri_target[0]);
    if(expect_ri_skip?(ristate!=-10||owned_ri_target[0]!=0x55):(ristate!=1||owned_ri_target[0]!=0xe9))return 10;
    unsigned wrong6=0;
    for(unsigned i=0;i<200;i++)wrong6+=kb_stack_call(arena+32768,(void*)owned_ri_function)!=42;
    wrong+=wrong6;printf("HOMESET ri_calls=200 ri_wrong=%u\n",wrong6);
    for(unsigned i=0;i<CATALOG_COUNT;i++)wrong+=catalog_stack_call(arena+32768,targets[i])!=42;
    for(unsigned i=0;i<NTDLL_PADDED_COUNT;i++){
        void *target=(void*)GetProcAddress(GetModuleHandleA(NULL),ntdll_padded[i].owned_export);
        if(!target)return 2;wrong+=catalog_stack_call(arena+32768,target)!=42;
    }
    for(unsigned i=0;i<200;i++){
        memset(arena+32768,0,64);wrong+=home_stack_call(arena+32768,(void*)owned_home_target,0x12345678+i)!=42;
    }
    unsigned wrong2=0;
    for(unsigned i=0;i<200;i++){memset(arena+32768,0,64);wrong2+=home2_stack_call(arena+32768,(void*)owned_home_target2,0x9abcdef0+i)!=42;}
    wrong+=wrong2;
    unsigned wrong3=0;
    for(unsigned i=0;i<200;i++)wrong3+=kb_stack_call(arena+32768,(void*)owned_kb_function)!=42;
    wrong+=wrong3;printf("HOMESET kb_calls=200 kb_wrong=%u\n",wrong3);
    printf("HOMESET summary inline=%u padded=%u home_calls=200 wrong=%u optional_calls=200 optional_wrong=%u\n",(unsigned)CATALOG_COUNT,(unsigned)NTDLL_PADDED_COUNT,wrong-wrong2,wrong2);
    return wrong?1:0;
}
