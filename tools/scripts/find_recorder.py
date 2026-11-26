# scan.py <dump> : pattern searches in the 64-bit v115 memory dump (file layout == memory layout)
import re, struct, sys
img = open(sys.argv[1], 'rb').read()
BASE = 0x7ff69a690000
TEXT = (0x1000, 0x1000 + 0x2008000)

def find(pat, lo=TEXT[0], hi=TEXT[1]):
    return [m.start() for m in re.finditer(pat, img[lo:hi], re.S)]

def rel32_target(off):  # off = offset of E8
    return off + 5 + struct.unpack_from('<i', img, off + 1)[0]

print('== Terarizer record-enter pattern: 48 89 53 04 E8 ....(7) E8')
hits = find(rb'\x48\x89\x53\x04\xe8.{4}.{7}\xe8')
for h in hits[:20]:
    r = TEXT[0] + h
    print(f'  rva {r:08x}: {img[r:r+21].hex(" ")}  call1->{rel32_target(r+4):08x} call2->{rel32_target(r+0x10):08x}')
print(f'  {len(hits)} hits')

print('== looser: 48 89 53 04 E8')
hits2 = find(rb'\x48\x89\x53\x04\xe8')
print(f'  {len(hits2)} hits')
for h in hits2[:30]:
    r = TEXT[0] + h
    print(f'  rva {r:08x}: {img[r:r+24].hex(" ")}')
