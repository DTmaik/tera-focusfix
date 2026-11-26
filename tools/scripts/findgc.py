# findgc.py <dump>: find "mov reg,[rip+G]; ... movss/comiss xmm,[reg+OFF]" pairs where the live
# value *(float*)(*G + OFF) == 60.0  -> candidates for GEngine->TimeBetweenPurgingPendingKillObjects
import re, struct, sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('TERA_PATH_FILTER', 'tera')
from mem64 import rd, u64, BASE
img = open(sys.argv[1], 'rb').read()
TEXT = (0x1000, 0x2009000); DATA = (0x2d94000, 0x2d94000 + 0x498000)
pairs = collections.Counter(); sites = collections.defaultdict(list)
# mov r64,[rip+disp32]: REX.W(48/4C) 8B modrm(00 reg 101)
for m in re.finditer(rb'[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', img[TEXT[0]:TEXT[1]]):
    off = TEXT[0] + m.start()
    g = off + 7 + struct.unpack_from('<i', img, off + 3)[0]
    if not (DATA[0] <= g < DATA[1]): continue
    reg = (img[off + 2] >> 3) & 7
    w = img[off + 7: off + 40]
    # movss xmm,[reg+disp32] = F3 (REX?) 0F 10 modrm(10 xxx reg) ; comiss = (REX?) 0F 2F modrm
    for mm in re.finditer(rb'(?:\xf3[\x40-\x4f]?\x0f\x10|[\x40-\x4f]?\x0f\x2f)([\x80-\xbf])(.{4})', w, re.S):
        modrm = mm.group(1)[0]
        if (modrm & 7) == reg and (modrm >> 6) == 2:
            disp = struct.unpack('<i', mm.group(2))[0]
            if 0 < disp < 0x4000: pairs[(g, disp)] += 1; sites[(g, disp)].append(off)
print(len(pairs), 'global/offset pairs')
hits = []
for (g, disp), cnt in pairs.items():
    obj = u64(BASE + g)
    if not obj or obj < 0x10000: continue
    b = rd(obj + disp, 4)
    if len(b) == 4 and abs(struct.unpack('<f', b)[0] - 60.0) < 1e-4: hits.append((g, disp, cnt, obj))
for g, disp, cnt, obj in hits:
    print(f'global rva {g:08x} -> obj {obj:x}, +0x{disp:x} == 60.0  ({cnt} code refs, e.g. {", ".join(hex(s) for s in sites[(g, disp)][:4])})')
