// dinput_cache.cpp - removes the freeze when TERA regains focus
//
// About 1 s after every re-activation TERA calls IDirectInput8W::EnumDevices(DI8DEVCLASS_GAMECTRL)
// and GetDeviceStatus() for its 4 gamepad slots on the game thread. For that, DirectInput opens
// every HID device in the system; a single slow device (e.g. a keyboard's vendor/config HID
// interface) can take hundreds of milliseconds, so the whole game stalls.
//
// We patch EnumDevices / GetDeviceStatus in the vtables of the *system* dinput8.dll (so objects
// created before we were loaded are covered too), cache the device list and failed status
// queries, and drop the caches whenever Windows reports a device interface arrival/removal
// (CM_Register_Notification). Gamepad hot-plug keeps working.
#define DIRECTINPUT_VERSION 0x0800
#include "common.h"
#include <dinput.h>
#include <cfgmgr32.h>
#include <vector>

#pragma comment(lib, "dxguid.lib")

namespace {

CRITICAL_SECTION g_cs;
volatile LONG g_gen = 1;  // bumped on every device change; cache entries of older generations are stale
bool g_cacheOn = true;

// ---- device change notification -------------------------------------------------------
DWORD CALLBACK on_cm_notify(HCMNOTIFICATION, PVOID, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
        InterlockedIncrement(&g_gen);
    return ERROR_SUCCESS;
}

void register_device_notify() {
    typedef CONFIGRET(WINAPI * PFN)(PCM_NOTIFY_FILTER, PVOID, PCM_NOTIFY_CALLBACK, PHCMNOTIFICATION);
    HMODULE cm = LoadLibraryW(L"cfgmgr32.dll");
    PFN reg = cm ? (PFN)GetProcAddress(cm, "CM_Register_Notification") : nullptr;
    if (!reg) { logf("dinput: CM_Register_Notification unavailable - caches are never invalidated"); return; }
    CM_NOTIFY_FILTER f = {};
    f.cbSize = sizeof f;
    f.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
    f.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    HCMNOTIFICATION h;
    logf("dinput: device-change notification %s", reg(&f, nullptr, on_cm_notify, &h) == CR_SUCCESS ? "ok" : "FAILED");
}

// ---- EnumDevices cache ----------------------------------------------------------------
template <class INST> struct EnumCache {
    struct Entry { DWORD type, flags; LONG gen; std::vector<INST> list; };
    std::vector<Entry> entries;
    Entry *find(DWORD type, DWORD flags) {
        for (auto &e : entries) if (e.type == type && e.flags == flags) return &e;
        return nullptr;
    }
};
EnumCache<DIDEVICEINSTANCEA> g_enumA;
EnumCache<DIDEVICEINSTANCEW> g_enumW;

// Forwards every device to the caller and also records it (the full list is kept even if
// the caller stops the enumeration early).
template <class INST, class CB> struct Collector {
    std::vector<INST> list;
    CB userCb;
    LPVOID userRef;
    bool userStopped = false;
    static BOOL CALLBACK thunk(const INST *inst, LPVOID p) {
        Collector *c = (Collector *)p;
        INST copy = {};
        DWORD n = inst->dwSize && inst->dwSize < sizeof(INST) ? inst->dwSize : (DWORD)sizeof(INST);
        memcpy(&copy, inst, n);
        c->list.push_back(copy);
        if (!c->userStopped && c->userCb(inst, c->userRef) == DIENUM_STOP) c->userStopped = true;
        return DIENUM_CONTINUE;
    }
};

typedef HRESULT(STDMETHODCALLTYPE *EnumA_t)(IDirectInput8A *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *EnumW_t)(IDirectInput8W *, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *StatusA_t)(IDirectInput8A *, REFGUID);
typedef HRESULT(STDMETHODCALLTYPE *StatusW_t)(IDirectInput8W *, REFGUID);
typedef HRESULT(STDMETHODCALLTYPE *CreateA_t)(IDirectInput8A *, REFGUID, LPDIRECTINPUTDEVICE8A *, LPUNKNOWN);
typedef HRESULT(STDMETHODCALLTYPE *CreateW_t)(IDirectInput8W *, REFGUID, LPDIRECTINPUTDEVICE8W *, LPUNKNOWN);
EnumA_t oEnumA; EnumW_t oEnumW;
StatusA_t oStatusA; StatusW_t oStatusW;
CreateA_t oCreateA; CreateW_t oCreateW;

template <class SELF, class INST, class CB, class ORIG>
HRESULT enum_cached(const char *tag, EnumCache<INST> &cache, ORIG orig, SELF *self, DWORD type, CB cb, LPVOID ref, DWORD flags) {
    double t0 = now_ms();
    LONG gen = g_gen;
    if (!g_cacheOn || !cb) {
        HRESULT hr = orig(self, type, cb, ref, flags);
        logf("dinput: EnumDevices%s(type=%u flags=0x%x) passthrough %.1f ms", tag, type, flags, now_ms() - t0);
        return hr;
    }
    std::vector<INST> replay;
    bool hit = false;
    EnterCriticalSection(&g_cs);
    if (auto *e = cache.find(type, flags)) if (e->gen == gen) { replay = e->list; hit = true; }
    LeaveCriticalSection(&g_cs);
    if (hit) {
        for (auto &i : replay) if (cb(&i, ref) == DIENUM_STOP) break;
        logf("dinput: EnumDevices%s(type=%u flags=0x%x) -> %u devices from cache, %.2f ms", tag, type, flags, (unsigned)replay.size(), now_ms() - t0);
        return DI_OK;
    }
    Collector<INST, CB> col;
    col.userCb = cb;
    col.userRef = ref;
    HRESULT hr = orig(self, type, &Collector<INST, CB>::thunk, &col, flags);
    double dt = now_ms() - t0;
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&g_cs);
        auto *e = cache.find(type, flags);
        if (!e) { cache.entries.push_back({type, flags, 0, {}}); e = &cache.entries.back(); }
        e->gen = gen;
        e->list = col.list;
        LeaveCriticalSection(&g_cs);
    }
    logf("dinput: EnumDevices%s(type=%u flags=0x%x) -> %u devices, real call %.1f ms, hr=0x%08x (cached now)", tag, type, flags, (unsigned)col.list.size(), dt, (unsigned)hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE hkEnumA(IDirectInput8A *s, DWORD t, LPDIENUMDEVICESCALLBACKA cb, LPVOID r, DWORD f) { return enum_cached("A", g_enumA, oEnumA, s, t, cb, r, f); }
HRESULT STDMETHODCALLTYPE hkEnumW(IDirectInput8W *s, DWORD t, LPDIENUMDEVICESCALLBACKW cb, LPVOID r, DWORD f) { return enum_cached("W", g_enumW, oEnumW, s, t, cb, r, f); }

// ---- GetDeviceStatus: cache failures (empty gamepad slots = GUID_NULL, ~12 ms each) ----
struct StatusEntry { GUID g; HRESULT hr; LONG gen; };
std::vector<StatusEntry> g_status;

template <class SELF, class ORIG>
HRESULT status_cached(const char *tag, ORIG orig, SELF *s, REFGUID g) {
    LONG gen = g_gen;
    if (g_cacheOn) {
        EnterCriticalSection(&g_cs);
        for (auto &e : g_status)
            if (e.gen == gen && IsEqualGUID(e.g, g)) { HRESULT hr = e.hr; LeaveCriticalSection(&g_cs); return hr; }
        LeaveCriticalSection(&g_cs);
    }
    double t = now_ms();
    HRESULT hr = orig(s, g);
    double d = now_ms() - t;
    if (FAILED(hr) && g_cacheOn) {
        EnterCriticalSection(&g_cs);
        bool found = false;
        for (auto &e : g_status) if (IsEqualGUID(e.g, g)) { e.hr = hr; e.gen = gen; found = true; }
        if (!found) g_status.push_back({g, hr, gen});
        LeaveCriticalSection(&g_cs);
    }
    if (d > 2) logf("dinput: GetDeviceStatus%s {%08lx-...} real call %.1f ms hr=0x%08x%s", tag, g.Data1, d, (unsigned)hr, FAILED(hr) && g_cacheOn ? " (failure cached)" : "");
    return hr;
}
HRESULT STDMETHODCALLTYPE hkStatusA(IDirectInput8A *s, REFGUID g) { return status_cached("A", oStatusA, s, g); }
HRESULT STDMETHODCALLTYPE hkStatusW(IDirectInput8W *s, REFGUID g) { return status_cached("W", oStatusW, s, g); }

// ---- CreateDevice: passthrough, slow calls are logged (shows which device is slow) -----
template <class SELF, class DEV, class ORIG>
HRESULT create_logged(const char *tag, ORIG orig, SELF *s, REFGUID g, DEV *o, LPUNKNOWN u) {
    double t = now_ms();
    HRESULT h = orig(s, g, o, u);
    double d = now_ms() - t;
    if (d > 20) logf("dinput: CreateDevice%s {%08lx-...} %.1f ms hr=0x%08x", tag, g.Data1, d, (unsigned)h);
    return h;
}
HRESULT STDMETHODCALLTYPE hkCreateA(IDirectInput8A *s, REFGUID g, LPDIRECTINPUTDEVICE8A *o, LPUNKNOWN u) { return create_logged("A", oCreateA, s, g, o, u); }
HRESULT STDMETHODCALLTYPE hkCreateW(IDirectInput8W *s, REFGUID g, LPDIRECTINPUTDEVICE8W *o, LPUNKNOWN u) { return create_logged("W", oCreateW, s, g, o, u); }

volatile LONG g_installed;

} // namespace

void dinput_cache_install(HMODULE real) {
    if (InterlockedExchange(&g_installed, 1)) return;
    InitializeCriticalSection(&g_cs);
    g_cacheOn = !flag_file(L"focusfix.off");
    typedef HRESULT(WINAPI * Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    Create_t create = real ? (Create_t)GetProcAddress(real, "DirectInput8Create") : nullptr;
    if (!create) { logf("dinput: real DirectInput8Create not found"); return; }

    // IDirectInput8 vtable: 3 CreateDevice, 4 EnumDevices, 5 GetDeviceStatus.
    // A throw-away object of each flavour gives us the (shared, static) vtables to patch.
    IDirectInput8A *a = nullptr;
    if (SUCCEEDED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, (void **)&a, nullptr)) && a) {
        void **vt = *(void ***)a;
        oCreateA = (CreateA_t)vtable_swap_slot(vt, 3, (void *)hkCreateA);
        oEnumA = (EnumA_t)vtable_swap_slot(vt, 4, (void *)hkEnumA);
        oStatusA = (StatusA_t)vtable_swap_slot(vt, 5, (void *)hkStatusA);
        a->Release();
    }
    IDirectInput8W *w = nullptr;
    if (SUCCEEDED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void **)&w, nullptr)) && w) {
        void **vt = *(void ***)w;
        oCreateW = (CreateW_t)vtable_swap_slot(vt, 3, (void *)hkCreateW);
        oEnumW = (EnumW_t)vtable_swap_slot(vt, 4, (void *)hkEnumW);
        oStatusW = (StatusW_t)vtable_swap_slot(vt, 5, (void *)hkStatusW);
        w->Release();
    }
    logf("dinput: hooks EnumA=%s EnumW=%s StatusA=%s StatusW=%s CreateA=%s CreateW=%s, cache %s",
         oEnumA ? "ok" : "-", oEnumW ? "ok" : "-", oStatusA ? "ok" : "-", oStatusW ? "ok" : "-",
         oCreateA ? "ok" : "-", oCreateW ? "ok" : "-", g_cacheOn ? "ON" : "OFF (focusfix.off present)");
    register_device_notify();
}
