// inject.exe <full dll path> [process name]  - loads a DLL into a running TERA client for testing.
// Build it with the same bitness as the client (x86 for the 32-bit client, x64 for the 64-bit one).
// Default process names: Tera.exe, TERA.osto.exe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

static DWORD find_process(const wchar_t *name) {
    DWORD pid = 0; PROCESSENTRY32W pe; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    pe.dwSize = sizeof pe;
    if (Process32FirstW(s, &pe)) do {
        if (name ? !_wcsicmp(pe.szExeFile, name) : (!_wcsicmp(pe.szExeFile, L"Tera.exe") || !_wcsicmp(pe.szExeFile, L"TERA.osto.exe"))) { pid = pe.th32ProcessID; break; }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return pid;
}

int wmain(int argc, wchar_t **argv) {
    HANDLE hp, ht; void *mem; SIZE_T n; DWORD code = 0, pid;
    if (argc < 2) { wprintf(L"usage: inject <dll> [process name]\n"); return 1; }
    pid = find_process(argc > 2 ? argv[2] : NULL);
    if (!pid) { wprintf(L"TERA client not running\n"); return 1; }
    hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) { wprintf(L"OpenProcess failed %lu\n", GetLastError()); return 1; }
    n = (wcslen(argv[1]) + 1) * sizeof(wchar_t);
    mem = VirtualAllocEx(hp, NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(hp, mem, argv[1], n, NULL);
    ht = CreateRemoteThread(hp, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"), mem, 0, NULL);
    if (!ht) { wprintf(L"CreateRemoteThread failed %lu\n", GetLastError()); return 1; }
    WaitForSingleObject(ht, 10000);
    GetExitCodeThread(ht, &code);   // low 32 bits of the module handle; 0 = LoadLibrary failed
    wprintf(L"injected into pid %lu, LoadLibrary returned 0x%08lx\n", pid, code);
    VirtualFreeEx(hp, mem, 0, MEM_RELEASE);
    return code ? 0 : 2;
}
