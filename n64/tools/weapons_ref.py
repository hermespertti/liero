#!/usr/bin/env python3
"""Host mirror of ROM weapons slice (dirtball + dirt nobject).
Generates terrain via bake_assets (exact RNG stream), carves the same WORM
test chamber, runs the scripted AUTOTEST inputs and prints CK trace lines
identical in format to the ROM debugf output for byte-for-byte comparison."""
import os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_assets import MWC, load_cfg, read_tga, generate, TC, LEVEL_W, LEVEL_H

W, H = LEVEL_W, LEVEL_H
DIRT, DIRT2, ROCK, BACK, SEE = 1, 2, 4, 8, 16
M_DIRTROCK = DIRT | DIRT2 | ROCK

sw, sh, pal, smallpix = read_tga(os.path.join(TC, "sprites", "small.tga"))
lw, lh, _, largepix = read_tga(os.path.join(TC, "sprites", "large.tga"))
mats, textures = load_cfg(os.path.join(TC, "tc.cfg"))
mflat = bytes(mats)

terrain, rnd = generate(mats, textures, pal, largepix)
data = bytearray(terrain)

def i32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v

def tdiv(a, b):  # C truncation division
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q

def sp(x, y, v):
    if 0 <= x < W and 0 <= y < H:
        data[y*W+x] = v & 0xFF

def px(x, y):
    return data[y*W+x]

def dirtrock(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return True
    return (mflat[data[y*W+x]] & M_DIRTROCK) != 0

def worm_solid(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return True
    return (mflat[data[y*W+x]] & BACK) == 0

# ---- cossin ----
txt = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "baked.h")).read()
CSX = [int(v) for v in re.search(r"cs_x\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]
CSY = [int(v) for v in re.search(r"cs_y\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]

# ---- carve chamber (same as ROM WORM build: idx160 air to worm+bullets) ----
for y in range(156, 180):
    for x in range(100, 180):
        sp(x, y, 160)

# ---- reactions (slice 2) ----
COL = {0: [(-1,-4),(0,-4),(1,-4)],
       1: [(1,-3),(1,-2),(1,-1),(1,0),(1,1),(1,2),(1,3)],
       2: [(-1,4),(0,4),(1,4)],
       3: [(-1,-3),(-1,-2),(-1,-1),(-1,0),(-1,1),(-1,2),(1-0,3)]}
COL[3] = [(-1,-3),(-1,-2),(-1,-1),(-1,0),(-1,1),(-1,2),(-1,3)]
RF_DOWN, RF_LEFT, RF_UP, RF_RIGHT = 0, 1, 2, 3

def rc(x, y, d):
    n = 0
    for dx, dy in COL[d]:
        if worm_solid(x+dx, y+dy): n += 1
    return n

# ---- worm ----
FRIC_M, FRIC_D = 89, 100
GRAVITY, JUMP = 1500, 56064
WALK, MAXV, BOUNCE = 3000, 29184, 53248
AACC, MAIMV, AF_M, AF_D = 4000, 70000, 83, 100
AMINR, AMAXR, AMAXL, AMINL = 64, 116, 12, 64
ANIM = [0, 7, 0, 14]

w = type("W", (), {})()
sx = 115; sy = 175
for y in range(156, 180):
    if rc(sx, y, RF_UP) >= 1:
        sy = y; break
w.pos_x = sx << 16; w.pos_y = sy << 16
w.vel_x = w.vel_y = 0
w.aim_angle = 64 << 16; w.aim_speed = 0
w.direction = 1; w.able_jump = 1; w.animate = 0; w.cur = 0
print("SPAWN x=%d y=%d" % (sx, sy))

# ---- weapons ----
W_GRAVITY, W_SPEED, W_DIST = 1000, 200, 12000
W_DELAY, W_LOADING, W_AMMO = 20, 225, 5
W_BOUNCE, W_TTIME, W_TTIMEV, W_SPLINTER, W_DIRT = 30, 80, 20, 5, 4
N_TTIME, N_DIRT = 1, 4

wwp = [{"ammo": W_AMMO, "delay": 0, "loading": 0} for _ in range(5)]
wobjs = {}
nobjs = {}
prev_fire = 0

sprite = lambda f: largepix[f*256:(f+1)*256]

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
                if c == 6 and (mf & (DIRT|DIRT2)):
                    v = t_frame[(my & 15)*16 + (mx & 15)]
                    data[i] = v
                elif c == 1:
                    if mf & DIRT2: data[i] = 2
                    elif mf & DIRT: data[i] = 1
            else:
                if c in (10, 6):
                    if mf & BACK:
                        data[i] = t_frame[(my & 15)*16 + (mx & 15)]
                elif c == 2:
                    if mf & BACK: data[i] = 2
                elif c == 1:
                    if mf & BACK: data[i] = 1

def correct_shadow(x1, y1, x2, y2):
    if x1 < 0: x1 = 0
    if y1 < 3: y1 = 3
    if x2 > W-3: x2 = W-3
    if y2 > H: y2 = H
    for x in range(x1, x2):
        for y in range(y1, y2):
            i = y*W + x
            j = (y-3)*W + x+3
            pix = data[i]
            if (mflat[pix] & SEE) and (mflat[data[j]] & M_DIRTROCK):
                data[i] = (pix + 4) & 0xFF
            elif 164 <= pix <= 167 and not (mflat[data[j]] & M_DIRTROCK):
                data[i] = (pix - 4) & 0xFF

def alloc(pool, cap):
    for i in range(cap):
        if i not in pool:
            pool[i] = True
            return i
    return cap - 1

def blow_up(i):
    o = wobjs.pop(i)
    ppx, ppy = o["x"] >> 16, o["y"] >> 16
    for _ in range(W_SPLINTER):
        angle = rnd.bounded(128)
        colorSub = rnd.bounded(2)
        realSpeed = 400 - rnd.bounded(800)
        vx = tdiv(CSX[angle] * realSpeed, 100)
        vy = tdiv(CSY[angle] * realSpeed, 100)
        vx += rnd.bounded(20000) - 10000
        vy += rnd.bounded(20000) - 10000
        j = alloc(nobjs, 512)
        nobjs[j] = {"x": i32((ppx << 16) + vx), "y": i32((ppy << 16) + vy),
                    "vx": i32(vx), "vy": i32(vy),
                    "cur": -colorSub, "time": N_TTIME}
    dirt_effect(W_DIRT, ppx - 7, ppy - 7)
    correct_shadow(ppx - 10, ppy - 10, ppx + 11, ppy + 11)

def fire_wobj():
    aa = (w.aim_angle >> 16) & 127
    fx = i32(CSX[aa] * 5 + w.pos_x)
    fy = i32(CSY[aa] * 5 + w.pos_y - (1 << 16))
    fvx = tdiv(w.vel_x * 100, W_SPEED)
    fvy = tdiv(w.vel_y * 100, W_SPEED)
    vx = i32(tdiv(CSX[aa] * W_SPEED, 100) + fvx)
    vy = i32(tdiv(CSY[aa] * W_SPEED, 100) + fvy)
    vx = i32(vx + rnd.bounded(W_DIST * 2) - W_DIST)
    vy = i32(vy + rnd.bounded(W_DIST * 2) - W_DIST)
    tl = W_TTIME - rnd.bounded(W_TTIMEV)
    i = alloc(wobjs, 256)
    wobjs[i] = {"x": fx, "y": fy, "vx": vx, "vy": vy,
                "cur": 0, "time": tl, "owner": 0}

def worm_weapons():
    for ww in wwp:
        if ww["delay"] >= 0:
            ww["delay"] -= 1
    ww = wwp[0]
    if ww["ammo"] <= 0:
        lt = (100 * W_LOADING) // 100
        if lt == 0: lt = 1
        ww["loading"] = lt
        ww["ammo"] = W_AMMO
    if ww["loading"] > 0:
        ww["loading"] -= 1

def try_fire(fire_held):
    global prev_fire
    ww = wwp[0]
    if fire_held and not prev_fire and ww["loading"] == 0 and ww["delay"] <= 0:
        ww["ammo"] -= 1
        ww["delay"] = W_DELAY
        fire_wobj()
    prev_fire = fire_held

def process_wobjects():
    for i in sorted(list(wobjs.keys())):
        o = wobjs[i]
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
        ix = o["x"] >> 16; iy = o["y"] >> 16
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0 or nx >= W or dirtrock(nx, iy):
            o["vx"] = i32(-tdiv(o["vx"] * W_BOUNCE, 100))
            o["vy"] = i32(tdiv(o["vy"] * 4, 5))
        if ny < 0 or ny >= H or dirtrock(ix, ny):
            o["vy"] = i32(-tdiv(o["vy"] * W_BOUNCE, 100))
            o["vx"] = i32(tdiv(o["vx"] * 4, 5))
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0: o["x"] = 0
        if ny < 0: o["y"] = 0
        if nx >= W: o["x"] = (W - 1) << 16
        if ny >= H: o["y"] = (H - 1) << 16
        hit = nx < 0 or nx >= W or ny < 0 or ny >= H or dirtrock(nx, ny)
        if not hit:
            o["vy"] = i32(o["vy"] + W_GRAVITY)
        o["time"] -= 1
        if o["time"] < 0:
            blow_up(i)

def process_nobjects():
    for i in sorted(list(nobjs.keys())):
        o = nobjs[i]
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
        ix = o["x"] >> 16; iy = o["y"] >> 16
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0 or nx >= W or dirtrock(nx, iy):
            o["vx"] = i32(tdiv(-o["vx"] * 100, 100))
            o["vy"] = i32(tdiv(o["vy"] * 4, 5))
        if ny < 0 or ny >= H or dirtrock(ix, ny):
            o["vy"] = i32(tdiv(-o["vy"] * 100, 100))
            o["vx"] = i32(tdiv(o["vx"] * 4, 5))
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0: o["x"] = 0
        if ny < 0: o["y"] = 0
        if nx >= W: o["x"] = W << 16
        if ny >= H: o["y"] = H << 16
        hit = nx < 0 or nx >= W or ny < 0 or ny >= H or dirtrock(nx, ny)
        if hit:
            o["vx"] = 0; o["vy"] = 0
        o["time"] -= 1
        if o["time"] <= 0:
            dirt_effect(N_DIRT, (o["x"] >> 16) - 7, (o["y"] >> 16) - 7)
            correct_shadow((o["x"] >> 16) - 10, (o["y"] >> 16) - 10,
                          (o["x"] >> 16) + 11, (o["y"] >> 16) + 11)
            del nobjs[i]

prev_jump = 0
def worm_process(L, R, U, D, J, F, cycle):
    global prev_jump
    nx = i32(w.pos_x + w.vel_x); ny = i32(w.pos_y + w.vel_y)
    inx = nx >> 16; iny = ny >> 16
    reacts = [0]*4
    for d in range(4):
        reacts[d] = rc(inx, iny, d)
        if inx < 4: reacts[RF_RIGHT] += 5
        elif inx > W - 5: reacts[RF_LEFT] += 5
        if iny < 5: reacts[RF_DOWN] += 5
        elif iny > H - 6: reacts[RF_UP] += 5
    ru, rd, rl, rr = reacts[RF_UP], reacts[RF_DOWN], reacts[RF_LEFT], reacts[RF_RIGHT]
    if rd < 2 and ru > 0 and (rl > 0 or rr > 0):
        w.pos_y = i32(w.pos_y - (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    if reacts[RF_UP] < 2 and reacts[RF_DOWN] > 0 and (reacts[RF_LEFT] > 0 or reacts[RF_RIGHT] > 0):
        w.pos_y = i32(w.pos_y + (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    # aiming
    if w.aim_speed != 0:
        w.aim_angle = i32(w.aim_angle + w.aim_speed)
        if not U and not D:
            w.aim_speed = tdiv(w.aim_speed * AF_M, AF_D)
        if w.direction == 1:
            if (w.aim_angle >> 16) > AMAXR: w.aim_speed = 0; w.aim_angle = AMAXR << 16
            if (w.aim_angle >> 16) < AMINR: w.aim_speed = 0; w.aim_angle = AMINR << 16
        else:
            if (w.aim_angle >> 16) < AMAXL: w.aim_speed = 0; w.aim_angle = AMAXL << 16
            if (w.aim_angle >> 16) > AMINL: w.aim_speed = 0; w.aim_angle = AMINL << 16
    if U:
        if w.direction == 0:
            if w.aim_speed < MAIMV: w.aim_speed = i32(w.aim_speed + AACC)
        else:
            if w.aim_speed > -MAIMV: w.aim_speed = i32(w.aim_speed - AACC)
    if D:
        if w.direction == 1:
            if w.aim_speed < MAIMV: w.aim_speed = i32(w.aim_speed + AACC)
        else:
            if w.aim_speed > -MAIMV: w.aim_speed = i32(w.aim_speed - AACC)
    # jump task
    if J and not prev_jump and reacts[RF_UP] > 0 and w.able_jump:
        w.vel_y = i32(w.vel_y - JUMP); w.able_jump = 0
    if not J: w.able_jump = 1
    prev_jump = J
    # weapons + fire (before physics, upstream order)
    worm_weapons()
    try_fire(F)
    # physics
    if reacts[RF_UP] > 0:
        w.vel_x = tdiv(w.vel_x * FRIC_M, FRIC_D)
    ax = abs(w.vel_x); ay = abs(w.vel_y)
    rh = reacts[RF_LEFT if w.vel_x >= 0 else RF_RIGHT]
    rv = reacts[RF_UP if w.vel_y >= 0 else RF_DOWN]
    if w.vel_x and rh:
        w.vel_x = i32(-tdiv(w.vel_x, 3)) if ax > BOUNCE else 0
    if w.vel_y and rv:
        w.vel_y = i32(-tdiv(w.vel_y, 3)) if ay > BOUNCE else 0
    if reacts[RF_UP] == 0:
        w.vel_y = i32(w.vel_y + GRAVITY)
    if reacts[RF_LEFT if w.vel_x >= 0 else RF_RIGHT] < 2:
        w.pos_x = i32(w.pos_x + w.vel_x)
    if reacts[RF_UP if w.vel_y >= 0 else RF_DOWN] < 2:
        w.pos_y = i32(w.pos_y + w.vel_y)
    # movement
    w.animate = 1 if (L or R) else 0
    if L and not R:
        if w.vel_x > -MAXV: w.vel_x = i32(w.vel_x - WALK)
        if w.direction != 0:
            w.aim_speed = 0
            if w.aim_angle >= (64<<16): w.aim_angle = i32((128<<16) - w.aim_angle)
            w.direction = 0
    if not L and R:
        if w.vel_x < MAXV: w.vel_x = i32(w.vel_x + WALK)
        if w.direction != 1:
            w.aim_speed = 0
            if w.aim_angle <= (64<<16): w.aim_angle = i32((128<<16) - w.aim_angle)
            w.direction = 1
    af = ((cycle & 31) >> 3) if w.animate else 0
    x = (w.aim_angle >> 16) - 12
    if w.direction != 0: x -= 49
    x >>= 3
    x = 0 if x < 0 else (6 if x > 6 else x)
    if w.direction != 0: x = 6 - x
    w.cur = x + ANIM[af]

def fnv(b):
    h = 2166136261
    for v in b:
        h = ((h ^ v) * 16777619) & 0xFFFFFFFF
    return h

for frame in range(840):
    ph = frame % 420
    R = 1 if ph < 120 else 0
    J = 1 if ph == 120 else 0
    L = 1 if (ph >= 210 and ph < 330) else 0
    U = 1 if ph >= 330 else 0
    D = 1 if (ph >= 100 and ph < 118) else 0
    F = 1 if (ph in (110, 135, 160, 185, 210)) else 0
    process_wobjects()
    if 108 <= frame <= 265 and frame % 5 == 2:
        for j in sorted(wobjs):
            o = wobjs[j]
            print("WT f=%d i=%d x=%d y=%d vx=%d vy=%d t=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["time"]))
    process_nobjects()
    worm_process(L, R, U, D, J, F, frame)
    if ph in (112, 137, 162, 180) or frame % 60 == 59:
        th = fnv(data)
        wc = len(wobjs); nc = len(nobjs)
        oh = 2166136261
        for j in sorted(wobjs):
            o = wobjs[j]
            bb = struct.pack("<7i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"], o["owner"])
            oh = fnv(bb) ^ oh if False else oh
            for v in bb:
                oh = ((oh ^ v) * 16777619) & 0xFFFFFFFF
        for j in sorted(nobjs):
            o = nobjs[j]
            bb = struct.pack("<6i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"])
            for v in bb:
                oh = ((oh ^ v) * 16777619) & 0xFFFFFFFF
        print("CK frame=%d terr=%08x rng=%08x,%08x wc=%d nc=%d obj=%08x wpos=%d,%d wvel=%d,%d ammo=%d dly=%d ld=%d aim=%d" % (
            frame, th, rnd.x, rnd.c, wc, nc, oh,
            w.pos_x >> 16, w.pos_y >> 16, w.vel_x, w.vel_y,
            wwp[0]["ammo"], wwp[0]["delay"], wwp[0]["loading"], w.aim_angle >> 16))
