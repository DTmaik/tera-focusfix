// dllmain.cpp - entry point and dinput8.dll proxy exports
//
// Two ways to load this DLL:
//   * as <TERA>\Binaries\dinput8.dll  -> "proxy mode": TERA loads it at start, every export is
//     forwarded to the real %SystemRoot%\SysWOW64\dinput8.dll
//   * injected into a running TERA under any other file name -> "injected mode" (for testing)
#define DIRECTINPUT_VERSION 0x0800
#include "common.h"
#include <dinput.h>

static HMODULE g_real;

static void load_real() {
    if (g_real) return;
    wchar_t p[MAX_PATH];
    GetSystemDirectoryW(p, MAX_PATH); // SysWOW64 for a 32-bit process
    wcscat_s(p, L"\\dinput8.dll");
    g_real = LoadLibraryW(p);
    logf("system dinput8: %s", g_real ? "loaded" : "NOT loaded");
}

static bool is_proxy() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(g_self, p, MAX_PATH);
    const wchar_t *n = wcsrchr(p, L'\\');
    return _wcsicmp(n ? n + 1 : p, L"dinput8.dll") == 0;
}

static DWORD WINAPI injected_init(LPVOID) {
    g_real = GetModuleHandleW(L"dinput8.dll");
    if (!g_real) load_real();
    dinput_cache_install(g_real);
    return 0;
}

// ---- proxy exports (names mapped in dinput8.def) --------------------------------------
extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPUNKNOWN u) {
    load_real();
    dinput_cache_install(g_real);
    typedef HRESULT(WINAPI * F)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    F f = g_real ? (F)GetProcAddress(g_real, "DirectInput8Create") : nullptr;
    return f ? f(h, v, r, o, u) : E_FAIL;
}

#define FORWARD(name, ret, params, args, fail)                                            \
    extern "C" ret WINAPI Proxy_##name params {                                           \
        load_real();                                                                      \
        typedef ret(WINAPI * F) params;                                                   \
        F f = g_real ? (F)GetProcAddress(g_real, #name) : nullptr;                        \
        return f ? f args : fail;                                                         \
    }
FORWARD(DllCanUnloadNow, HRESULT, (void), (), S_FALSE)
FORWARD(DllGetClassObject, HRESULT, (REFCLSID a, REFIID b, LPVOID *c), (a, b, c), E_FAIL)
FORWARD(DllRegisterServer, HRESULT, (void), (), E_FAIL)
FORWARD(DllUnregisterServer, HRESULT, (void), (), E_FAIL)
FORWARD(GetdfDIJoystick, LPCDIDATAFORMAT, (void), (), nullptr)

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        log_open();
#if defined(_M_X64)
        const char *arch = "64-bit";
#else
        const char *arch = "32-bit";
#endif
        logf("tera-focusfix loaded (%s, %s mode)", arch, is_proxy() ? "proxy" : "injected");
        if (!is_proxy()) CloseHandle(CreateThread(nullptr, 0, injected_init, nullptr, 0, nullptr));
        perf_start();
    }
    return TRUE;
}
