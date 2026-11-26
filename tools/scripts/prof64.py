# prof64.py <seconds> [front]: sample the 64-bit TERA game thread (leaf RIP + return-address scan)
# and report time in the "recorder", in SetEvent/Wait syscalls, per module; plus per-thread CPU.
import ctypes, ctypes.wintypes as wt, struct, sys, time, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('TERA_PATH_FILTER', 'tera')
from mem64 import k, PID, MODS, BASE, rd, H
u = ctypes.WinDLL('user32'); u.GetForegroundWindow.restype = wt.HWND
k.OpenThread.restype = wt.HANDLE
k.GetModuleHandleW.restype = ctypes.c_void_p; k.GetProcAddress.restype = ctypes.c_void_p
k.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dur = float(sys.argv[1]); front = len(sys.argv) > 2

# main thread = owner of the biggest visible top-level window of the process
wins = []
@ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
def cb(h, l):
    p = wt.DWORD(); t = u.GetWindowThreadProcessId(h, ctypes.byref(p))
    if p.value == PID and u.IsWindowVisible(h):
        r = wt.RECT(); u.GetWindowRect(h, ctypes.byref(r)); wins.append(((r.right - r.left) * (r.bottom - r.top), h, t))
    return True
u.EnumWindows(cb, 0)
wins.sort(); area, hwnd, mainTid = wins[-1]

ntdll = k.GetModuleHandleW('ntdll.dll')
sym = {n: k.GetProcAddress(ntdll, n.encode()) for n in ('NtSetEvent', 'NtWaitForSingleObject', 'NtWaitForMultipleObjects', 'NtDelayExecution', 'NtYieldExecution')}
RANGES = {  # rva ranges inside TERA.osto.exe
    'recorder fn A (1ab2110)': (0x1ab2110, 0x1ab2200), 'recorder fn B (1ab2200)': (0x1ab2200, 0x1ab22fe),
    'recorder init (140f700)': (0x140f700, 0x140f800), 'recorder copy (1ab4840)': (0x1ab4840, 0x1ab4960)}
RECORDER_RETS = {0x1ab21d0, 0x1ab21dc, 0x1ab22c5, 0x1ab22d1}
mods = sorted((b, b + sz, n) for n, b, sz, p in MODS)
def modof(a):
    for b, e, n in mods:
        if b <= a < e: return n
    return '?'

THREAD_ALL = 0x0002 | 0x0008 | 0x0040   # suspend/resume, get context, query info
ht = k.OpenThread(THREAD_ALL, False, mainTid)
raw = ctypes.create_string_buffer(1232 + 16); ctx = (ctypes.addressof(raw) + 15) & ~15
def sample():
    if k.SuspendThread(ht) == 0xFFFFFFFF: return None
    ctypes.memset(ctx, 0, 1232); struct.pack_into('<I', (ctypes.c_char * 1232).from_address(ctx), 0x30, 0x00100001)
    ok = k.GetThreadContext(ht, ctypes.c_void_p(ctx))
    rip = struct.unpack_from('<Q', (ctypes.c_char * 1232).from_address(ctx), 0xF8)[0]
    rsp = struct.unpack_from('<Q', (ctypes.c_char * 1232).from_address(ctx), 0x98)[0]
    stk = rd(rsp, 0x800) if ok else b''
    k.ResumeThread(ht)
    return (rip, stk) if ok else None

def bring(w):
    fg = u.GetForegroundWindow(); me = k.GetCurrentThreadId(); ft = u.GetWindowThreadProcessId(fg, None)
    u.AttachThreadInput(me, ft, True); u.SetForegroundWindow(w); u.BringWindowToTop(w); u.AttachThreadInput(me, ft, False)
other = u.GetForegroundWindow()
if front: bring(hwnd); time.sleep(1.0)

# per-thread CPU (all threads of the process)
def thread_ids():
    class TE(ctypes.Structure):
        _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ThreadID', wt.DWORD), ('th32OwnerProcessID', wt.DWORD), ('tpBasePri', ctypes.c_long), ('tpDeltaPri', ctypes.c_long), ('dwFlags', wt.DWORD)]
    s = k.CreateToolhelp32Snapshot(4, 0); te = TE(); te.dwSize = ctypes.sizeof(TE); ids = []
    ok = k.Thread32First(wt.HANDLE(s), ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == PID: ids.append(te.th32ThreadID)
        ok = k.Thread32Next(wt.HANDLE(s), ctypes.byref(te))
    return ids
def cpu(tid):
    h = k.OpenThread(0x0040 | 0x0800, False, tid)
    if not h: return None
    c, e, kt, ut = (wt.FILETIME() for _ in range(4))
    k.GetThreadTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(kt), ctypes.byref(ut)); k.CloseHandle(h)
    f = lambda x: (x.dwHighDateTime << 32 | x.dwLowDateTime) / 1e4
    return f(kt) + f(ut)
tids = thread_ids(); c0 = {t: cpu(t) for t in tids}

n = 0; cat = collections.Counter(); modc = collections.Counter(); recIncl = 0; t0 = time.time()
while time.time() - t0 < dur:
    s = sample()
    if not s: continue
    rip, stk = s; n += 1
    m = modof(rip); modc[m] += 1
    rva = rip - BASE
    hit = None
    for name, (a, b) in RANGES.items():
        if a <= rva < b: hit = name
    for name, addr in sym.items():
        if addr <= rip < addr + 0x20: hit = name
    cat[hit or 'other'] += 1
    rets = {struct.unpack_from('<Q', stk, i)[0] - BASE for i in range(0, len(stk) - 7, 8)}
    if hit and hit.startswith('recorder') or rets & RECORDER_RETS: recIncl += 1
    time.sleep(0.0008)
el = time.time() - t0
c1 = {t: cpu(t) for t in tids}
if front: bring(other)

print(f'pid {PID} main thread {mainTid}, {n} samples in {el:.1f}s')
print(f'recorder (inclusive, incl. its callees): {recIncl*100/n:.2f}% of game-thread samples')
for kk, v in cat.most_common(): print(f'  {v*100/n:6.2f}%  {kk}')
print('-- leaf module')
for kk, v in modc.most_common(8): print(f'  {v*100/n:6.2f}%  {kk}')
print('-- thread CPU over the window (top 6)')
rows = sorted(((c1[t] - c0[t]) if c0.get(t) is not None and c1.get(t) is not None else 0, t) for t in tids)[::-1][:6]
for v, t in rows: print(f'  thread {t}{" (main)" if t == mainTid else ""}: {v*100/(el*1000):5.1f}% CPU')
