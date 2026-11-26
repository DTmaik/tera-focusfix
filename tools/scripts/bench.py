# bench.py <seconds> [label]: bring TERA to front, measure fps/frametime via RTSS, switch back.
import ctypes, ctypes.wintypes as wt, struct, sys, time
u = ctypes.WinDLL('user32'); k = ctypes.WinDLL('kernel32')
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
u.FindWindowW.restype = wt.HWND; u.GetForegroundWindow.restype = wt.HWND
dur = float(sys.argv[1]); label = sys.argv[2] if len(sys.argv) > 2 else ''
tera = u.FindWindowW('LaunchUnrealUWindowsClient', 'TERA')
pid = wt.DWORD(); u.GetWindowThreadProcessId(tera, ctypes.byref(pid)); pid = pid.value
def bring(w):
    fg = u.GetForegroundWindow(); me = k.GetCurrentThreadId()
    ft = u.GetWindowThreadProcessId(fg, None)
    u.AttachThreadInput(me, ft, True); u.SetForegroundWindow(w); u.BringWindowToTop(w); u.AttachThreadInput(me, ft, False)
    if u.GetForegroundWindow() != w: u.SwitchToThisWindow(w, True)
h = k.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2'); p = k.MapViewOfFile(h, 4, 0, 0, 0)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', ctypes.string_at(p, 20))
idx = next(i for i in range(arrsz) if struct.unpack('<I', ctypes.string_at(p + arroff + i * entsz, 4))[0] == pid)
def ent():
    b = ctypes.string_at(p + arroff + idx * entsz, 300)
    return struct.unpack_from('<5I', b, 264)
other = u.GetForegroundWindow()
bring(tera); time.sleep(1.5)
fps = []; fts = []; last = None; end = time.time() + dur
while time.time() < end:
    flags, t0, t1, fr, ft = ent()
    if (t0, t1) != last and t1 > t0: fps.append(fr * 1000.0 / (t1 - t0)); last = (t0, t1)
    fts.append(ft / 1000.0); time.sleep(0.001)
bring(other)
fps = fps[1:] or fps; fts.sort()
print(f'{label:12} fps avg {sum(fps)/len(fps):6.1f} (min {min(fps):.1f} max {max(fps):.1f}, n={len(fps)})  frametime median {fts[len(fts)//2]:.2f} ms  p99 {fts[int(len(fts)*.99)]:.2f} ms  max {fts[-1]:.2f} ms')
