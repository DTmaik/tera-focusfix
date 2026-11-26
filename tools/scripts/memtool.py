# small live-memory helper for the 32-bit TERA process
import ctypes, ctypes.wintypes as wt, struct, sys
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wt.HANDLE
k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
u = ctypes.WinDLL('user32'); u.FindWindowW.restype = wt.HWND
w = u.FindWindowW('LaunchUnrealUWindowsClient', 'TERA'); pid = wt.DWORD(); u.GetWindowThreadProcessId(w, ctypes.byref(pid))
h = k.OpenProcess(0x0410, False, pid.value)
def rd(a, n):
    b = ctypes.create_string_buffer(n); g = ctypes.c_size_t()
    k.ReadProcessMemory(h, a, b, n, ctypes.byref(g)); return b.raw[:g.value]
def u32(a): return struct.unpack('<I', rd(a, 4))[0]
if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'floats':   # floats <ptr-to-object-pointer> <size> <value>
        obj = u32(int(sys.argv[2], 16)); n = int(sys.argv[3], 16); val = float(sys.argv[4])
        b = rd(obj, n); print(f'object @ {obj:08x}, vtable {u32(obj):08x}')
        for off in range(0, len(b) - 3, 4):
            f = struct.unpack_from('<f', b, off)[0]
            if abs(f - val) < 1e-3: print(f'  +0x{off:x} = {f}')
    elif cmd == 'dump':   # dump <addr> <size>
        a = int(sys.argv[2], 16); b = rd(a, int(sys.argv[3], 16))
        for o in range(0, len(b), 16): print(f'{a+o:08x}  ' + ' '.join(f'{x:02x}' for x in b[o:o+16]))
