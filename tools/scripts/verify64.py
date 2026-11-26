import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('TERA_PATH_FILTER', 'tera')
from mem64 import rd, u64, BASE, PID
print('pid', PID)
for rva in (0x1ab21c7, 0x1ab22bc):
    print(f'recorder site {rva:08x}: {rd(BASE + rva, 21).hex(" ")}')
ge = u64(BASE + 0x3093120)
print('GC interval =', struct.unpack('<f', rd(ge + 0x7c8, 4))[0])
w = u64(BASE + 0x3097708)
print('time since last GC =', round(struct.unpack('<f', rd(w + 0x260, 4))[0], 1), 's')
