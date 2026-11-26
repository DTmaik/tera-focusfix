# xref.py <dump> <rva>...: function start (via .pdata) + direct call sites (E8) of each function
import struct, sys, re
img = open(sys.argv[1], 'rb').read()
PDATA = (0x0322c000, 0x164000)
TEXT = (0x1000, 0x2008000)
entries = []
for o in range(PDATA[0], PDATA[0] + PDATA[1], 12):
    b, e, u = struct.unpack_from('<III', img, o)
    if b == 0: break
    entries.append((b, e))
def func_of(rva):
    lo, hi = 0, len(entries) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e = entries[m]
        if rva < b: hi = m - 1
        elif rva >= e: lo = m + 1
        else: return b, e
    return None
# index all E8 call targets once
calls = {}
t = img[TEXT[0]:TEXT[1]]
for m in re.finditer(rb'\xe8', t):
    off = TEXT[0] + m.start()
    if off + 5 > TEXT[1]: break
    tgt = off + 5 + struct.unpack_from('<i', img, off + 1)[0]
    if TEXT[0] <= tgt < TEXT[1]: calls.setdefault(tgt, []).append(off)
for a in sys.argv[2:]:
    rva = int(a, 16)
    f = func_of(rva)
    print(f'rva {rva:08x}: function {f[0]:08x}-{f[1]:08x} ({f[1]-f[0]} bytes)' if f else f'rva {rva:08x}: no pdata entry')
    if f:
        cs = calls.get(f[0], [])
        print(f'  {len(cs)} direct callers' + (':' if cs else ''))
        for c in cs[:15]:
            cf = func_of(c)
            print(f'    call at {c:08x} in function {cf[0]:08x}' if cf else f'    call at {c:08x}')
