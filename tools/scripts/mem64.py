# mem64.py - read-only helpers for a 64-bit TERA process.
# The process is picked by exe name (tera*.exe) plus TERA_PATH_FILTER (substring of its path).
import ctypes, ctypes.wintypes as wt, struct, sys
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wt.HANDLE
k.CreateToolhelp32Snapshot.restype = wt.HANDLE
k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
class PE32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD), ('th32DefaultHeapID', ctypes.c_void_p),
                ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD), ('th32ParentProcessID', wt.DWORD), ('pcPriClassBase', ctypes.c_long),
                ('dwFlags', wt.DWORD), ('szExeFile', ctypes.c_wchar * 260)]
class ME32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD), ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD), ('ProccntUsage', wt.DWORD),
                ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE), ('szModule', ctypes.c_wchar * 256), ('szExePath', ctypes.c_wchar * 260)]
def modules(pid):
    s = k.CreateToolhelp32Snapshot(0x18, pid); m = ME32(); m.dwSize = ctypes.sizeof(ME32); out = []
    ok = k.Module32FirstW(wt.HANDLE(s), ctypes.byref(m))
    while ok:
        out.append((m.szModule, m.modBaseAddr or 0, m.modBaseSize, m.szExePath)); ok = k.Module32NextW(wt.HANDLE(s), ctypes.byref(m))
    k.CloseHandle(wt.HANDLE(s)); return out
import os
PATH_FILTER = os.environ.get('TERA_PATH_FILTER', 'tera').lower()
def find_pid():
    s = k.CreateToolhelp32Snapshot(2, 0); p = PE32(); p.dwSize = ctypes.sizeof(PE32); ok = k.Process32FirstW(wt.HANDLE(s), ctypes.byref(p)); res = []
    while ok:
        if p.szExeFile.lower().startswith('tera') and p.szExeFile.lower().endswith('.exe'): res.append(p.th32ProcessID)
        ok = k.Process32NextW(wt.HANDLE(s), ctypes.byref(p))
    for pid in res:
        mods = modules(pid)
        if mods and PATH_FILTER in mods[0][3].lower(): return pid, mods
    return None, None
PID, MODS = find_pid()
H = k.OpenProcess(0x0410, False, PID) if PID else None
BASE = MODS[0][1] if MODS else 0
def rd(a, n):
    b = ctypes.create_string_buffer(n); g = ctypes.c_size_t()
    k.ReadProcessMemory(H, ctypes.c_void_p(a), b, n, ctypes.byref(g)); return b.raw[:g.value]
def u64(a): b = rd(a, 8); return struct.unpack('<Q', b)[0] if len(b) == 8 else None
