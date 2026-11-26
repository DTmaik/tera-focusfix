# gcmon.py <seconds>: detect TERA garbage collections (GWorld+0x184 resets) and the frame
# times RTSS reports around them.
import ctypes, ctypes.wintypes as wt, struct, sys, time
sys.path.insert(0, __import__('os').path.dirname(__file__))
from memtool import rd, u32, pid
k = ctypes.WinDLL('kernel32')
k.OpenFileMappingW.restype = wt.HANDLE
k.MapViewOfFile.restype = ctypes.c_void_p
k.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
ctypes.WinDLL('winmm').timeBeginPeriod(1)
h = k.OpenFileMappingW(4, False, 'RTSSSharedMemoryV2'); p = k.MapViewOfFile(h, 4, 0, 0, 0)
sig, ver, entsz, arroff, arrsz = struct.unpack('<5I', ctypes.string_at(p, 20))
idx = next(i for i in range(arrsz) if struct.unpack('<I', ctypes.string_at(p + arroff + i * entsz, 4))[0] == pid.value)
def ft(): return struct.unpack_from('<I', ctypes.string_at(p + arroff + idx * entsz + 280, 4))[0] / 1000.0
dur = float(sys.argv[1]); start = time.time(); last = None; frames = []; lastft = None; gcs = []
interval = struct.unpack('<f', rd(u32(0x2137a7c) + 0x314, 4))[0]
print(f'GEngine TimeBetweenPurgingPendingKillObjects = {interval}')
while time.time() - start < dur:
    t = time.time() - start
    w = u32(0x2133798)
    v = struct.unpack('<f', rd(w + 0x184, 4))[0] if w else 0
    if last is not None and v < last - 1: gcs.append(t); print(f'{t:7.2f}s  GC (timer was {last:.1f}s)', flush=True)
    last = v
    f = ft()
    if f != lastft: frames.append((t, f)); lastft = f
    time.sleep(0.001)
for g in gcs:
    around = [f for (t, f) in frames if g - 0.5 <= t <= g + 1.0]
    print(f'GC at {g:7.2f}s: worst frame within -0.5..+1.0 s = {max(around or [0]):.1f} ms')
base = sorted(f for (t, f) in frames)
print(f'all frames: median {base[len(base)//2]:.1f} ms, p99 {base[int(len(base)*.99)]:.1f} ms')
