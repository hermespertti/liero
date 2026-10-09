#!/usr/bin/env python3
"""Compare an ares screenshot of liero64 terrain vs the Python reference.

Usage: cmp_shot.py shot.png ref.png [expected_camx]
Shot is 640x240 (VI doubles width); ref is 504x350 at camy=0.
Brute-forces camx 0..(504-320), mirrors the ROM quantization on the ref:
ref entries are 6-bit; ROM packs RGB555 (6bit>>1), VI scanout expands
5-bit back to 8-bit as (v<<3)|(v>>2).
"""
import sys, zlib, struct

def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n"
    pos = 8
    idat = b""
    w = h = None
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        data = d[pos+8:pos+8+ln]
        if typ == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", data[:10])
            assert bd == 8 and ct == 2, f"expect 8-bit RGB, got bd={bd} ct={ct}"
        elif typ == b"IDAT":
            idat += data
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w*3 + 1
    rows = []
    prev = bytearray(w*3)
    for y in range(h):
        line = bytearray(raw[y*stride+1:(y+1)*stride])
        ft = raw[y*stride]
        if ft == 0:
            pass
        elif ft == 1:  # sub
            for i in range(3, len(line)):
                line[i] = (line[i] + line[i-3]) & 0xFF
        elif ft == 2:  # up
            for i in range(len(line)):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:  # avg
            for i in range(len(line)):
                a = line[i-3] if i >= 3 else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:  # paeth
            for i in range(len(line)):
                a = line[i-3] if i >= 3 else 0
                b = prev[i]
                c = prev[i-3] if i >= 3 else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        prev = line
        rows.append(line)
    return w, h, rows

def main():
    shot_path, ref_path = sys.argv[1], sys.argv[2]
    exp_cam = int(sys.argv[3]) if len(sys.argv) > 3 else None
    sw, h2, srows = read_png(shot_path)
    rw, rh, rrows = read_png(ref_path)
    VH = h2 if h2 <= 350 else 350
    VW = sw // 2

    # quantize ref 6-bit -> RGB555 -> back to 8-bit (mirror of ROM+VI path)
    rq = []
    for y in range(VH):
        row = rrows[y]
        q = bytearray(VW*3)
        for x in range(VW):
            r5 = row[x*3] >> 3; g5 = row[x*3+1] >> 3; b5 = row[x*3+2] >> 3
            # NOTE: this ares fork VI-expands RGB555 by truncating shift (v<<3),
            # verified vs pattern capture — no bit replication.
            q[x*3]   = r5 << 3
            q[x*3+1] = g5 << 3
            q[x*3+2] = b5 << 3
        rq.append(q)

    # auto-detect shot active area: min nonzero column across sampled rows
    x0 = 0
    for y in range(0, h2, max(1, h2//8)):
        nz = [x for x in range(sw) if any(srows[y][x*3:x*3+3])]
        if nz:
            x0 = max(x0, min(nz))  # conservative: last row's leftmost
    # VI doubles width: game x -> screen x0 + 2*x

    ys = list(range(0, VH, max(1, VH//16)))
    xs = list(range(0, VW, max(1, VW//40)))

    lo, hi = (exp_cam-32, exp_cam+32) if exp_cam is not None else (0, rw-VW)
    lo = max(0, lo); hi = min(rw-VW, hi)
    best = None
    for camx in range(lo, hi+1):
        bad = tot = 0; sad = 0
        for y in ys:
            sr = srows[y]; rr = rq[y]
            for x in xs:
                i = (x0 + (x + camx)*2)*3; j = x*3
                if i+2 >= len(sr): continue
                d = abs(sr[i]-rr[j]) + abs(sr[i+1]-rr[j+1]) + abs(sr[i+2]-rr[j+2])
                sad += d
                if d > 24: bad += 1
                tot += 1
        m = sad / (tot*3) if tot else 1e9
        if best is None or m < best[0]:
            best = (m, camx, bad, tot)
        if m == 0: break
    print(f"shot {sw}x{h2} ref {rw}x{rh} VW={VW} x0={x0}")
    print(f"best camx={best[1]} meanSAD={best[0]:.3f} badpix={best[2]}/{best[3]} ({100*best[2]/best[3]:.2f}%)")

if __name__ == "__main__":
    main()
