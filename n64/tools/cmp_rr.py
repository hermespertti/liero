#!/usr/bin/env python3
# Side-by-side RR/FW/AS stream comparison (f=185 RNG forensics).
import sys, re

def stream(path, pres):
    out = []
    prev = None
    for l in open(path):
        s = re.sub(r'^\[[^\]]*\]\s*', '', l.strip())
        if not s: continue
        pre = s.split()[0]
        if pre in pres and s != prev:
            out.append(s)
        prev = s
    return out

rom = stream(sys.argv[1], {"RR","FW","AS","AI","A0","WC"})
host = stream(sys.argv[2], {"RR","FW","AS","AI","A0","WC"})
print("== ROM ==")
for l in rom: print(l)
print("== HOST ==")
for l in host: print(l)
