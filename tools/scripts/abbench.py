# abbench.py <toggle-file> <rounds> [phase_s]: TERA in front, alternate feature ON/OFF by
# deleting/creating <toggle-file>, measure fps (RTSS) per phase.
import ctypes, ctypes.wintypes as wt, struct, sys, time, os
u = ctypes.WinDLL('user32'); k = ctypes.WinDLL('kernel32')
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
u.FindWindowW.restype = wt.HWND; u.GetForegroundWindow.restype = wt.HWND
tog = sys.argv[1]; rounds = int(sys.argv[2]); phase = float(sys.argv[3]) if len(sys.argv) > 3 else 4
tera = u.FindWindowW('LaunchUnrealUWindowsClient', 'TERA')
pid = wt.DWORD(); u.GetWindowThreadProcessId(tera, ctypes.byref(pid)); pid = pid.value
def bring(w):
    fg = u.GetForegroundWindow(); me = k.GetCurrentThreadId(); ft = u.GetWindowThreadProcessId(fg, None)
    u.AttachThreadInput(me, ft, True); u.SetForegroundWindow(w); u.BringWindowToTop(w); u.AttachThreadInput(me, ft, False)
    if u.GetForegroundWindow() != w: u.SwitchToThisWindow(w, True)
h = k.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2'); p = k.MapViewOfFile(h, 4, 0, 0, 0)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', ctypes.string_at(p, 20))
idx = next(i for i in range(arrsz) if struct.unpack('<I', ctypes.string_at(p + arroff + i * entsz, 4))[0] == pid)
def ent(): return struct.unpack_from('<5I', ctypes.string_at(p + arroff + idx * entsz, 300), 264)
def measure(sec):
    fps = []; last = None; end = time.time() + sec
    while time.time() < end:
        f, t0, t1, fr, ft = ent()
        if (t0, t1) != last and t1 > t0: fps.append(fr * 1000.0 / (t1 - t0)); last = (t0, t1)
        time.sleep(0.005)
    fps = fps[1:] or fps
    return sum(fps) / max(1, len(fps))
def setstate(on):
    if on:
        if os.path.exists(tog): os.remove(tog)
    else: open(tog, 'w').close()
other = u.GetForegroundWindow(); bring(tera); time.sleep(1.0)
res = {True: [], False: []}
for r in range(rounds):
    for on in ((True, False) if r % 2 == 0 else (False, True)):
        setstate(on); time.sleep(1.2)
        v = measure(phase); res[on].append(v)
        print(f'round {r} {"ON " if on else "OFF"} {v:6.1f} fps', flush=True)
setstate(True); bring(other)
a = sum(res[True]) / len(res[True]); b = sum(res[False]) / len(res[False])
print(f'== ON avg {a:.1f} fps   OFF avg {b:.1f} fps   delta {a - b:+.1f} ({(a / b - 1) * 100:+.1f}%)')
