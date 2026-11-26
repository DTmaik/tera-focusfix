// devtime.exe - times IDirectInput8::CreateDevice per attached device (system dinput8).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dinput8.lib")
static IDirectInput8W *di;
static BOOL CALLBACK cb(LPCDIDEVICEINSTANCEW i, LPVOID p) {
    LARGE_INTEGER f, a, b; IDirectInputDevice8W *d = 0; HRESULT hr;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    hr = di->lpVtbl->CreateDevice(di, &i->guidInstance, &d, 0);
    QueryPerformanceCounter(&b);
    if (d) d->lpVtbl->Release(d);
    wprintf(L"%8.1f ms  type=0x%02x  %-40s  {%08lx}  hr=0x%08lx\n", (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart, GET_DIDEVICE_TYPE(i->dwDevType), i->tszProductName, i->guidInstance.Data1, hr);
    return DIENUM_CONTINUE;
}
int main(void) {
    DirectInput8Create(GetModuleHandleW(0), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&di, 0);
    di->lpVtbl->EnumDevices(di, DI8DEVCLASS_ALL, cb, 0, DIEDFL_ATTACHEDONLY);
    return 0;
}
