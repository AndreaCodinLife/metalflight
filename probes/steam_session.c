// Positive Steam-session/ownership check through the user's installed SDK DLL.
// No authentication tickets, account identifiers or credentials are requested.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc!=2 && !(argc==3 && (!strcmp(argv[2],"--interface-results") || !strcmp(argv[2],"--loader-results")))) return 2;
    char directory[MAX_PATH];
    if(GetModuleFileNameA(NULL,directory,sizeof(directory))) {
        char *slash=strrchr(directory,'\\');
        if(slash) {*slash=0;SetCurrentDirectoryA(directory);}
    }
    SetEnvironmentVariableA("SteamAppId","2537590");
    SetEnvironmentVariableA("SteamGameId","2537590");
    if(argc==3 && !strcmp(argv[2],"--interface-results")) {
        /* Match a game whose executable and SDK/forwarder sidecar share a
         * directory. The owned executable itself lives in probes/. */
        char sdk_directory[MAX_PATH];
        if(strlen(argv[1])>=sizeof(sdk_directory))return 2;
        strcpy(sdk_directory,argv[1]);
        char *slash=strrchr(sdk_directory,'\\');
        if(!slash)return 2;
        *slash=0;
        if(!SetDllDirectoryA(sdk_directory))return 2;
    }
    puts("Loading installed Steam SDK.");
    HMODULE library=LoadLibraryA(argv[1]);
    if(!library) {printf("Steam SDK load error=%lu\n",GetLastError());return 1;}
    bool (*init)(void)=(void *)GetProcAddress(library,"SteamAPI_Init");
    bool (*running)(void)=(void *)GetProcAddress(library,"SteamAPI_IsSteamRunning");
    void (*shutdown)(void)=(void *)GetProcAddress(library,"SteamAPI_Shutdown");
    if(!init||!running||!shutdown) return 1;
    puts("Steam SDK loaded; checking client.");
    bool active=running();
    printf("Steam client running=%d; initializing SDK.\n",active);
    bool ready=init();
    printf("Steam client running=%d SDK initialized=%d\n",active,ready);
    if(!ready) {
        if(argc==3 && !strcmp(argv[2],"--loader-results")) {
            BOOL before=GetModuleHandleA("steamclient64.dll")!=NULL;
            HMODULE client=LoadLibraryW(L"C:\\Program Files (x86)\\Steam\\steamclient64.dll");
            DWORD error=client?0:GetLastError();
            printf("Steam loader control client_preexisting=%d full_path_load=%d error=%lu\n",before,client!=NULL,error);
            if(client)FreeLibrary(client);
            client=LoadLibraryExW(L"C:\\Program Files (x86)\\Steam\\steamclient64.dll",NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
            error=client?0:GetLastError();
            printf("Steam loader control altered_path_load=%d error=%lu\n",client!=NULL,error);
            if(client)FreeLibrary(client);
        }
        return 1; /* Diagnostic loading never changes the readiness decision. */
    }
    if(argc==3 && !strcmp(argv[2],"--interface-results")) {
        int (*get_user)(void)=(void *)GetProcAddress(library,"SteamAPI_GetHSteamUser");
        void *(*find_user)(int,const char *)=(void *)GetProcAddress(library,"SteamInternal_FindOrCreateUserInterface");
        if(!get_user||!find_user){shutdown();return 1;}
        int handle=get_user();
        void *user=find_user(handle,"SteamUser023"),*apps=find_user(handle,"STEAMAPPS_INTERFACE_VERSION008");
        printf("Steam header interfaces user=%d apps=%d handle_nonzero=%d\n",user!=NULL,apps!=NULL,handle!=0);
        if(!user||!apps||!handle){shutdown();return 1;}
    }
    void *(*user_interface)(void)=NULL;
    for(unsigned version=24;version>=20;version--) {
        char name[64];snprintf(name,sizeof(name),"SteamAPI_SteamUser_v%03u",version);
        user_interface=(void *)GetProcAddress(library,name);
        if(user_interface) break;
    }
    void *(*apps_interface)(void)=(void *)GetProcAddress(library,"SteamAPI_SteamApps_v008");
    bool (*logged)(void *)=(void *)GetProcAddress(library,"SteamAPI_ISteamUser_BLoggedOn");
    bool (*owned)(void *,unsigned)=(void *)GetProcAddress(library,"SteamAPI_ISteamApps_BIsSubscribedApp");
    void (*callbacks)(void)=(void *)GetProcAddress(library,"SteamAPI_RunCallbacks");
    if(!user_interface||!apps_interface||!logged||!owned) {shutdown();return 1;}
    void *user=user_interface(),*apps=apps_interface();
    bool signed_in=false;
    for(unsigned i=0;i<100&&user;i++) {
        if(callbacks) callbacks();
        signed_in=logged(user);
        if(signed_in) break;
        Sleep(100);
    }
    bool licensed=apps&&owned(apps,2537590);
    printf("Steam logged in=%d MSFS24 ownership recognized=%d\n",signed_in,licensed);
    shutdown();return !signed_in||!licensed;
}
