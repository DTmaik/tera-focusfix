import sys, collections
path = sys.argv[1]
# freeze windows per event from args "ev:start:end"
wins = {}
for a in sys.argv[2:]:
    e, s, t = a.split(':'); wins[int(e)] = (float(s), float(t))
rows = []
for line in open(path, encoding='latin-1'):
    p = line.rstrip('\n').split('\t')
    if len(p) < 5: continue
    rows.append((int(p[0]), p[1], p[2], float(p[3]), p[4:]))

def inwin(r):
    w = wins.get(r[0]);
    return w and w[0] <= r[3] <= w[1]

for label, sel in (('MAIN in freeze', lambda r: r[2] == 'M' and inwin(r)),
                   ('MAIN outside freeze', lambda r: r[2] == 'M' and not inwin(r))):
    rs = [r for r in rows if sel(r)]
    print(f'===== {label}: {len(rs)} samples')
    incl = collections.Counter(); leaf = collections.Counter()
    for r in rs:
        leaf[r[4][0]] += 1
        for f in set(r[4]): incl[f] += 1
    print('-- top leaf frames')
    for f, c in leaf.most_common(15): print(f'  {c*100/len(rs):5.1f}%  {f}')
    print('-- top inclusive frames')
    for f, c in incl.most_common(45): print(f'  {c*100/len(rs):5.1f}%  {f}')

# other threads in freeze
print('===== other threads in freeze')
by = collections.defaultdict(list)
for r in rows:
    if r[2] != 'M' and inwin(r): by[r[1]].append(r)
for tid, rs in sorted(by.items(), key=lambda kv: -len(kv[1]))[:4]:
    incl = collections.Counter()
    for r in rs:
        for f in set(r[4][:8]): incl[f] += 1
    print(f'-- thread {tid}: {len(rs)} samples')
    for f, c in incl.most_common(12): print(f'  {c*100/len(rs):5.1f}%  {f}')
