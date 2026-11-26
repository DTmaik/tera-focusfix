// diag.dll - injected into TERA: counts SetEvent / WaitForSingleObject calls made through
// TERA's import slots for N seconds, grouped by the caller of TERA's thin wrapper, then
// restores the slots and writes diag.txt next to the DLL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <intrin.h>

#define SLOT_SETEVENT 0x01c42290
#define SLOT_WAIT     0x01c420d0
#define MAXC 512
typedef struct { DWORD caller; DWORD tid; LONG n; } Cnt;
static Cnt cSet[MAXC], cWait[MAXC];
static volatile LONG nSetTot, nWaitTot, armed;
static BOOL(WINAPI *oSet)(HANDLE);
static DWORD(WINAPI *oWait)(HANDLE, DWORD);
static HMODULE self;

static void bump(Cnt *t, DWORD caller) {
    DWORD tid = GetCurrentThreadId(); int i;
    for (i = 0; i < MAXC; i++) {
        if (t[i].caller == caller && t[i].tid == tid) { InterlockedIncrement(&t[i].n); return; }
        if (t[i].caller == 0) {
            if (InterlockedCompareExchange((LONG *)&t[i].caller, caller, 0) == 0) { t[i].tid = tid; InterlockedIncrement(&t[i].n); return; }
            if (t[i].caller == caller && t[i].tid == tid) { InterlockedIncrement(&t[i].n); return; }
        }
    }
}
// stack at entry: [esp]=ret into wrapper, [esp+4..] args, then wrapper's own return address
static BOOL WINAPI hkSet(HANDLE h) {
    DWORD *ap = (DWORD *)_AddressOfReturnAddress();
    if (armed) { InterlockedIncrement(&nSetTot); bump(cSet, ap[0] == 0x006a3089 ? ap[2] : ap[0]); }
    return oSet(h);
}
static DWORD WINAPI hkWait(HANDLE h, DWORD ms) {
    DWORD *ap = (DWORD *)_AddressOfReturnAddress();
    if (armed) { InterlockedIncrement(&nWaitTot); bump(cWait, ap[0] == 0x006a351f ? ap[4] : ap[0] == 0x006a353b ? ap[3] : ap[0]); }
    return oWait(h, ms);
}
static void put(DWORD slot, void *fn, void **orig) {
    DWORD pr; void **p = (void **)slot;
    VirtualProtect(p, 4, PAGE_READWRITE, &pr);
    if (orig) *orig = *p;
    *p = fn;
    VirtualProtect(p, 4, pr, &pr);
}
static int cmp(const void *a, const void *b) { return ((Cnt *)b)->n - ((Cnt *)a)->n; }
static DWORD WINAPI run(LPVOID x) {
    char path[MAX_PATH], *s; FILE *f; int i; DWORD secs = 6;
    {
        HMODULE kb = GetModuleHandleA("kernelbase.dll"), k32 = GetModuleHandleA("kernel32.dll");
        void *se = *(void **)SLOT_SETEVENT, *wt = *(void **)SLOT_WAIT;
        if ((se != GetProcAddress(kb, "SetEvent") && se != GetProcAddress(k32, "SetEvent")) ||
            (wt != GetProcAddress(kb, "WaitForSingleObject") && wt != GetProcAddress(k32, "WaitForSingleObject"))) {
            GetModuleFileNameA(self, path, MAX_PATH); s = strrchr(path, 92); strcpy(s + 1, "diag.txt");
            f = fopen(path, "w"); fputs("import slots do not match - aborted", f); fclose(f); return 0;
        }
    }
    put(SLOT_SETEVENT, hkSet, (void **)&oSet);
    put(SLOT_WAIT, hkWait, (void **)&oWait);
    Sleep(2500);        // time to bring TERA to the foreground
    armed = 1; Sleep(secs * 1000); armed = 0;
    put(SLOT_SETEVENT, oSet, NULL);
    put(SLOT_WAIT, oWait, NULL);
    Sleep(200);
    GetModuleFileNameA(self, path, MAX_PATH); s = strrchr(path, '\\'); strcpy(s + 1, "diag.txt");
    f = fopen(path, "w");
    qsort(cSet, MAXC, sizeof(Cnt), cmp); qsort(cWait, MAXC, sizeof(Cnt), cmp);
    fprintf(f, "SetEvent total %ld in %lus (%.0f/s)\n", nSetTot, secs, nSetTot / (double)secs);
    for (i = 0; i < 25 && cSet[i].n; i++) fprintf(f, "  %8ld  caller 0x%08lx  tid %lu\n", cSet[i].n, cSet[i].caller, cSet[i].tid);
    fprintf(f, "WaitForSingleObject total %ld (%.0f/s)\n", nWaitTot, nWaitTot / (double)secs);
    for (i = 0; i < 25 && cWait[i].n; i++) fprintf(f, "  %8ld  caller 0x%08lx  tid %lu\n", cWait[i].n, cWait[i].caller, cWait[i].tid);
    fclose(f);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) {
    if (r == DLL_PROCESS_ATTACH) { self = h; DisableThreadLibraryCalls(h); CloseHandle(CreateThread(0, 0, run, 0, 0, 0)); }
    return TRUE;
}
