#!/usr/bin/env python3
"""Bake Liero TC assets -> C header + reference terrain PNG.

Mirrors src/game/common.cpp (TGA loading), level.cpp (generateDirtPattern/
generateRandom stone+dirt stamping) and blit.cpp (blitStone, drawDirtEffect)
with byte-exact semantics. The ROM runs the same generator in C; this script
is the host reference.

Outputs:
  n64/assets/baked.h          palette (RGB555), large stamps, textures, materials
  n64/ref/terrain_ref.png     reference render of generated level
"""
import re, struct, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # n64/
TC = os.path.join(os.path.dirname(ROOT), "TC", "openliero")
LEVEL_W, LEVEL_H = 504, 350

# ---------- mwc RNG (gvl::mwc, exact) ----------
class MWC:
    def __init__(self, seed=0x1337):
        self.x = seed & 0xFFFFFFFF
        self.c = 9413207
    def next(self):
        t = 2083801278 * self.x + self.c
        self.c = t >> 32
        self.x = t & 0xFFFFFFFF
        return self.x
    def bounded(self, maxv):  # prng_common operator()(uint32_t max): ALWAYS advances state
        maxv &= 0xFFFFFFFF
        v = self.next()
        return (v * maxv) >> 32

# ---------- TGA ----------
def read_tga(path):
    d = open(path, "rb").read()
    assert d[1] == 1 and d[2] == 1, "expect color-mapped RLE-less TGA"
    pstart, plen, bps = struct.unpack_from("<HHB", d, 3)
    assert plen == 256 and bps == 24
    w, h = struct.unpack_from("<HH", d, 12)
    assert d[16] == 8 and d[17] == 0
    off = 18 + d[0]
    pal = []
    for i in range(256):
        b, g, r = d[off + i*3 : off + i*3 + 3]
        pal.append((r >> 2, g >> 2, b >> 2))  # 6-bit like engine
    off += 768
    # rows bottom-up in TGA; engine reads sequentially -> sprite order top row first
    pix = d[off:off + w*h]
    # engine reads TGA sequentially (no row flip)
    return w, h, pal, list(pix)

# ---------- tc.cfg ----------
def load_cfg(path):
    txt = open(path).read()
    materials = [int(v) for v in re.search(r"^\s*materials = \[([^\]]+)\]", txt, re.M).group(1).replace("\n"," ").split(",")]
    textures = []
    for m in re.finditer(r"\[\[constants\.textures\]\](.*?)(?=\[\[|\[texts\]|\[hacks\]|$)", txt, re.S):
        blk = m.group(1)
        g = lambda k: int(re.search(rf"{k} = (-?\d+)", blk).group(1))
        textures.append(dict(mFrame=g("mframe"), rFrame=g("rframe"),
                             sFrame=g("sframe"),
                             nDrawBack="true" in re.search(r"ndrawback = (\w+)", blk).group(1)))
    return materials, textures

# ---------- material flags (from baked tc.cfg byte values) ----------
DIRT, DIRT2, ROCK, BACK, SEE = 1, 2, 4, 8, 16
def has(f, m): return (f & m) != 0

# ---------- generator (mirror of level.cpp generateRandom minus origpal) ----------
def generate(mats, textures, pal, large):
    rnd = MWC(0x1337)
    W, H = LEVEL_W, LEVEL_H
    data = bytearray(W*H)
    mflat = bytes(mats)

    def sp(x, y, v):
        if 0 <= x < W and 0 <= y < H:
            data[y*W+x] = v & 0xFF

    def px(x, y):
        return data[y*W+x]

    def stage(tag):
        h = 2166136261
        for b in data: h = ((h ^ b) * 16777619) & 0xFFFFFFFF
        print(f"STAGE {tag} hash=%08x" % h)

    # generateDirtPattern
    sp(0, 0, rnd.bounded(7) + 12)
    for y in range(1, H):
        sp(0, y, ((rnd.bounded(7) + 12) + px(0, y-1)) >> 1)
    for x in range(1, W):
        sp(x, 0, ((rnd.bounded(7) + 12) + px(x-1, 0)) >> 1)
    for y in range(1, H):
        rowbase = y*W
        for x in range(1, W):
            v = (px(x-1, y) + px(x, y-1) + rnd.bounded(8) + 12) // 3
            data[rowbase + x] = v & 0xFF

    def sprite(f):  # 16x16 frame from large.tga
        return large[f*256:(f+1)*256]

    # dirt pattern stamps (frames 69..72)
    for _ in range(rnd.bounded(100)):
        x = rnd.bounded(W) - 8
        y = rnd.bounded(H) - 8
        img = sprite(rnd.bounded(4) + 69)
        for cy in range(16):
            my = cy + y
            if my >= H: break
            if my < 0: continue
            for cx in range(16):
                mx = cx + x
                if mx >= W: break
                if mx < 0: continue
                src = img[cy*16+cx]
                if src > 0:
                    p = px(mx, my)
                    if 176 < p < 180:
                        sp(mx, my, (src + p)//2)
                    else:
                        sp(mx, my, src)

    # stone stamps (frames 56..59); exact RNG order: x, y, which
    def blit_stone(img, x, y):
        for cy in range(16):
            for cx in range(16):
                mx, my = x+cx, y+cy
                if 0 <= mx < W and 0 <= my < H:
                    c = img[cy*16+cx]
                    if c:
                        sp(mx, my, c)

    for _ in range(rnd.bounded(15)):
        x = rnd.bounded(W) - 8
        y = rnd.bounded(H) - 8
        which = rnd.bounded(4) + 56
        blit_stone(sprite(which), x, y)
    stage("dirt")

    def dirt_effect(eff, x, y):
        tx = textures[eff]
        t_frame = sprite(tx["sFrame"] + rnd.bounded(tx["rFrame"]))
        m_frame = sprite(tx["mFrame"])
        for cy in range(16):
            for cx in range(16):
                mx, my = x+cx, y+cy
                if not (0 <= mx < W and 0 <= my < H-1):
                    continue
                c = m_frame[cy*16+cx]
                i = my*W+mx
                mf = mflat[data[i]]
                if tx["nDrawBack"]:
                    if c == 6 and has(mf, DIRT|DIRT2):
                        v = t_frame[(my & 15)*16 + (mx & 15)]
                        data[i] = v
                    elif c == 1:
                        if has(mf, DIRT2): data[i] = 2
                        elif has(mf, DIRT): data[i] = 1
                else:
                    if c in (10, 6):
                        if has(mf, BACK):
                            data[i] = t_frame[(my & 15)*16 + (mx & 15)]
                    elif c == 2:
                        if has(mf, BACK): data[i] = 2
                    elif c == 1:
                        if has(mf, BACK): data[i] = 1

    # generateRandom dirt trail phase (exact RNG order: count2 -> count3 -> per-effect rFrame -> jitters)
    for _ in range(rnd.bounded(50) + 5):
        cx = rnd.bounded(W) - 8
        cy = rnd.bounded(H) - 8
        dx = rnd.bounded(11) - 5
        dy = rnd.bounded(5) - 2
        for _ in range(rnd.bounded(12)):
            count3 = rnd.bounded(5)
            for _ in range(count3):
                cx += dx; cy += dy
                dirt_effect(1, cx, cy)
            cx -= (count3 + 1) * dx
            cy -= (count3 + 1) * dy
            cx += rnd.bounded(7) - 3
            cy += rnd.bounded(15) - 7
    stage("trails")

    # stone blobs phase
    stone_tab = [[98,60,61,62],[63,75,85,86],[89,90,97,96]]
    def is_no_rock(size, x, y):
        for yy in range(max(0,y), min(H, y+size+1)):
            for xx in range(max(0,x), min(W, x+size+1)):
                if has(mflat[data[yy*W+xx]], ROCK):
                    return False
        return True

    for _ in range(rnd.bounded(15) + 5):
        while True:
            cx = rnd.bounded(W) - 16
            cy = (H - 1 - rnd.bounded(20)) if rnd.bounded(4) == 0 else rnd.bounded(H) - 16
            if is_no_rock(32, cx, cy): break
        rock = rnd.bounded(3)
        t = stone_tab[rock]
        blit_stone(sprite(t[0]), cx, cy)
        blit_stone(sprite(t[1]), cx+16, cy)
        blit_stone(sprite(t[2]), cx, cy+16)
        blit_stone(sprite(t[3]), cx+16, cy+16)

    for _ in range(rnd.bounded(25) + 5):
        while True:
            cx = rnd.bounded(W) - 8
            cy = (H - 1 - rnd.bounded(13)) if rnd.bounded(5) == 0 else rnd.bounded(H) - 8
            if is_no_rock(15, cx, cy): break
        blit_stone(sprite(rnd.bounded(6) + 3), cx, cy)
    stage("smallrocks")

    # makeShadow (upstream order: shadow loop, then bottom row fill)
    for x in range(W-3):
        for y in range(3, H):
            i = y*W+x
            if has(mflat[data[i]], SEE) and has(mflat[data[(y-3)*W+x+3]], DIRT|DIRT2|ROCK):
                data[i] = (data[i] + 4) & 0xFF
            if 12 <= data[i] <= 18 and has(mflat[data[(y-3)*W+x+3]], ROCK):
                v = data[i] - 2
                data[i] = max(v, 12)

    # bottom row fill
    for x in range(W):
        if has(mflat[data[(H-1)*W+x]], BACK):
            data[(H-1)*W+x] = 13
    stage("shadow")

    return bytes(data), rnd

def main():
    sw, sh, pal, smallpix = read_tga(os.path.join(TC, "sprites", "small.tga"))
    lw, lh, _, largepix = read_tga(os.path.join(TC, "sprites", "large.tga"))
    mats, textures = load_cfg(os.path.join(TC, "tc.cfg"))
    assert sh % 7 == 0 and lh % 16 == 0
    assert len(textures) >= 9, f"textures parsed: {len(textures)}"

    # baked.h for the ROM: palette RGB555 (entries are 6-bit -> >>1),
    # material flags, texture table, large stamps
    def c16(rgb):
        r,g,b = rgb
        return ((r>>1)<<10)|((g>>1)<<5)|(b>>1)|0x8000
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    with open(os.path.join(ROOT, "assets", "baked.h"), "w") as f:
        f.write("// Auto-baked from TC/openliero by tools/bake_assets.py. Do not edit.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define PAL_COUNT 256\nstatic const uint16_t pal_rgb555[256]={")
        for rgb in pal: f.write("0x%04x,"%c16(rgb))
        f.write("};\n\n")
        f.write("static const uint8_t mat_flags[256]={"+",".join(map(str,mats))+"};\n\n")
        f.write("#define TEX_COUNT %d\ntypedef struct { int8_t mFrame,rFrame,sFrame; uint8_t nDrawBack; } TexEnt;\nstatic const TexEnt tex_tab[TEX_COUNT]={\n"%len(textures))
        for tx in textures: f.write("  {%d,%d,%d,%d},\n"%(tx["mFrame"],tx["rFrame"],tx["sFrame"],tx["nDrawBack"]))
        f.write("};\n\n")
        f.write("#define LARGE_FRAMES %d\nstatic const uint8_t large_stamps[]={\n"%(lh//16))
        for i in range(0,len(largepix),32):
            f.write(",".join(map(str,largepix[i:i+32]))+",\n")
        f.write("};\n\n")

        # cossin table: exact upstream math.cpp precomputeTables
        def fp_reducedfrac(s, bits, tobits):
            rbits = bits
            while rbits > 60:
                s >>= 1; rbits -= 1
            return s << (tobits - rbits)
        xs = [0]*128; ys = [0]*128
        scalebits = 28; scale = 13176795
        for i in range(128):
            rf = 0; c = -1; xf = i * scale
            ns, nb = xf, scalebits
            t = 1
            while t < 26:
                rf += c * fp_reducedfrac(ns, nb, 60)
                t += 1; ns //= t
                lim = 1 << 31
                while ns < -lim-1 or ns > lim: ns >>= 1; nb -= 1
                ns *= xf; nb += scalebits
                t += 1; ns //= t
                while ns < -lim-1 or ns > lim: ns >>= 1; nb -= 1
                ns *= xf; nb += scalebits
                c = -c
            shift = 60 - 16
            rf += 1 << (shift-1)
            r = rf >> shift
            if r >= 0x80000000: r -= 0x100000000
            xs[i] = r; ys[(i+32) & 0x7f] = r
        f.write("static const int32_t cs_x[128]={"+",".join(map(str,xs))+"};\n")
        f.write("static const int32_t cs_y[128]={"+",".join(map(str,ys))+"};\n\n")

        # small sprites (7px frames) for nobject render
        f.write("#define SMALL_FRAMES %d\nstatic const uint8_t small_pix[]={\n"%(sh//7))
        for i in range(0,len(smallpix),32):
            f.write(",".join(map(str,smallpix[i:i+32]))+",\n")
        f.write("};\n")
        print("wrote assets/baked.h")

    terrain, _rnd = generate(mats, textures, pal, largepix)

    os.makedirs(os.path.join(ROOT, "ref"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    open(os.path.join(ROOT, "ref", "terrain.bin"), "wb").write(terrain)

    # reference PNG (RGB, 6-bit<<2)
    try:
        import zlib, struct as st
        w, h = LEVEL_W, LEVEL_H
        raw = b"".join(b"\x00" + b"".join(
            bytes((pal[p][0]<<2, pal[p][1]<<2, pal[p][2]<<2))
            for p in terrain[y*w:(y+1)*w]) for y in range(h))
        def chunk(t, d):
            return st.pack(">I", len(d)) + t + d + st.pack(">I", zlib.crc32(t+d) & 0xFFFFFFFF)
        png = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", st.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw, 9))
               + chunk(b"IEND", b""))
        open(os.path.join(ROOT, "ref", "terrain_ref.png"), "wb").write(png)
        print("wrote ref/terrain_ref.png", len(png))
    except ImportError:
        pass
    print("wrote ref/terrain.bin", len(terrain))
    print("first row sample:", list(terrain[:32]))

if __name__ == "__main__":
    main()
