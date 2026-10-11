#!/usr/bin/env python3
# Compare ROM trace vs host duel trace category-by-category (byte-exact per category).
import sys, re, collections

def load(path, prefixes):
    out = collections.defaultdict(list)
    seen = set()
    for l in open(path):
        l = l.rstrip("\n")
        if not l: continue
        pre = l.split()[0]
        if pre not in prefixes: continue
        out[pre].append(l)
    return out

CATS = ["SPAWN","AI","A0","WT","WC","NT","NN","ST","BO","DMG","DIE","RSP","OVR","CK","CW","RG","CM","RR","FW","AS","WS","FS","DS"]

rom = load(sys.argv[1], CATS)
host = load(sys.argv[2], CATS)

# ares log may echo console.log section: rom file has each line twice (once [debug] prefix?).
# normalize: strip leading "[...]" debug prefixes
def norm(lines):
    out=[]
    for l in lines:
        m=re.match(r'^\[[^\]]*\]\s*',l)
        if m: l=l[m.end():]
        out.append(l)
    # dedup consecutive duplicates (echo)
    ded=[]
    prev=None
    for l in out:
        if l!=prev: ded.append(l)
        prev=l
    return ded

allok=True
cats=sorted(set(list(rom)+list(host)))
for c in cats:
    r=norm(rom.get(c,[])); h=norm(host.get(c,[]))
    if r==h:
        print(f"{c} {len(r)} OK")
    else:
        allok=False
        print(f"{c} DIFF rom={len(r)} host={len(h)}")
        n=max(len(r),len(h))
        shown=0
        for i in range(n):
            a=r[i] if i<len(r) else "<none>"
            b=h[i] if i<len(h) else "<none>"
            if a!=b:
                print("  ROM :",a)
                print("  HOST:",b)
                shown+=1
                if shown>=5: break
print("ALL TRACE LINES EQUAL" if allok else "MISMATCH")
