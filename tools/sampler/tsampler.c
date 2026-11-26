// tsampler - samples TERA thread stacks right after the window regains focus.
// Build x86:  cl /O2 tsampler.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define MAXMODS 600
#define MAXTHR 512
#define MAXFR 32
#define TICKMS 4

typedef struct { DWORD va, size; BYTE *data; } Sec;
typedef struct { DWORD base, end; char name[64]; Sec sec[24]; int nsec; } Mod;
static Mod mods[MAXMODS];
static int nmods;
static HANDLE hp;
static DWORD pid, mainTid;
static HWND hwnd;
static LARGE_INTEGER qpf;
static volatile LONG stopAll;

static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / qpf.QuadPart; }

static BOOL rpm(DWORD a, void *b, SIZE_T n) { SIZE_T g = 0; return ReadProcessMemory(hp, (LPCVOID)(UINT_PTR)a, b, n, &g) && g == n; }

static void load_mods(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me; me.dwSize = sizeof me;
    if (!Module32First(s, &me)) { CloseHandle(s); return; }
    do {
        Mod *m; IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS32 nt; IMAGE_SECTION_HEADER sh[24]; int i, ns;
        if (nmods >= MAXMODS) break;
        m = &mods[nmods];
        memset(m, 0, sizeof *m);
        m->base = (DWORD)(UINT_PTR)me.modBaseAddr; m->end = m->base + me.modBaseSize;
        strncpy(m->name, me.szModule, 63);
        if (!rpm(m->base, &dos, sizeof dos) || !rpm(m->base + dos.e_lfanew, &nt, sizeof nt)) { nmods++; continue; }
        ns = nt.FileHeader.NumberOfSections; if (ns > 24) ns = 24;
        if (!rpm(m->base + dos.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader, sh, ns * sizeof(IMAGE_SECTION_HEADER))) { nmods++; continue; }
        for (i = 0; i < ns; i++) {
            if (sh[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                Sec *x = &m->sec[m->nsec];
                x->va = m->base + sh[i].VirtualAddress; x->size = sh[i].Misc.VirtualSize;
                x->data = (BYTE *)malloc(x->size);
                if (x->data && rpm(x->va, x->data, x->size)) m->nsec++;
                else { free(x->data); x->data = 0; }
            }
        }
        nmods++;
    } while (Module32Next(s, &me));
    CloseHandle(s);
}

static Mod *find_mod(DWORD a) { int i; for (i = 0; i < nmods; i++) if (a >= mods[i].base && a < mods[i].end) return &mods[i]; return 0; }

static int is_ret(DWORD a) {
    Mod *m; int i; const BYTE *p;
    if (a < 0x10000) return 0;
    m = find_mod(a); if (!m) return 0;
    for (i = 0; i < m->nsec; i++) {
        Sec *x = &m->sec[i];
        if (a >= x->va + 8 && a < x->va + x->size) {
            p = x->data + (a - x->va); // p points at return address; inspect bytes before
            if (p[-5] == 0xE8) return 1;                                   // call rel32
            if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) return 1;         // call reg
            if (p[-2] == 0xFF && (p[-1] & 0xF8) == 0x10 && (p[-1] & 7) != 4 && (p[-1] & 7) != 5) return 1; // call [reg]
            if (p[-3] == 0xFF && (p[-2] & 0xF8) == 0x50 && (p[-2] & 7) != 4) return 1; // call [reg+d8]
            if (p[-3] == 0xFF && p[-2] == 0x14) return 1;                  // call [sib]
            if (p[-4] == 0xFF && p[-3] == 0x54) return 1;                  // call [sib+d8]
            if (p[-6] == 0xFF && p[-5] == 0x15) return 1;                  // call [abs32]
            if (p[-6] == 0xFF && (p[-5] & 0xF8) == 0x90 && (p[-5] & 7) != 4) return 1; // call [reg+d32]
            if (p[-7] == 0xFF && p[-6] == 0x94) return 1;                  // call [sib+d32]
            return 0;
        }
    }
    return 0;
}

typedef struct { DWORD fr[MAXFR]; BYTE n; } Stack;

static int capture(HANDLE ht, Stack *st, BYTE *buf, DWORD maxbytes) {
    CONTEXT c; DWORD esp, len = maxbytes, i; SIZE_T got = 0; MEMORY_BASIC_INFORMATION mbi;
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(ht) == (DWORD)-1) return 0;
    if (!GetThreadContext(ht, &c)) { ResumeThread(ht); return 0; }
    esp = c.Esp;
    if (VirtualQueryEx(hp, (LPCVOID)(UINT_PTR)esp, &mbi, sizeof mbi)) {
        DWORD rend = (DWORD)(UINT_PTR)mbi.BaseAddress + (DWORD)mbi.RegionSize;
        if (rend - esp < len) len = rend - esp;
    }
    ReadProcessMemory(hp, (LPCVOID)(UINT_PTR)esp, buf, len, &got);
    ResumeThread(ht);
    st->n = 0; st->fr[st->n++] = c.Eip;
    for (i = 0; i + 4 <= got && st->n < MAXFR; i += 4) { DWORD v = *(DWORD *)(buf + i); if (is_ret(v)) st->fr[st->n++] = v; }
    return 1;
}

static void sym(DWORD a, char *out, int n) {
    char b[sizeof(SYMBOL_INFO) + 256]; SYMBOL_INFO *si = (SYMBOL_INFO *)b; DWORD64 disp = 0; Mod *m = find_mod(a);
    si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 255;
    if (SymFromAddr(hp, a, &disp, si) && disp < 0x4000) _snprintf(out, n, "%s!%s+0x%llx", m ? m->name : "?", si->Name, disp);
    else if (m) _snprintf(out, n, "%s+0x%x", m->name, a - m->base);
    else _snprintf(out, n, "0x%08x", a);
    out[n - 1] = 0;
}

// ---------------- message latency probe ----------------
#define MAXLAT 200000
static double latT[MAXLAT], latV[MAXLAT];
static volatile LONG nlat;
static DWORD WINAPI lat_thread(LPVOID p) {
    while (!stopAll) {
        double t0 = now_ms(); DWORD_PTR r; double t1;
        SendMessageTimeoutA(hwnd, WM_NULL, 0, 0, SMTO_NORMAL, 15000, &r);
        t1 = now_ms();
        if (nlat < MAXLAT) { latT[nlat] = t0; latV[nlat] = t1 - t0; InterlockedIncrement(&nlat); }
        Sleep(10);
    }
    return 0;
}

// ---------------- auto alt-tab ----------------
static int autoN = 0, bgMs = 4000, winMs = 3000;
static HWND otherWnd;
static void bring(HWND w) {
    HWND fg = GetForegroundWindow();
    DWORD fgt = GetWindowThreadProcessId(fg, NULL), me = GetCurrentThreadId();
    MSG msg; PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
    if (fgt != me) AttachThreadInput(me, fgt, TRUE);
    if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
    SetForegroundWindow(w); BringWindowToTop(w);
    if (fgt != me) AttachThreadInput(me, fgt, FALSE);
    if (GetForegroundWindow() != w) { SwitchToThisWindow(w, TRUE); }
}
static DWORD WINAPI auto_thread(LPVOID p) {
    int i;
    for (i = 0; i < autoN && !stopAll; i++) {
        Sleep(bgMs);
        printf("[auto] -> TERA (%d/%d)\n", i + 1, autoN); fflush(stdout);
        bring(hwnd);
        Sleep(winMs + 1500);
        bring(otherWnd);
    }
    Sleep(winMs + 500);
    stopAll = 1;
    return 0;
}

// ---------------- main ----------------
typedef struct { DWORD tid; HANDLE h; DWORD start; ULONG64 cyc0, cyc1; FILETIME k0, u0, k1, u1; Stack *s; double *st; int ns; } Thr;
typedef LONG(NTAPI *NtQIT)(HANDLE, int, PVOID, ULONG, PULONG);

static BOOL CALLBACK enum_wnd(HWND w, LPARAM lp) {
    DWORD p; RECT r; GetWindowThreadProcessId(w, &p);
    if (p == pid && IsWindowVisible(w) && GetWindow(w, GW_OWNER) == NULL) {
        GetWindowRect(w, &r);
        if ((r.right - r.left) > 400) { hwnd = w; return FALSE; }
    }
    return TRUE;
}

static double ft_ms(FILETIME a, FILETIME b) { ULARGE_INTEGER x, y; x.LowPart = a.dwLowDateTime; x.HighPart = a.dwHighDateTime; y.LowPart = b.dwLowDateTime; y.HighPart = b.dwHighDateTime; return (double)(y.QuadPart - x.QuadPart) / 10000.0; }

static int stack_eq(Stack *a, Stack *b, int k) { int i, n = a->n < k ? a->n : k; if ((a->n < k ? a->n : k) != (b->n < k ? b->n : k)) return 0; for (i = 0; i < n; i++) if (a->fr[i] != b->fr[i]) return 0; return 1; }

static void print_stack(Stack *s, int k, const char *ind) {
    int i; char b[300];
    for (i = 0; i < s->n && i < k; i++) { sym(s->fr[i], b, sizeof b); printf("%s%s\n", ind, b); }
}

static FILE *rawf;
static void dump_raw(int ev, double tAct, Thr *th, int nth) {
    int i, j, f; char b[300];
    for (i = 0; i < nth; i++) {
        double cpu = ft_ms(th[i].k0, th[i].k1) + ft_ms(th[i].u0, th[i].u1);
        if (th[i].tid != mainTid && cpu < 30) continue;
        for (j = 0; j < th[i].ns; j++) {
            fprintf(rawf, "%d\t%u\t%s\t%.1f", ev, th[i].tid, th[i].tid == mainTid ? "M" : "-", th[i].st[j] - tAct);
            for (f = 0; f < th[i].s[j].n; f++) { sym(th[i].s[j].fr[f], b, sizeof b); fprintf(rawf, "\t%s", b); }
            fputc(10, rawf);
        }
    }
    fflush(rawf);
}
// ---------------- visual change probe ----------------
#define MAXVIS 200000
static double visT[MAXVIS]; static volatile LONG nvis;
static DWORD WINAPI vis_thread(LPVOID p) {
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr); RECT r; int w = 320, h = 240;
    BITMAPINFO bi; void *bits; HBITMAP bm; unsigned prev = 0;
    memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bm = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, 0, 0); SelectObject(mem, bm);
    while (!stopAll) {
        unsigned hsh = 2166136261u; int i;
        GetWindowRect(hwnd, &r);
        BitBlt(mem, 0, 0, w, h, scr, (r.left + r.right) / 2 - w / 2, (r.top + r.bottom) / 2 - h / 2, SRCCOPY);
        GdiFlush();
        for (i = 0; i < w * h; i += 7) hsh = (hsh ^ ((unsigned *)bits)[i]) * 16777619u;
        if (hsh != prev && nvis < MAXVIS) { visT[nvis] = now_ms(); InterlockedIncrement(&nvis); prev = hsh; }
        Sleep(2);
    }
    return 0;
}
static void report(int ev, double tAct, Thr *th, int nth) {
    int i, j;
    dump_raw(ev, tAct, th, nth);
    {
        double last = tAct - 500, worst = 0, worstAt = 0; LONG n = nvis; int cnt = 0;
        puts("-- visible center-region changes: gaps > 60ms --");
        for (i = 0; i < n; i++) {
            if (visT[i] < tAct - 500 || visT[i] > tAct + winMs) continue;
            cnt++;
            if (visT[i] - last > 60) printf("   no change from %+7.0f to %+7.0f ms (%.0f ms)%c", last - tAct, visT[i] - tAct, visT[i] - last, 10);
            if (visT[i] - last > worst) { worst = visT[i] - last; worstAt = last - tAct; }
            last = visT[i];
        }
        printf("   %d changes, longest gap %.0f ms starting %+.0f%c", cnt, worst, worstAt, 10);
    }
    printf("\n================ EVENT %d (activation at t=%.0f) ================\n", ev, tAct);
    // latency timeline
    printf("-- WM_NULL round trip (main thread message pump) --\n");
    {
        double worst = 0, worstAt = 0; LONG n = nlat;
        for (i = 0; i < n; i++) {
            if (latT[i] >= tAct - 200 && latT[i] <= tAct + winMs) {
                if (latV[i] > worst) { worst = latV[i]; worstAt = latT[i] - tAct; }
                if (latV[i] > 40) printf("   sent at %+7.0f ms  -> answered after %7.1f ms\n", latT[i] - tAct, latV[i]);
            }
        }
        printf("   worst: %.1f ms (sent at %+.0f)\n", worst, worstAt);
    }
    // main thread timeline segments
    for (i = 0; i < nth; i++) if (th[i].tid == mainTid) break;
    if (i < nth) {
        Thr *t = &th[i]; int a = 0;
        printf("-- MAIN thread %u: CPU %.0f ms (k %.0f) during window; segments >= 24ms --\n", t->tid, ft_ms(t->k0, t->k1) + ft_ms(t->u0, t->u1), ft_ms(t->k0, t->k1));
        while (a < t->ns) {
            int b = a + 1;
            while (b < t->ns && stack_eq(&t->s[a], &t->s[b], 10)) b++;
            if (t->st[b - 1] - t->st[a] >= 24) {
                printf("  [%+6.0f .. %+6.0f ms] %d samples\n", t->st[a] - tAct, t->st[b - 1] - tAct, b - a);
                print_stack(&t->s[a], 22, "      ");
            }
            a = b;
        }
    }
    // other busy threads
    printf("-- other threads by CPU during window --\n");
    {
        int order[MAXTHR], k; for (k = 0; k < nth; k++) order[k] = k;
        for (k = 0; k < nth; k++) for (j = k + 1; j < nth; j++) {
            double ck = ft_ms(th[order[k]].k0, th[order[k]].k1) + ft_ms(th[order[k]].u0, th[order[k]].u1);
            double cj = ft_ms(th[order[j]].k0, th[order[j]].k1) + ft_ms(th[order[j]].u0, th[order[j]].u1);
            if (cj > ck) { int x = order[k]; order[k] = order[j]; order[j] = x; }
        }
        for (k = 0; k < nth && k < 10; k++) {
            Thr *t = &th[order[k]]; char sb[300];
            double cpu = ft_ms(t->k0, t->k1) + ft_ms(t->u0, t->u1);
            if (t->tid == mainTid) continue;
            if (cpu < 30) break;
            sym(t->start, sb, sizeof sb);
            printf("  thread %u start=%s CPU %.0f ms (k %.0f), %d samples\n", t->tid, sb, cpu, ft_ms(t->k0, t->k1), t->ns);
            // top 3 distinct stacks by count (k=6)
            {
                int used[4096] = {0}, top, rep;
                for (rep = 0; rep < 3; rep++) {
                    int best = -1, bestc = 0, a2, b2;
                    for (a2 = 0; a2 < t->ns && a2 < 4096; a2++) {
                        int c = 0; if (used[a2]) continue;
                        for (b2 = a2; b2 < t->ns && b2 < 4096; b2++) if (!used[b2] && stack_eq(&t->s[a2], &t->s[b2], 6)) c++;
                        if (c > bestc) { bestc = c; best = a2; }
                    }
                    if (best < 0) break;
                    for (b2 = best; b2 < t->ns && b2 < 4096; b2++) if (stack_eq(&t->s[best], &t->s[b2], 6)) used[b2] = 1;
                    printf("     %d x:\n", bestc);
                    print_stack(&t->s[best], 14, "        ");
                }
                (void)top;
            }
        }
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    int i, ev = 0, prevFg = 0; double tEnd; HANDLE s; PROCESSENTRY32 pe; NtQIT ntqit; BYTE *buf; double durS = 60;
    QueryPerformanceFrequency(&qpf);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-auto") && i + 1 < argc) autoN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-bg") && i + 1 < argc) bgMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-win") && i + 1 < argc) winMs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dur") && i + 1 < argc) durS = atof(argv[++i]);
    }
    s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); pe.dwSize = sizeof pe;
    if (Process32First(s, &pe)) do { if (!_stricmp(pe.szExeFile, "Tera.exe")) { pid = pe.th32ProcessID; break; } } while (Process32Next(s, &pe));
    CloseHandle(s);
    if (!pid) { printf("TERA not running\n"); return 1; }
    EnumWindows(enum_wnd, 0);
    if (!hwnd) { printf("no TERA window\n"); return 1; }
    mainTid = GetWindowThreadProcessId(hwnd, NULL);
    { char cls[128], title[128]; RECT r; GetClassNameA(hwnd, cls, 128); GetWindowTextA(hwnd, title, 128); GetWindowRect(hwnd, &r);
      printf("pid %u hwnd %p class '%s' title '%s' rect %d,%d %dx%d style %08x mainTid %u\n", pid, hwnd, cls, title, r.left, r.top, r.right - r.left, r.bottom - r.top, (unsigned)GetWindowLongA(hwnd, GWL_STYLE), mainTid); }
    hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hp) { printf("OpenProcess failed %u\n", GetLastError()); return 1; }
    load_mods();
    printf("%d modules\n", nmods);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(hp, NULL, TRUE);
    ntqit = (NtQIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    buf = (BYTE *)malloc(65536);
    otherWnd = GetForegroundWindow();
    if (otherWnd == hwnd) otherWnd = GetShellWindow();
    rawf = fopen("raw.tsv", "w");
    CreateThread(0, 0, lat_thread, 0, 0, 0);
    CreateThread(0, 0, vis_thread, 0, 0, 0);
    if (autoN) { CreateThread(0, 0, auto_thread, 0, 0, 0); durS = 1e9; }
    tEnd = now_ms() + durS * 1000;
    prevFg = (GetForegroundWindow() == hwnd);
    printf("ready. TERA foreground now: %d\n", prevFg); fflush(stdout);

    while (!stopAll && now_ms() < tEnd) {
        int fg = (GetForegroundWindow() == hwnd);
        if (fg && !prevFg) {
            // activation -> capture window
            double tAct = now_ms(), tStop = tAct + winMs, next = tAct;
            Thr *th = (Thr *)calloc(MAXTHR, sizeof(Thr)); int nth = 0, maxs = winMs / TICKMS + 8, tick = 0;
            THREADENTRY32 te; te.dwSize = sizeof te;
            s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (Thread32First(s, &te)) do {
                if (te.th32OwnerProcessID == pid && nth < MAXTHR) {
                    Thr *t = &th[nth]; FILETIME c, e;
                    t->tid = te.th32ThreadID;
                    t->h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t->tid);
                    if (!t->h) continue;
                    if (ntqit) ntqit(t->h, 9, &t->start, sizeof(DWORD), NULL);
                    GetThreadTimes(t->h, &c, &e, &t->k0, &t->u0);
                    QueryThreadCycleTime(t->h, &t->cyc0);
                    t->s = (Stack *)malloc(maxs * sizeof(Stack)); t->st = (double *)malloc(maxs * sizeof(double));
                    nth++;
                }
            } while (Thread32Next(s, &te));
            CloseHandle(s);
            while (now_ms() < tStop) {
                int k;
                for (k = 0; k < nth; k++) {
                    Thr *t = &th[k]; ULONG64 cyc;
                    int isMain = t->tid == mainTid;
                    QueryThreadCycleTime(t->h, &cyc);
                    if (!isMain && cyc == t->cyc1 && (tick % 25) != 0) continue; // idle thread: sample rarely
                    t->cyc1 = cyc;
                    if (t->ns < maxs && capture(t->h, &t->s[t->ns], buf, isMain ? 65536 : 16384)) { t->st[t->ns] = now_ms(); t->ns++; }
                }
                tick++;
                next += TICKMS;
                while (now_ms() < next) { if (next - now_ms() > 1.5) Sleep(1); }
            }
            for (i = 0; i < nth; i++) { FILETIME c, e; GetThreadTimes(th[i].h, &c, &e, &th[i].k1, &th[i].u1); }
            Sleep(300); // let latency probe finish
            report(++ev, tAct, th, nth);
            for (i = 0; i < nth; i++) { CloseHandle(th[i].h); free(th[i].s); free(th[i].st); }
            free(th);
            fg = (GetForegroundWindow() == hwnd);
        }
        prevFg = fg;
        Sleep(1);
    }
    stopAll = 1;
    printf("done, %d events\n", ev);
    return 0;
}
