# fps64.py <seconds>: bring the 64-bit test client to front, read FPS from RTSS, switch back
import ctypes, ctypes.wintypes as wt, struct, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('TERA_PATH_FILTER', 'tera')
from mem64 import PID
u = ctypes.WinDLL('user32'); k = ctypes.WinDLL('kernel32')
u.GetForegroundWindow.restype = wt.HWND
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
wins = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(h, l):
    p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
    if p.value == PID and u.IsWindowVisible(h):
        r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); wins.append(((r.right - r.left) * (r.bottom - r.top), h))
    return True
u.EnumWindows(cb, 0); hwnd = max(wins)[1]
def bring(w):
    fg = u.GetForegroundWindow(); me = k.GetCurrentThreadId(); ft = u.GetWindowThreadProcessId(fg, None)
    u.AttachThreadInput(me, ft, True); u.SetForegroundWindow(w); u.BringWindowToTop(w); u.AttachThreadInput(me, ft, False)
h = k.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2'); p = k.MapViewOfFile(h, 4, 0, 0, 0)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', ctypes.string_at(p, 20))
idx = next((i for i in range(arrsz) if struct.unpack('<I', ctypes.string_at(p + arroff + i * entsz, 4))[0] == PID), None)
if idx is None: print('RTSS has no entry for this process (overlay not hooked?)'); sys.exit(1)
def ent(): return struct.unpack_from('<5I', ctypes.string_at(p + arroff + idx * entsz, 300), 264)
other = u.GetForegroundWindow(); bring(hwnd); time.sleep(1.5)
fps = []; fts = []; last = None; end = time.time() + float(sys.argv[1])
while time.time() < end:
    f, t0, t1, fr, ft = ent()
    if (t0, t1) != last and t1 > t0: fps.append(fr * 1000.0 / (t1 - t0)); last = (t0, t1)
    fts.append(ft / 1000.0); time.sleep(0.002)
bring(other); fps = fps[1:] or fps; fts.sort()
print(f'fps avg {sum(fps)/len(fps):.1f} (min {min(fps):.1f} max {max(fps):.1f})  frametime median {fts[len(fts)//2]:.2f} ms  p99 {fts[int(len(fts)*.99)]:.2f} ms')
