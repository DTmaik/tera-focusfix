// proxytest.exe - loads bin\dinput8.dll the way TERA does and enumerates twice.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#pragma comment(lib, "dxguid.lib")
static BOOL CALLBACK cb(LPCDIDEVICEINSTANCEW i, LPVOID p) { ++*(int *)p; return DIENUM_CONTINUE; }
int main(void) {
    typedef HRESULT(WINAPI * C)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
    HMODULE h = LoadLibraryW(L"dinput8.dll"); // resolved from the exe folder first
    wchar_t p[MAX_PATH]; GetModuleFileNameW(h, p, MAX_PATH); wprintf(L"loaded %s\n", p);
    C c = (C)GetProcAddress(h, "DirectInput8Create");
    IDirectInput8W *di; int k, n;
    if (FAILED(c(GetModuleHandleW(0), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&di, 0))) { puts("create failed"); return 1; }
    for (k = 0; k < 2; k++) {
        DWORD t = GetTickCount(); n = 0;
        di->lpVtbl->EnumDevices(di, DI8DEVCLASS_ALL, cb, &n, DIEDFL_ATTACHEDONLY);
        printf("enum %d: %d devices, %lu ms\n", k, n, GetTickCount() - t);
    }
    printf("GetDeviceStatus(GUID_NULL) = 0x%08lx\n", di->lpVtbl->GetDeviceStatus(di, &GUID_NULL));
    di->lpVtbl->Release(di);
    return 0;
}
