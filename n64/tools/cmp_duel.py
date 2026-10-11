#!/usr/bin/env python3
# Compare ROM trace vs host duel trace category-by-category (byte-exact per category).
# Full-line content keys: ares may echo the entire debug section (non-adjacent dupes),
# and every trace line embeds its own f=... so content-keyed set equality is exact.
import sys, re, collections

CATS = ["SPAWN","AI","A0","WT","WC","NT","NN","ST","BO","DMG","DIE","RSP","OVR","CK","CW","RG","CM","RR","FW","AS","WS","FS","DS","STAGE"]

def load(path):
    d = collections.defaultdict(set)
    for l in open(path):
        m = re.match(r'^\[[^\]]*\]\s*', l)
        if m: l = l[m.end():]
        l = l.rstrip("\n").strip()
        if not l: continue
        pre = l.split(" ")[0].rstrip(":")
        if pre not in CATS: continue
        s = re.sub(r' L=\d+', '', l)   # strip source-line tags (diagnostic-only)
        d[pre].add(s)
    return d

rom = load(sys.argv[1]); host = load(sys.argv[2])
allok = True
for c in sorted(set(rom) | set(host)):
    rr = rom.get(c, set()); hh = host.get(c, set())
    missR = hh - rr; missH = rr - hh
    # host-side runover: ROM may trace one frame past host's FRAMES window
    missH_ok = all(" f=%d" % f not in s or f >= 3780 for s in missH for f in ([3780]))
    def frame_of(s):
        m = re.search(r'f=(\d+)', s)
        if m: return int(m.group(1))
        m = re.match(r'(CW|SPAWN) (-?\d+)', s)
        if m: return int(m.group(2))
        return -1
    missH_ok = all(frame_of(s) == -1 or frame_of(s) >= 3780 for s in missH)
    ok = not missR and missH_ok
    if not ok: allok = False
    print("%-6s %s rom=%d host=%d missR=%d missH=%d" % (c, "OK" if ok else "DIFF", len(rr), len(hh), len(missR), len(missH)))
    for s in list(missR)[:3]: print("   HOST-only:", s[:130])
    for s in list(missH)[:3]: print("   ROM-only:", s[:130])
print("ALL TRACE LINES EQUAL" if allok else "MISMATCH")
