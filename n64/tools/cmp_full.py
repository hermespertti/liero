#!/usr/bin/env python3
"""Full-range camx brute force; coarse then exact. Proves pixel correctness of
terrain render regardless of capture timing."""
import sys, zlib, struct

def read_png(path):
    d = open(path, "rb").read(); pos = 8; idat = b""; w = h = None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos+4])[0]; typ = d[pos+4:pos+8]; data = d[pos+8:pos+8+ln]
        if typ == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", data[:10])
        elif typ == b"IDAT": idat += data
        elif typ == b"IEND": break
        pos += 12 + ln
    raw = zlib.decompress(idat); stride = w*3+1; rows = []; prev = bytearray(w*3)
    for y in range(h):
        line = bytearray(raw[y*stride+1:(y+1)*stride]); ft = raw[y*stride]
        if ft == 1:
            for i in range(3, len(line)): line[i] = (line[i]+line[i-3]) & 0xFF
        elif ft == 2:
            for i in range(len(line)): line[i] = (line[i]+prev[i]) & 0xFF
        elif ft == 3:
            for i in range(len(line)):
                a = line[i-3] if i >= 3 else 0
                line[i] = (line[i]+((a+prev[i])>>1)) & 0xFF
        elif ft == 4:
            for i in range(len(line)):
                a = line[i-3] if i >= 3 else 0; b = prev[i]; c = prev[i-3] if i >= 3 else 0
                p = a+b-c; pa,pb,pc = abs(p-a),abs(p-b),abs(p-c)
                pr = a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i] = (line[i]+pr) & 0xFF
        prev = line; rows.append(line)
    return w, h, rows

def main():
    sw, h2, srows = read_png(sys.argv[1])
    rw, rh, rrows = read_png(sys.argv[2])
    VH, VW = 240, 320
    # ref 8-bit holds 6-bit<<2; quantize to ROM+VI model: (v6>>1)<<3
    rq = [bytes((((rrows[y][x*3]>>1)<<3) & 0xF8) if False else 0 for x in range(rw*3)) for y in range(VH)]
    rq = []
    for y in range(VH):
        row = rrows[y]
        q = bytearray(rw*3)
        for x in range(rw):
            q[x*3]   = ((row[x*3]>>3)<<3)
            q[x*3+1] = ((row[x*3+1]>>3)<<3)
            q[x*3+2] = ((row[x*3+2]>>3)<<3)
        rq.append(q)
    ys = range(0, VH, 2)
    best = None
    for x0 in (0, 8):
        for camx in range(0, rw-VW+1):
            bad = tot = 0
            for y in ys:
                sr = srows[y]
                for x in range(0, VW, 2):
                    sx = (x0 + (x+camx)*2)*3
                    if sx+2 >= len(sr): continue
                    d = abs(sr[sx]-rq[y][x*3]) + abs(sr[sx+1]-rq[y][x*3+1]) + abs(sr[sx+2]-rq[y][x*3+2])
                    if d > 24: bad += 1
                    tot += 1
            r = bad/tot
            if best is None or r < best[0]:
                best = (r, x0, camx, bad, tot)
            if bad == 0: break
    print("shot=%s BEST bad=%d/%d (%.3f%%) x0=%d camx=%d" % (sys.argv[1], best[3], best[4], 100*best[0], best[1], best[2]))

main()
