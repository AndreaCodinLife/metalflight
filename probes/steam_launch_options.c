/* Owned fixture: command-line compatibility, without Steam or account data. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static unsigned count_option(const WCHAR *command)
{
    const WCHAR *option = L"--in-process-gpu", *p = command;
    unsigned count = 0;
    while ((p = wcsstr(p, option))) {
        WCHAR before = p == command ? L' ' : p[-1], after = p[wcslen(option)];
        if ((before == L' ' || before == L'\t' || before == L'"') &&
            (!after || after == L' ' || after == L'\t' || after == L'"')) count++;
        p++;
    }
    return count;
}

int wmain(int argc, WCHAR **argv)
{
    BOOL expected = FALSE, unicode = FALSE, spawn = FALSE;
    for (int i=1; i<argc; ++i) {
        if (!wcscmp(argv[i], L"--expect-on")) expected = TRUE;
        if (!wcscmp(argv[i], L"--unicode") && i+1<argc)
            unicode = !wcscmp(argv[i+1], L"caf\u00e9-\u65e5\u672c");
        if (!wcscmp(argv[i], L"--spawn")) spawn = TRUE;
    }
    if (spawn) {
        WCHAR path[32768], command[32768];
        GetModuleFileNameW(NULL,path,32768);
        WCHAR *leaf=wcsrchr(path,L'\\'); if(!leaf) return 10;
        wcscpy(leaf+1,L"steamwebhelper.exe");
        _snwprintf(command,32768,L"\"%ls\" --expect-on --unicode \"caf\u00e9-\u65e5\u672c\"",path);
        STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);
        if (!CreateProcessW(path,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&child)) return 11;
        CloseHandle(child.hThread);
        WaitForSingleObject(child.hProcess,15000);
        DWORD code=12;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);
        printf("OwnedSteamOptions child_exit=%lu\n",code);return code;
    }
    unsigned wide=count_option(GetCommandLineW());
    WCHAR converted[32768];
    int converted_size=MultiByteToWideChar(CP_ACP,0,GetCommandLineA(),-1,converted,32768);
    unsigned ansi=converted_size ? count_option(converted) : 99;
    BOOL passed=wide==(unsigned)expected && ansi==(unsigned)expected && unicode;
    printf("OwnedSteamOptions wide_count=%u ansi_count=%u unicode_preserved=%u pass=%u\n",wide,ansi,unicode,passed);
    return passed ? 0 : 1;
}
