# Dump a module image from a running process into a PE whose file layout == memory layout.
import ctypes, ctypes.wintypes as wt, struct, sys
pid = int(sys.argv[1]); base = int(sys.argv[2], 16); out = sys.argv[3]
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wt.HANDLE
k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
h = k.OpenProcess(0x0410, False, pid)
def rd(a, n):
    b = ctypes.create_string_buffer(n); g = ctypes.c_size_t()
    if not k.ReadProcessMemory(h, a, b, n, ctypes.byref(g)): return None
    return b.raw[:g.value]
hdr = rd(base, 0x1000)
pe = struct.unpack_from('<I', hdr, 0x3c)[0]
nsec = struct.unpack_from('<H', hdr, pe + 6)[0]
optsz = struct.unpack_from('<H', hdr, pe + 20)[0]
size = struct.unpack_from('<I', hdr, pe + 24 + 56)[0]
img = bytearray(size)
for off in range(0, size, 0x1000):
    b = rd(base + off, 0x1000)
    if b: img[off:off + len(b)] = b
sh = pe + 24 + optsz
for i in range(nsec):
    o = sh + 40 * i
    vsz, va = struct.unpack_from('<II', img, o + 8)
    name = img[o:o + 8].rstrip(b'\0').decode('latin-1')
    struct.pack_into('<II', img, o + 16, vsz, va)  # SizeOfRawData, PointerToRawData
    print(f'{name:8} va={va:08x} size={vsz:08x} ch={struct.unpack_from("<I", img, o + 36)[0]:08x}')
open(out, 'wb').write(img)
print('wrote', len(img))
