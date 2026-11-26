# Read RivaTuner Statistics Server shared memory: per-app framerate / frametime.
import ctypes, ctypes.wintypes as wt, struct, sys, time
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
h = k.OpenFileMappingW(0x0004, False, 'RTSSSharedMemoryV2')
if not h: print('RTSS shared memory not found'); sys.exit(1)
p = k.MapViewOfFile(h, 0x0004, 0, 0, 0)
def rd(off, n): return ctypes.string_at(p + off, n)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', rd(0, 20))
print(f'sig={sig:08x} ver={ver:08x} entry={entsz} arr@{arroff} n={arrsz}')
pidfilter = int(sys.argv[1]) if len(sys.argv) > 1 else 0
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 0
# APP_ENTRY: dwProcessID(4) szName[260] dwFlags dwTime0 dwTime1 dwFrames dwFrameTime(us) dwStatFlags dwStatTime0 dwStatTime1 dwStatFrames dwStatCount dwStatFramerateMin Avg Max ...
def entry(i):
    b = rd(arroff + i * entsz, 300)
    pid = struct.unpack_from('<I', b, 0)[0]
    name = b[4:264].split(b'\0')[0].decode('latin-1')
    flags, t0, t1, frames, ftime = struct.unpack_from('<5I', b, 264)
    return pid, name, t0, t1, frames, ftime
for i in range(arrsz):
    pid, name, t0, t1, fr, ft = entry(i)
    if pid and (not pidfilter or pid == pidfilter):
        fps = fr * 1000.0 / (t1 - t0) if t1 > t0 else 0
        print(f'[{i}] pid {pid} {name}  fps={fps:.1f} frametime={ft/1000:.2f}ms')
if dur and pidfilter:
    idx = [i for i in range(arrsz) if entry(i)[0] == pidfilter][0]
    fts = []; fpss = []; end = time.time() + dur; last = None
    while time.time() < end:
        pid, name, t0, t1, fr, ft = entry(idx)
        if (t0, t1) != last and t1 > t0: fpss.append(fr * 1000.0 / (t1 - t0)); last = (t0, t1)
        fts.append(ft / 1000); time.sleep(0.002)
    fts.sort()
    print(f'fps samples {len(fpss)}: avg {sum(fpss)/max(1,len(fpss)):.1f} min {min(fpss or [0]):.1f} max {max(fpss or [0]):.1f}')
    print(f'frametime median {fts[len(fts)//2]:.2f} p99 {fts[int(len(fts)*.99)]:.2f} max {fts[-1]:.2f}')
