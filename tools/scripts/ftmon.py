# ftmon.py <seconds> [threshold_ms]: log TERA frame-time spikes from RTSS (works in background).
import ctypes, ctypes.wintypes as wt, struct, sys, time
u = ctypes.WinDLL('user32'); k = ctypes.WinDLL('kernel32')
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
u.FindWindowW.restype = wt.HWND
dur = float(sys.argv[1]); thr = float(sys.argv[2]) if len(sys.argv) > 2 else 40
tera = u.FindWindowW('LaunchUnrealUWindowsClient', 'TERA')
pid = wt.DWORD(); u.GetWindowThreadProcessId(tera, ctypes.byref(pid)); pid = pid.value
h = k.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2'); p = k.MapViewOfFile(h, 4, 0, 0, 0)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', ctypes.string_at(p, 20))
idx = next(i for i in range(arrsz) if struct.unpack('<I', ctypes.string_at(p + arroff + i * entsz, 4))[0] == pid)
start = time.time(); end = start + dur; lastft = None; spikes = []
k.timeBeginPeriod = ctypes.WinDLL('winmm').timeBeginPeriod; k.timeBeginPeriod(1)
while time.time() < end:
    ft = struct.unpack_from('<I', ctypes.string_at(p + arroff + idx * entsz + 264 + 16, 4))[0] / 1000.0
    if ft != lastft:
        if ft >= thr: spikes.append((time.time() - start, ft)); print(f'{time.time()-start:7.2f}s  frame {ft:7.1f} ms', flush=True)
        lastft = ft
    time.sleep(0.0005)
print(f'== {len(spikes)} frames >= {thr} ms in {dur:.0f}s')
