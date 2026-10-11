#!/usr/bin/env python3
"""Host mirror v3: two worms, DumbLieroAI, per-worm viewports, KillEmAll.

Slices 1-4 physics identical to weapons_ref.py (trace-exact committed), now
multi-worm per upstream: AI phase loop before frame process, per-worm
weapons/shells/spawn logic, sobject damage loop over worms, collideWithObjects,
checkRespawnPosition with enemy coords, two viewport shakes (2 rand each).
Field/param tables baked from TC by tools/params2.py (shared with ROM).
"""
import os, re, struct, json, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bake_assets import load_cfg, read_tga, generate, TC, LEVEL_W, LEVEL_H

W, H = LEVEL_W, LEVEL_H
DIRT, DIRT2, ROCK, BACK, SEE, WORMF = 1, 2, 4, 8, 16, 32
M_DIRTROCK = DIRT | DIRT2 | ROCK

sw, sh, pal, smallpix = read_tga(os.path.join(TC, "sprites", "small.tga"))
lw, lh, _, largepix = read_tga(os.path.join(TC, "sprites", "large.tga"))
mats, textures = load_cfg(os.path.join(TC, "tc.cfg"))
mflat = bytes(mats)

terrain, rnd = generate(mats, textures, pal, largepix)
_rnd_b = rnd.bounded
import inspect as _insp
def _rdbg(m):
    v = _rnd_b(m)
    if 595 <= frame_g[0] <= 605:
        st = _insp.stack()
        ln = 0
        for fr in st[1:4]:
            if fr.filename.endswith("duel_ref.py"): ln = fr.lineno; break
        print("RR f=%d m=%d v=%d x=%08x L=%d" % (frame_g[0], m, v, rnd.x, ln))
    return v
rnd.bounded = _rdbg
data = bytearray(terrain)

def sp(x, y, v):
    if 0 <= x < W and 0 <= y < H:
        data[y*W+x] = v & 0xFF
def dirtrock(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return True
    return (mflat[data[y*W+x]] & M_DIRTROCK) != 0
def inside(x, y): return 0 <= x < W and 0 <= y < H
def rockq(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return True
    return (mflat[data[y*W+x]] & ROCK) != 0
def anydirt(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return False
    return (mflat[data[y*W+x]] & (DIRT|DIRT2)) != 0
def back(x, y):
    if x < 0 or x >= W or y < 0 or y >= H: return False
    return (mflat[data[y*W+x]] & BACK) != 0

P = json.load(open(os.path.join(HERE, "..", "ref", "combat_params.json")))
WP, NP, SP, IDX = P["w"], P["n"], P["s"], P["idx"]
AI_K = P["ai_k"]                       # [2][7] k[state][ctrl]; ctrl Up,Dn,L,R,Fire,Chg,Jump
C = P["const"]

txt = open(os.path.join(HERE, "..", "assets", "baked.h")).read()
CSX = [int(v) for v in re.search(r"cs_x\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]
CSY = [int(v) for v in re.search(r"cs_y\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]

BLOOD = IDX["blood"]; SHELLS = IDX["shells"]; PART2 = IDX["part2"]
DIRTN = IDX["dirt"];  WPARTS = 0
SMALLEXPC = IDX["smallexp"]
DIRTBALL = 4; HANDGUN = 17

WK = dict(gravity=0,speed=1,distribution=2,parts=3,recoil=4,multSpeed=5,delay=6,loadingTime=7,
          ammo=8,dirtEffect=9,fireCone=10,bounce=11,timeToExplo=12,timeToExploV=13,hitDamage=14,
          bloodOnHit=15,startFrame=16,numFrames=17,shotType=18,colorBullets=19,splinterAmount=20,
          splinterColour=21,splinterType=22,splinterScatter=23,detectDistance=24,blowAway=25,
          addSpeed=26,affectByWorm=27,wormExplode=28,explGround=29,wormCollide=30,collideWithObjects=31,
          affectByExplosions=32,loopAnim=33,chainExplosion=34,leaveShells=35,leaveShellDelay=36,
          objTrailType=37,objTrailDelay=38,partTrailType=39,partTrailObj=40,partTrailDelay=41,createOnExp=42)
NK = dict(gravity=0,speed=1,speedV=2,distribution=3,bounce=4,blowAway=5,hitDamage=6,bloodOnHit=7,
          startFrame=8,numFrames=9,colorBullets=10,dirtEffect=11,timeToExplo=12,timeToExploV=13,
          splinterAmount=14,splinterColour=15,splinterType=16,detectDistance=17,bloodTrailDelay=18,
          leaveObjDelay=19,leaveObj=20,wormExplode=21,explGround=22,wormDestroy=23,drawOnMap=24,
          affectByExplosions=25,bloodTrail=26,createOnExp=27)
SK = dict(startFrame=0,numFrames=1,animDelay=2,detectRange=3,damage=4,blowAway=5,dirtEffect=6,
          shake=7,flash=8,startSound=9,numSounds=10)
AI  = dict(up=0,down=1,left=2,right=3,fire=4,change=5,jump=6)

# ---- chamber (same carve as slices 1-4) ----
for y in range(156, 180):
    for x in range(100, 180):
        sp(x, y, 160)

COL = {0: [(-1,-4),(0,-4),(1,-4)],
       1: [(1,-3),(1,-2),(1,-1),(1,0),(1,1),(1,2),(1,3)],
       2: [(-1,4),(0,4),(1,4)],
       3: [(-1,-3),(-1,-2),(-1,-1),(-1,0),(-1,1),(-1,2),(-1,3)]}
RF_DOWN, RF_LEFT, RF_UP, RF_RIGHT = 0, 1, 2, 3

def rc(x, y, d):
    n = 0
    for dx, dy in COL[d]:
        if (x+dx < 0 or x+dx >= W or y+dy < 0 or y+dy >= H or
            (mflat[data[(y+dy)*W+(x+dx)]] & BACK) == 0): n += 1
    return n

FRIC_M, FRIC_D = 89, 100
GRAVITY, JUMP = 1500, 56064
WALK, MAXV, BOUNCE = 3000, 29184, 53248
AACC, MAIMV, AF_M, AF_D = 4000, 70000, 83, 100
AMINR, AMAXR, AMAXL, AMINL = 64, 116, 12, 64
ANIM = [0, 7, 0, 14]
VW, VH = 320, 240          # viewport size (match ROM macros)

def i32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v
def tdiv(a, b):
    return int(a / b) if (a < 0) != (b < 0) and a % b else a // b

# ---- worms ----
def new_worm(idx, sx, sy):
    d = type("W", (), {})()
    d.index = idx
    d.pos_x = sx << 16; d.pos_y = sy << 16
    d.vel_x = d.vel_y = 0
    d.aim_angle = 64 << 16
    d.direction = 1
    d.able_jump = 1; d.animate = 0; d.cur = 0
    d.health = 100; d.lives = 5; d.visible = 1; d.ready = 0
    d.killed_timer = 0
    d.lr_x = sx - 80; d.lr_y = sy - 80
    d.prev_fire = 0; d.prev_change = 0; d.prev_left = 0; d.prev_right = 0; d.prev_jump = 0
    d.leave_shell_timer = 0
    d.wwp = [{"ammo": WP[w][WK["ammo"]], "delay": 0, "loading": 0} for w in WEAPONS_LIST[idx]]
    d.wslot = 0; d.weapon = WEAPONS_LIST[idx][0]
    d.last_killed_by = -1
    d.kills = 0
    d.script_state = (0,0,0,0,0,0,0)
    d.ai_reacts = [0,0,0,0]
    return d

WEAPONS_LIST = [[DIRTBALL, HANDGUN], [DIRTBALL, HANDGUN]]

def find_floor(sx):
    for y in range(156, 180):
        if rc(sx, y, RF_UP) >= 1: return y
    return 179

s0 = 115; s1 = 150
y0 = find_floor(s0); y1 = find_floor(s1)
worms = [new_worm(0, s0, y0), new_worm(1, s1, y1)]
print("SPAWN i=0 x=%d y=%d" % (s0, y0))
print("SPAWN i=1 x=%d y=%d" % (s1, y1))

# ---- global state ----
wobjs = {}; nobjs = {}; sobjs = {}; bobjs = []
MAX_WOBJ, MAX_NOBJ, MAX_SOBJ, MAX_BOBJ = 256, 512, 700, 700
frame_g = [0]
g_screen_flash = 0
g_shake = [0, 0]              # per-viewport (worm idx)
g_last_killed = -1            # game.lastKilledIdx
v_x = [0, 0]; v_y = [0, 0]   # viewport top-left (int px)

sprite = lambda f: largepix[f*256:(f+1)*256]

def dirt_effect(eff, x, y):
    if 595 <= frame_g[0] <= 610: print("DS f=%d e=%d x=%d y=%d" % (frame_g[0], eff, x, y))
    tx = textures[eff]
    t_frame = sprite(tx["sFrame"] + rnd.bounded(tx["rFrame"]))
    m_frame = sprite(tx["mFrame"])
    for cy in range(16):
        for cx in range(16):
            mx, my = x+cx, y+cy
            if not (0 <= mx < W and 0 <= my < H-1): continue
            c = m_frame[cy*16+cx]
            i = my*W+mx
            mf = mflat[data[i]]
            if tx["nDrawBack"]:
                if c == 6 and (mf & (DIRT|DIRT2)): data[i] = t_frame[(my & 15)*16 + (mx & 15)]
                elif c == 1:
                    if mf & DIRT2: data[i] = 2
                    elif mf & DIRT: data[i] = 1
            else:
                if c in (10, 6):
                    if mf & BACK: data[i] = t_frame[(my & 15)*16 + (mx & 15)]
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

def blit_small(f, x, y, wd=7, h=7):
    if f < 0 or f*49 >= len(smallpix): return
    for cy in range(h):
        my = y + cy
        if my < 0 or my >= H: continue
        for cx in range(wd):
            mx = x + cx
            if mx < 0 or mx >= W: continue
            c = smallpix[f*49 + cy*wd + cx]
            if not c: continue
            i = my*W + mx
            n = c if (mflat[data[i]] & (DIRT|DIRT2|BACK)) else (c + 3)
            data[i] = n & 0xFF

def alloc(pool, cap):
    for i in range(cap):
        if i not in pool:
            pool[i] = True; return i
    return cap - 1

# ---- nobject create ----
def nobj_core(t, vx, vy, px_, py_, color, owner):
    p = NP[t]
    i = alloc(nobjs, MAX_NOBJ)
    if p[NK["startFrame"]] > 0: cf = rnd.bounded(p[NK["numFrames"]] + 1)
    elif color != 0:            cf = color
    else:                       cf = p[NK["colorBullets"]]
    tl = p[NK["timeToExplo"]]
    if p[NK["timeToExploV"]]: tl -= rnd.bounded(p[NK["timeToExploV"]])
    nobjs[i] = {"x": i32(px_), "y": i32(py_), "vx": i32(vx), "vy": i32(vy),
                "cur": cf, "time": tl, "type": t, "owner": owner}
    return i

def nobj_create1(t, vx, vy, px_, py_, color, owner):
    d = NP[t][NK["distribution"]]
    if d:
        vx += d - rnd.bounded(d*2)
        vy += d - rnd.bounded(d*2)
    nobj_core(t, vx, vy, px_, py_, color, owner)

def nobj_create2(t, angle, bvx, bvy, px_, py_, color, owner):
    p = NP[t]
    rs = p[NK["speed"]] - rnd.bounded(p[NK["speedV"]])
    vx = bvx + tdiv(CSX[angle & 127] * rs, 100)
    vy = bvy + tdiv(CSY[angle & 127] * rs, 100)
    d = p[NK["distribution"]]
    if d:
        vx += rnd.bounded(d*2) - d
        vy += rnd.bounded(d*2) - d
    i = nobj_core(t, vx, vy, px_, py_, color, owner)
    nobjs[i]["x"] = i32(nobjs[i]["x"] + nobjs[i]["vx"])
    nobjs[i]["y"] = i32(nobjs[i]["y"] + nobjs[i]["vy"])

# ---- sobject create (damage loop over all worms, per upstream) ----
def sobj_create(t, x, y, owner_idx):
    global g_screen_flash
    p = SP[t]
    i = alloc(sobjs, MAX_SOBJ)
    if p[SK["startSound"]] >= 0: rnd.bounded(p[SK["numSounds"]])
    sobjs[i] = {"x": x-8, "y": y-8, "cur": 0, "ad": p[SK["animDelay"]], "type": t}
    if p[SK["flash"]] > g_screen_flash: g_screen_flash = p[SK["flash"]]
    # viewport shake set (2 viewports)
    for vi in range(2):
        if x > v_x[vi] and x < v_x[vi] + VW and y > v_y[vi] and y < v_y[vi] + VH:
            shk = p[SK["shake"]] << 16
            if shk > g_shake[vi]: g_shake[vi] = shk
    damage = p[SK["damage"]]
    if damage > 0:
        dr = p[SK["detectRange"]]
        for w in worms:
            wix = w.pos_x >> 16; wiy = w.pos_y >> 16
            if wix < x+dr and wix > x-dr and wiy < y+dr and wiy > y-dr:
                delta = wix - x
                power = dr - abs(delta)
                power_sum = power
                if -131072 < w.vel_x < 131072:
                    if delta > 0: w.vel_x = i32(w.vel_x + p[SK["blowAway"]] * power)
                    else:         w.vel_x = i32(w.vel_x - p[SK["blowAway"]] * power)
                delta = wiy - y
                power = dr - abs(delta)
                power_sum = (power_sum + power) // 2
                if -131072 < w.vel_y < 131072:
                    if delta > 0: w.vel_y = i32(w.vel_y + p[SK["blowAway"]] * power)
                    else:         w.vel_y = i32(w.vel_y - p[SK["blowAway"]] * power)
                z = damage * power_sum
                if dr: z = tdiv(z, dr)
                if w.health > 0:
                    do_damage(w, z, owner_idx)
                    blood_amount = power_sum   # settings blood=100
                    for _ in range(blood_amount):
                        angle = rnd.bounded(128)
                        nobj_create2(BLOOD, angle, tdiv(w.vel_x,3), tdiv(w.vel_y,3),
                                     w.pos_x, w.pos_y, 0, w.index)
                    if rnd.bounded(3) == 0: rnd.bounded(3)
        obj_blow = p[SK["blowAway"]] // 3
        for k in sorted(list(wobjs.keys())):
            o = wobjs[k]
            if WP[o["type"]][WK["affectByExplosions"]]:
                ox = o["x"] >> 16; oy = o["y"] >> 16
                if ox < x+dr and ox > x-dr and oy < y+dr and oy > y-dr:
                    d = ox - x; pw = dr - abs(d)
                    if pw > 0:
                        if d > 0: o["vx"] = i32(o["vx"] + obj_blow*pw)
                        elif d < 0: o["vx"] = i32(o["vx"] - obj_blow*pw)
                    d = oy - y; pw = dr - abs(d)
                    if pw > 0:
                        if d > 0: o["vy"] = i32(o["vy"] + obj_blow*pw)
                        else: o["vy"] = i32(o["vy"] - obj_blow*pw)
                    if WP[o["type"]][WK["chainExplosion"]]: wobj_blow_up(k, owner_idx)
        for k in sorted(list(nobjs.keys())):
            o = nobjs[k]
            if NP[o["type"]][NK["affectByExplosions"]]:
                ox = o["x"] >> 16; oy = o["y"] >> 16
                if ox < x+dr and ox > x-dr and oy < y+dr and oy > y-dr:
                    d = ox - x; pw = dr - abs(d)
                    if pw > 0:
                        if d > 0: o["vx"] = i32(o["vx"] + obj_blow*pw)
                        elif d < 0: o["vx"] = i32(o["vx"] - obj_blow*pw)
                    d = oy - y; pw = dr - abs(d)
                    if pw > 0:
                        if d > 0: o["vy"] = i32(o["vy"] + obj_blow*pw)
                        else: o["vy"] = i32(o["vy"] - obj_blow*pw)
        width = dr // 2
        for py_ in range(max(y-width,0), min(y+width+1,H)):
            for px_ in range(max(x-width,0), min(x+width+1,W)):
                if anydirt(px_, py_) and rnd.bounded(8) == 0:
                    pix = data[py_*W+px_]
                    angle = rnd.bounded(128)
                    nobj_create2(PART2, angle, 0, 0, px_ << 16, py_ << 16, pix, owner_idx)
    if p[SK["dirtEffect"]] >= 0:
        dirt_effect(p[SK["dirtEffect"]], x-7, y-7)
        correct_shadow(x-10, y-10, x+11, y+11)

# ---- wobject blow up ----
def wobj_blow_up(i, cause_idx):
    o = wobjs.pop(i)
    p = WP[o["type"]]
    px_ = o["x"] >> 16; py_ = o["y"] >> 16
    velX, velY = o["vx"], o["vy"]
    if p[WK["createOnExp"]] >= 0: sobj_create(p[WK["createOnExp"]], px_, py_, cause_idx)
    splinters = p[WK["splinterAmount"]]
    if splinters > 0 and p[WK["splinterScatter"]] == 0:
        for _ in range(splinters):
            angle = rnd.bounded(128)
            colorSub = rnd.bounded(2)
            nobj_create2(p[WK["splinterType"]], angle, 0, 0, px_<<16, py_<<16,
                         p[WK["splinterColour"]] - colorSub, cause_idx)
    elif splinters > 0:
        for _ in range(splinters):
            colorSub = rnd.bounded(2)
            nobj_create1(p[WK["splinterType"]], velX, velY, px_<<16, py_<<16,
                         p[WK["splinterColour"]] - colorSub, cause_idx)
    if p[WK["dirtEffect"]] >= 0:
        dirt_effect(p[WK["dirtEffect"]], px_-7, py_-7)
        correct_shadow(px_-10, py_-10, px_+11, py_+11)

# ---- damage / death ----
def do_damage(w, amount, by_idx):
    if amount > 0:
        w.health -= amount
        print("DMG f=%d w=%d a=%d hp=%d by=%d" % (frame_g[0], w.index, amount, w.health, by_idx))
        if w.health <= 0: w.last_killed_by = by_idx

def worm_die(w):
    global g_last_killed
    w.leave_shell_timer = 0
    rnd.bounded(3)                       # death snd pick
    old_last_killed = g_last_killed
    g_last_killed = w.index
    if w.last_killed_by >= 0 and w.last_killed_by != w.index:
        worms[w.last_killed_by].kills += 1
    w.visible = 0
    w.killed_timer = 150
    w.lives -= 1
    print("DIE f=%d w=%d lives=%d by=%d kl=%d,%d" % (
        frame_g[0], w.index, w.lives, w.last_killed_by, old_last_killed, g_last_killed))
    for _ in range(120):
        angle = rnd.bounded(128)
        nobj_create2(BLOOD, angle, tdiv(w.vel_x,3), tdiv(w.vel_y,3), w.pos_x, w.pos_y, 0, w.index)
    a = 7
    while a <= 105:
        nobj_create2(w.index, a + rnd.bounded(14), tdiv(w.vel_x,3), tdiv(w.vel_y,3),
                     w.pos_x, w.pos_y, 0, w.index)
        a += 14
    w.prev_fire = 0

# ---- respawn ----
def check_respawn_pos(enemy_x, enemy_y, oldx, oldy, x, y):
    dxL = oldx; dyL = oldy - y
    if abs(dxL) <= C["WormMinSpawnDistLast"] and abs(dyL) <= C["WormMinSpawnDistLast"]: return False
    if abs(enemy_x - x) <= C["WormMinSpawnDistEnemy"] and abs(enemy_y - y) <= C["WormMinSpawnDistEnemy"]: return False
    x1 = max(x-3, 0); y1 = max(y-4, 0)
    x2 = min(x+3, W-1); y2 = min(y+4, H-1)
    for j in range(y1, y2):
        for k in range(x1, x2):
            if rockq(k, j): return False
    return True

def worm_begin_respawn(w):
    temp_x = w.pos_x >> 16; temp_y = w.pos_y >> 16
    enemy_x, enemy_y = temp_x, temp_y
    if len(worms) == 2:
        e = worms[w.index ^ 1]
        enemy_x = e.pos_x >> 16; enemy_y = e.pos_y >> 16
    trials = 0
    while True:
        sx2 = C["WormSpawnRectX"] + rnd.bounded(C["WormSpawnRectW"])
        sy2 = C["WormSpawnRectY"] + rnd.bounded(C["WormSpawnRectH"])
        while sy2 + 4 < H and back(sx2, sy2 + 4): sy2 += 1
        trials += 1
        if trials >= 50000: break
        if check_respawn_pos(enemy_x, enemy_y, temp_x, temp_y, sx2, sy2): break
    w.pos_x = sx2 << 16; w.pos_y = sy2 << 16
    w.lr_x = temp_x - 80; w.lr_y = temp_y - 80
    w.killed_timer = -1

def worm_dead_process(w, fire_held):
    if fire_held: w.ready = 1
    if w.killed_timer > 0: w.killed_timer -= 1
    if w.killed_timer == 0: worm_begin_respawn(w)
    if w.killed_timer < 0:
        dest_x = (w.pos_x >> 16) - 80
        dest_y = (w.pos_y >> 16) - 80
        for _ in range(4):
            if w.lr_x < dest_x: w.lr_x += 1
            elif w.lr_x > dest_x: w.lr_x -= 1
            if w.lr_y < dest_y: w.lr_y += 1
            elif w.lr_y > dest_y: w.lr_y -= 1
        w.lr_x = max(0, min(w.lr_x, W-VW))
        w.lr_y = max(0, min(w.lr_y, H-VH))
        dest_x = max(0, min(dest_x, W-VW))
        dest_y = max(0, min(dest_y, H-VH))
        if dest_x-5 < w.lr_x < dest_x+5 and dest_y-5 < w.lr_y < dest_y+5 and w.ready:
            ipx = w.pos_x >> 16; ipy = w.pos_y >> 16
            dirt_effect(0, ipx-7, ipy-7)
            correct_shadow(ipx-10, ipy-10, ipx+11, ipy+11)
            w.ready = 0
            w.visible = 1
            w.vel_x = 0; w.vel_y = 0
            w.health = 100
            print("RSP f=%d w=%d x=%d y=%d" % (frame_g[0], w.index, ipx, ipy))
            if rnd.next() & 1: w.aim_angle = 32 << 16; w.direction = 0
            else:              w.aim_angle = 96 << 16; w.direction = 1

# ---- worm sprite hit ----
def check_spec_worm_hit(w, x, y, dist):
    if not w.visible: return False
    f = 16 + w.cur
    if f >= lw: f = 16
    ws = largepix[f*256:(f+1)*256]
    dX = x - (w.pos_x >> 16) + 7
    dY = y - (w.pos_y >> 16) + 5
    x1 = max(dX - dist, 0); y1 = max(dY - dist, 0)
    x2 = min(dX + dist + 1, 16); y2 = min(dY + dist + 1, 16)
    for cy in range(y1, y2):
        for cx in range(x1, x2):
            pi = cx if w.direction == 1 else 15 - cx
            if mflat[ws[cy*16+pi]] & WORMF: return True
    return False

# ---- fire ----
def fire_wobj(w):
    p = WP[w.weapon]
    aa = (w.aim_angle >> 16) & 127
    fx = i32(CSX[aa] * (p[WK["detectDistance"]] + 5) + w.pos_x)
    fy = i32(CSY[aa] * (p[WK["detectDistance"]] + 5) + w.pos_y - (1 << 16))
    if p[WK["leaveShells"]] > 0 and rnd.bounded(p[WK["leaveShells"]]) == 0:
        w.leave_shell_timer = p[WK["leaveShellDelay"]]
    speed = p[WK["speed"]]
    fvx = fvy = 0
    if p[WK["affectByWorm"]]:
        if speed < 100: speed = 100
        fvx = tdiv(w.vel_x * 100, speed)
        fvy = tdiv(w.vel_y * 100, speed)
    for _ in range(p[WK["parts"]]):
        vx = tdiv(CSX[aa] * speed, 100) + fvx
        vy = tdiv(CSY[aa] * speed, 100) + fvy
        d = p[WK["distribution"]]
        if d:
            r1 = rnd.bounded(d*2); vx += r1 - d
            r2 = rnd.bounded(d*2); vy += r2 - d
        cur = 0
        if p[WK["startFrame"]] >= 0:
            if p[WK["shotType"]] == 0 and p[WK["loopAnim"]]:
                cur = rnd.bounded(p[WK["numFrames"]] + 1) if p[WK["numFrames"]] else rnd.bounded(2)
        else:
            cur = p[WK["colorBullets"]] - rnd.bounded(2)
        tl = p[WK["timeToExplo"]]
        if p[WK["timeToExploV"]]:
            tl -= rnd.bounded(p[WK["timeToExploV"]])
        i = alloc(wobjs, MAX_WOBJ)
        wobjs[i] = {"x": fx, "y": fy, "vx": i32(vx), "vy": i32(vy), "cur": cur,
                    "time": tl, "owner": w.index, "type": w.weapon, "has_hit": 0}
        if True:
            print("WC f=%d wi=%d w=%d x=%d y=%d vx=%d vy=%d t=%d ty=%d" % (
                frame_g[0], i, w.index, fx, fy, i32(vx), i32(vy), tl, w.weapon))
    recoil = p[WK["recoil"]]
    w.vel_x = i32(w.vel_x - tdiv(CSX[aa] * recoil, 100))
    w.vel_y = i32(w.vel_y - tdiv(CSY[aa] * recoil, 100))

def try_fire(w):
    # upstream: level model — fire whenever held, loaded, delay expired, no Change
    ww = w.wwp[w.wslot]
    if 336 <= frame_g[0] <= 352: print("FS f=%d w=%d slot=%d wpn=%d am=%d dly=%d ld=%d fi=%d chg=%d" % (
        frame_g[0], w.index, w.wslot, w.weapon, ww["ammo"], ww["delay"], ww["loading"], w.keys["fire"], w.keys["change"]))
    if w.keys["fire"] and not w.keys["change"] and ww["loading"] == 0 and ww["delay"] <= 0:
        ww["ammo"] -= 1
        ww["delay"] = WP[w.weapon][WK["delay"]]
        fire_wobj(w)

def worm_available(w):
    return w.wwp[w.wslot]["loading"] == 0

def change_weapon(w, change_held, left_held, right_held):
    if change_held:
        if left_held and not w.prev_left:
            w.wslot -= 1
            if w.wslot < 0: w.wslot = len(WEAPONS_LIST[w.index])-1
            w.weapon = WEAPONS_LIST[w.index][w.wslot]
            w.wwp[w.wslot]["ammo"] = WP[w.weapon][WK["ammo"]]
            w.wwp[w.wslot]["loading"] = 0
            w.leave_shell_timer = 0
        if right_held and not w.prev_right:
            w.wslot += 1
            if w.wslot >= len(WEAPONS_LIST[w.index]): w.wslot = 0
            w.weapon = WEAPONS_LIST[w.index][w.wslot]
            w.wwp[w.wslot]["ammo"] = WP[w.weapon][WK["ammo"]]
            w.wwp[w.wslot]["loading"] = 0
            w.leave_shell_timer = 0
    w.prev_left = left_held if change_held else 0
    w.prev_right = right_held if change_held else 0
    w.prev_change = change_held

def worm_weapons(w):
    ww = w.wwp[w.wslot]
    if ww["delay"] >= 0:
        ww["delay"] -= 1
    if ww["ammo"] <= 0:
        lt = (100 * WP[w.weapon][WK["loadingTime"]]) // 100
        if lt == 0: lt = 1
        ww["loading"] = lt
        ww["ammo"] = WP[w.weapon][WK["ammo"]]
    if ww["loading"] > 0:
        ww["loading"] -= 1
    if w.leave_shell_timer > 0:
        w.leave_shell_timer -= 1
        if w.leave_shell_timer <= 0:
            vy = -rnd.bounded(20000)
            vx = rnd.bounded(16000) - 8000
            nobj_create1(SHELLS, vx, vy, w.pos_x, w.pos_y, 0, w.index)

# ---- wobject process ----
def process_wobjects(cyc):
    for i in sorted(list(wobjs.keys())):
        o = wobjs.get(i)
        if o is None: continue
        p = WP[o["type"]]
        do_explode = 0; do_remove = 0
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
        if frame_g[0] <= 190: print("WS f=%d wi=%d x=%d y=%d t=%d" % (frame_g[0], i, o["x"], o["y"], o["time"]))
        ix = o["x"] >> 16; iy = o["y"] >> 16
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        bounce = p[WK["bounce"]]
        if bounce > 0:
            if not inside(nx, iy) or dirtrock(nx, iy):
                if bounce != 100:
                    o["vx"] = i32(-tdiv(o["vx"]*bounce, 100)); o["vy"] = i32(tdiv(o["vy"]*4, 5))
                else: o["vx"] = i32(-o["vx"])
            if not inside(ix, ny) or dirtrock(ix, ny):
                if bounce != 100:
                    o["vy"] = i32(-tdiv(o["vy"]*bounce, 100)); o["vx"] = i32(tdiv(o["vx"]*4, 5))
                else: o["vy"] = i32(-o["vy"])
        if p[WK["multSpeed"]] != 100:
            o["vx"] = i32(tdiv(o["vx"]*p[WK["multSpeed"]], 100))
            o["vy"] = i32(tdiv(o["vy"]*p[WK["multSpeed"]], 100))
        # collideWithObjects
        if p[WK["collideWithObjects"]]:
            imp_x = tdiv(o["vx"] * p[WK["blowAway"]], 100)
            imp_y = tdiv(o["vy"] * p[WK["blowAway"]], 100)
            for k in sorted(list(wobjs.keys())):
                q = wobjs[k]
                if q["type"] != o["type"] or q["owner"] != o["owner"]:
                    if abs(o["x"] - q["x"]) <= (2 << 16) and abs(o["y"] - q["y"]) <= (2 << 16):
                        q["vx"] = i32(q["vx"] + imp_x); q["vy"] = i32(q["vy"] + imp_y)
            for k in sorted(list(nobjs.keys())):
                q = nobjs[k]
                if abs(o["x"] - q["x"]) <= (2 << 16) and abs(o["y"] - q["y"]) <= (2 << 16):
                    q["vx"] = i32(q["vx"] + imp_x); q["vy"] = i32(q["vy"] + imp_y)
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0: o["x"] = 0
        if ny < 0: o["y"] = 0
        if nx >= W: o["x"] = (W-1) << 16
        if ny >= H: o["y"] = (H-1) << 16
        hit = (not inside(nx, ny)) or dirtrock(nx, ny)
        if hit:
            if bounce == 0:
                if p[WK["explGround"]]: do_explode = 1
                else: o["vx"] = 0; o["vy"] = 0
        else:
            o["vy"] = i32(o["vy"] + p[WK["gravity"]])
            if p[WK["numFrames"]] > 0 and (cyc & 7) == 0:
                if not p[WK["loopAnim"]]:
                    o["cur"] += 1
                    if o["cur"] > p[WK["numFrames"]]: o["cur"] = 0
                else:
                    if o["vx"] < 0:
                        o["cur"] -= 1
                        if o["cur"] < 0: o["cur"] = p[WK["numFrames"]]
                    elif o["vx"] > 0:
                        o["cur"] += 1
                        if o["cur"] > p[WK["numFrames"]]: o["cur"] = 0
        if p[WK["timeToExplo"]] > 0:
            o["time"] -= 1
            if o["time"] < 0: do_explode = 1
        for w in worms:
            if (p[WK["hitDamage"]] or p[WK["blowAway"]] or p[WK["bloodOnHit"]] or p[WK["wormCollide"]]) \
               and check_spec_worm_hit(w, o["x"] >> 16, o["y"] >> 16, p[WK["detectDistance"]]):
                w.vel_x = i32(w.vel_x + tdiv(o["vx"] * p[WK["blowAway"]], 100))
                w.vel_y = i32(w.vel_y + tdiv(o["vy"] * p[WK["blowAway"]], 100))
                do_damage(w, p[WK["hitDamage"]], o["owner"])
                o["has_hit"] = 1
                blood = p[WK["bloodOnHit"]]
                for _ in range(blood):
                    angle = rnd.bounded(128)
                    nobj_create2(BLOOD, angle, tdiv(o["vx"],3), tdiv(o["vy"],3), o["x"], o["y"], 0, w.index)
                if p[WK["hitDamage"]] > 0 and w.health > 0 and rnd.bounded(3) == 0: rnd.bounded(3)
                if p[WK["wormCollide"]] and rnd.bounded(p[WK["wormCollide"]]) == 0:
                    if p[WK["wormExplode"]]: do_explode = 1
                    do_remove = 1
        if do_explode: wobj_blow_up(i, o["owner"])
        elif do_remove: wobjs.pop(i, None)

# ---- nobject process ----
def nobj_explode(i, cause):
    o = nobjs[i]
    p = NP[o["type"]]
    px_ = o["x"] >> 16; py_ = o["y"] >> 16
    if p[NK["createOnExp"]] >= 0: sobj_create(p[NK["createOnExp"]], px_, py_, o["owner"])
    if p[NK["dirtEffect"]] >= 0:
        dirt_effect(p[NK["dirtEffect"]], px_-7, py_-7)
        correct_shadow(px_-10, py_-10, px_+11, py_+11)
    for _ in range(p[NK["splinterAmount"]]):
        angle = rnd.bounded(128)
        cs = rnd.bounded(2)
        nobj_create2(p[NK["splinterType"]], angle, 0, 0, px_<<16, py_<<16,
                     p[NK["splinterColour"]] - cs, o["owner"])
    nobjs.pop(i, None)

def process_nobjects(cyc):
    for i in sorted(list(nobjs.keys())):
        o = nobjs.get(i)
        if o is None: continue
        p = NP[o["type"]]
        bounced = 0; do_explode = 0; freed = 0
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
        ix = o["x"] >> 16; iy = o["y"] >> 16
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        bounce = p[NK["bounce"]]
        if bounce > 0:
            if not inside(nx, iy) or dirtrock(nx, iy):
                o["vx"] = i32(-tdiv(o["vx"]*bounce, 100)); o["vy"] = i32(tdiv(o["vy"]*4, 5)); bounced = 1
            if not inside(ix, ny) or dirtrock(ix, ny):
                o["vy"] = i32(-tdiv(o["vy"]*bounce, 100)); o["vx"] = i32(tdiv(o["vx"]*4, 5)); bounced = 1
        if p[NK["bloodTrail"]] and p[NK["bloodTrailDelay"]] > 0 and cyc % p[NK["bloodTrailDelay"]] == 0:
            create_bobject(o["x"], o["y"], tdiv(o["vx"],4), tdiv(o["vy"],4))
        nx = (o["x"] + o["vx"]) >> 16; ny = (o["y"] + o["vy"]) >> 16
        if nx < 0: o["x"] = 0
        if ny < 0: o["y"] = 0
        if nx >= W: o["x"] = W << 16
        if ny >= H: o["y"] = H << 16
        hit = (not inside(nx, ny)) or dirtrock(nx, ny)
        if hit:
            o["vx"] = 0; o["vy"] = 0
            if p[NK["explGround"]]:
                if p[NK["startFrame"]] > 0 and p[NK["drawOnMap"]]:
                    blit_small(p[NK["startFrame"]] + o["cur"], ix-3, iy-3)
                    correct_shadow(ix-8, iy-8, ix+9, iy+9)
                do_explode = 1
        else:
            if not bounced and p[NK["leaveObjDelay"]] != 0 and p[NK["leaveObj"]] >= 0 \
               and cyc % p[NK["leaveObjDelay"]] == 0:
                sobj_create(p[NK["leaveObj"]], o["x"] >> 16, o["y"] >> 16, o["owner"])
            o["vy"] = i32(o["vy"] + p[NK["gravity"]])
            if p[NK["numFrames"]] > 0 and (cyc & 7) == 0:
                if o["vx"] > 0:
                    o["cur"] += 1
                    if o["cur"] > p[NK["numFrames"]]: o["cur"] = 0
                elif o["vx"] < 0:
                    o["cur"] -= 1
                    if o["cur"] < 0: o["cur"] = p[NK["numFrames"]]
        if p[NK["timeToExplo"]] > 0:
            o["time"] -= 1
            if o["time"] <= 0: do_explode = 1
        if not do_explode and p[NK["hitDamage"]] > 0:
            for w in worms:
                if check_spec_worm_hit(w, o["x"] >> 16, o["y"] >> 16, p[NK["detectDistance"]]):
                    w.vel_x = i32(w.vel_x + tdiv(o["vx"] * p[NK["blowAway"]], 100))
                    w.vel_y = i32(w.vel_y + tdiv(o["vy"] * p[NK["blowAway"]], 100))
                    do_damage(w, p[NK["hitDamage"]], o["owner"])
                    if rnd.bounded(3) == 0: rnd.bounded(3)   # pain snd pick
                    blood = p[NK["bloodOnHit"]]
                    for _ in range(blood):
                        angle = rnd.bounded(128)
                        nobj_create2(BLOOD, angle, tdiv(o["vx"],3), tdiv(o["vy"],3), o["x"], o["y"], 0, w.index)
                    if p[NK["wormExplode"]]: do_explode = 1
                    elif p[NK["wormDestroy"]]: nobjs.pop(i, None); freed = 1
                    break
        if not freed and do_explode: nobj_explode(i, o["owner"])

# ---- bobjects ----
def create_bobject(px_, py_, vx, vy):
    if len(bobjs) >= MAX_BOBJ: return
    bobjs.append({"x": i32(px_), "y": i32(py_), "vx": i32(vx), "vy": i32(vy),
                  "color": rnd.bounded(C["NumBloodColours"]) + C["FirstBloodColour"]})

def process_bobjects():
    i = 0
    while i < len(bobjs):
        o = bobjs[i]
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
        ix = o["x"] >> 16; iy = o["y"] >> 16
        remove = False
        if not inside(ix, iy): remove = True
        else:
            if back(ix, iy): o["vy"] = i32(o["vy"] + C["BObjGravity"])
            c = data[iy*W+ix]
            if (1 <= c <= 2) or (77 <= c <= 79):
                data[iy*W+ix] = (77 + rnd.bounded(3)) & 0xFF; remove = True
            elif anydirt(ix, iy):
                data[iy*W+ix] = (82 + rnd.bounded(3)) & 0xFF; remove = True
            elif rockq(ix, iy):
                data[iy*W+ix] = (85 + rnd.bounded(3)) & 0xFF; remove = True
        if remove:
            bobjs[i] = bobjs[-1]; bobjs.pop()
        else:
            i += 1

# ---- sobject process (animate only; damage done at create) ----
def process_sobjects(cyc):
    for i in sorted(list(sobjs.keys())):
        o = sobjs[i]
        p = SP[o["type"]]
        o["ad"] -= 1
        if o["ad"] <= 0:
            o["ad"] = p[SK["animDelay"]]
            o["cur"] += 1
            if o["cur"] > p[SK["numFrames"]]: sobjs.pop(i, None)

# ---- per-viewport process (follow worm; shake burns 2 rand) ----
def process_viewports():
    for vi in range(2):
        w = worms[vi]
        cx = VW // 2; cy = VH // 2
        if w.killed_timer <= 0:
            if w.visible:
                v_x[vi] = (w.pos_x >> 16) - cx
                v_y[vi] = (w.pos_y >> 16) - cy
            else:
                dx = (w.pos_x >> 16) - cx; dy = (w.pos_y >> 16) - cy
                for _ in range(4):
                    if v_x[vi] < dx: v_x[vi] += 1
                    elif v_x[vi] > dx: v_x[vi] -= 1
                    if v_y[vi] < dy: v_y[vi] += 1
                    elif v_y[vi] > dy: v_y[vi] -= 1
        elif w.health <= 0:
            v_x[vi] = (w.pos_x >> 16) - cx
            v_y[vi] = (w.pos_y >> 16) - cy
        rs = g_shake[vi] >> 16
        if rs > 0:
            v_x[vi] += rnd.bounded(rs*2) - rs
            v_y[vi] += rnd.bounded(rs*2) - rs
        v_x[vi] = max(0, min(v_x[vi], W - VW))
        v_y[vi] = max(0, min(v_y[vi], H - VH))

# ---- AI (DumbLieroAI, upstream worm.cpp:538) ----
def ai_process(w):
    # nearest other worm
    target = None; min_len = 0
    for o in worms:
        if o is w: continue
        dx = (w.pos_x >> 16) - (o.pos_x >> 16)
        dy = (w.pos_y >> 16) - (o.pos_y >> 16)
        ln = dx*dx + dy*dy
        if target is None or ln < min_len:
            target = o; min_len = ln
    if target is None: return (0,)*7
    p = WP[w.weapon]
    _dbg = frame_g[0] == 185
    if _dbg: print("AS f=%d s0 %08x,%08x real=%d maxd=%d vis=%d" % (frame_g[0], rnd.x, rnd.c, 0, 0, w.visible))
    tte = p[WK["timeToExplo"]]
    if 0 < tte < 500:
        max_dist = tdiv((tte - tdiv(p[WK["timeToExploV"]], 2)) * p[WK["speed"]], 130)
    else:
        max_dist = p[WK["speed"]] - tdiv(p[WK["gravity"]], 10)
    if max_dist < 90: max_dist = 90
    if _dbg: print("AS f=%d md %08x,%08x real=%d maxd=%d" % (frame_g[0], rnd.x, rnd.c, 0, max_dist))
    dx = (target.pos_x >> 16) - (w.pos_x >> 16)
    dy = (target.pos_y >> 16) - (w.pos_y >> 16)
    real = math.isqrt(dx*dx + dy*dy) if (dx*dx + dy*dy) >= 0 else 0   # vectorLength (isqrt, match ROM)
    # fire
    fire = w.ai_state["fire"]
    if real < max_dist or not w.visible:
        if rnd.bounded(AI_K[1 if fire else 0][AI["fire"]]) == 0:
            fire = 1 - fire
    else:
        if w.visible: fire = 0
    if _dbg: print("AS f=%d af %08x,%08x fire=%d" % (frame_g[0], rnd.x, rnd.c, fire))
    # jump
    jump = w.ai_state["jump"]
    if rnd.bounded(AI_K[1 if jump else 0][AI["jump"]]) == 0:
        jump = 1 - jump
    if _dbg: print("AS f=%d aj %08x,%08x jump=%d" % (frame_g[0], rnd.x, rnd.c, jump))
    # change
    change = w.ai_state["change"]
    if rnd.bounded(AI_K[1 if change else 0][AI["change"]]) == 0:
        change = 1 - change
    if _dbg: print("AS f=%d ac %08x,%08x chg=%d" % (frame_g[0], rnd.x, rnd.c, change))
    # dir
    if real > 0:
        ddx = tdiv(dx << 16, real); ddy = tdiv(dy << 16, real)
    else:
        ddx = ddy = 0
    dirn = 1
    while dirn < 128:
        if abs(CSX[dirn] - ddx) < 0xC00 and abs(CSY[dirn] - ddy) < 0xC00: break
        dirn += 1
    adx = abs(ddx); ady = abs(ddy)
    if dirn >= 128:
        if ddx > 0:
            if ddy < 0:
                if ady > adx: dirn = 64 + rnd.bounded(16)
                elif adx > ady: dirn = 80 + rnd.bounded(16)
                else: dirn = 80
            else:
                if adx > ady: dirn = 96 + rnd.bounded(16)
                else: dirn = 116
        else:
            if ddy < 0:
                if ady > adx: dirn = 48 + rnd.bounded(16)
                elif adx > ady: dirn = 32 + rnd.bounded(16)
                else: dirn = 48
            else:
                if adx > ady: dirn = 12 + rnd.bounded(16)
                else: dirn = 12
    # movement/aim
    left = w.ai_state["left"]; right = w.ai_state["right"]
    up = w.ai_state["up"]; down = w.ai_state["down"]
    if change:
        if rnd.bounded(AI_K[1 if left else 0][AI["left"]]) == 0:
            left = 1 - left
        if rnd.bounded(AI_K[1 if right else 0][AI["right"]]) == 0:
            right = 1 - right
        # ninjarope branch omitted (no rope yet): release up/down
        up = 0; down = 0
    else:
        if real > max_dist:
            right = 1 if dx > 0 else 0
            left  = 1 if dx <= 0 else 0
        else:
            left = 0; right = 0
        # aim
        aa = w.aim_angle >> 16
        if w.direction != 0:
            if dirn < 64: left = 1
            up   = 1 if dirn + 1 < aa else 0
            down = 1 if dirn - 1 > aa else 0
        else:
            if dirn > 64: right = 1
            up   = 1 if dirn - 1 > aa else 0
            down = 1 if dirn + 1 < aa else 0
        # obstacle reactions (reacts computed same coords as process)
        react = w.ai_reacts
        if left and react[RF_RIGHT]:
            if react[RF_DOWN] > 0: right = 1
            else: jump = 1
        if right and react[RF_LEFT]:
            if react[RF_DOWN] > 0: left = 1
            else: jump = 1
    if _dbg: print("AS f=%d am %08x,%08x l=%d r=%d u=%d d=%d" % (frame_g[0], rnd.x, rnd.c, left, right, up, down))
    w.ai_state = {"fire": fire, "jump": jump, "change": change,
                  "left": left, "right": right, "up": up, "down": down}
    return (fire, jump, change, left, right, up, down)

# ---- worm process ----
def worm_process(w, inp, cycle):
    global g_last_killed
    L, R, U, D, J, F, CH = inp
    w.keys = dict(left=L, right=R, up=U, down=D, jump=J, fire=F, change=CH)
    if frame_g[0] == 360 and w.index == 0: do_damage(w, 97, 1)   # AUTOTEST inject
    if frame_g[0] == 420 and w.index == 0: do_damage(w, 99, 1)   # AUTOTEST inject
    if not w.visible:
        worm_dead_process(w, F)
        w.ai_reacts = [0,0,0,0]
        return
    nx = i32(w.pos_x + w.vel_x); ny = i32(w.pos_y + w.vel_y)
    inx = nx >> 16; iny = ny >> 16
    reacts = [0]*4
    for d in range(4):
        reacts[d] = rc(inx, iny, d)
        if inx < 4: reacts[RF_RIGHT] += 5
        elif inx > W - 5: reacts[RF_LEFT] += 5
        if iny < 5: reacts[RF_DOWN] += 5
        elif iny > H - 6: reacts[RF_UP] += 5
    if reacts[RF_DOWN] < 2 and reacts[RF_UP] > 0 and (reacts[RF_LEFT] > 0 or reacts[RF_RIGHT] > 0):
        w.pos_y = i32(w.pos_y - (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    if reacts[RF_UP] < 2 and reacts[RF_DOWN] > 0 and (reacts[RF_LEFT] > 0 or reacts[RF_RIGHT] > 0):
        w.pos_y = i32(w.pos_y + (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    w.ai_reacts = reacts
    # processAiming
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
    if J and not w.prev_jump and reacts[RF_UP] > 0 and w.able_jump:
        w.vel_y = i32(w.vel_y - JUMP); w.able_jump = 0
    if not J: w.able_jump = 1
    w.prev_jump = J
    # processWeapons + fire (before physics, upstream order)
    worm_weapons(w)
    try_fire(w)
    # processPhysics
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
    # change or movement (upstream: processWeaponChange / processMovement)
    if CH:
        if not w.prev_change:
            # first Change press releases Left/Right
            w.prev_left = 0; w.prev_right = 0
            L = 0; R = 0
        w.animate = 0
        ww = w.wwp[w.wslot]
        if ww["loading"] == 0 or True:   # available() || loadChange (upstream default true, settings.cpp:46)
            if L and not w.prev_left:
                w.wslot -= 1
                if w.wslot < 0: w.wslot = len(WEAPONS_LIST[w.index])-1
                w.weapon = WEAPONS_LIST[w.index][w.wslot]
                w.wwp[w.wslot]["ammo"] = WP[w.weapon][WK["ammo"]]
                w.wwp[w.wslot]["loading"] = 0
                w.leave_shell_timer = 0
            if R and not w.prev_right:
                w.wslot += 1
                if w.wslot >= len(WEAPONS_LIST[w.index]): w.wslot = 0
                w.weapon = WEAPONS_LIST[w.index][w.wslot]
                w.wwp[w.wslot]["ammo"] = WP[w.weapon][WK["ammo"]]
                w.wwp[w.wslot]["loading"] = 0
                w.leave_shell_timer = 0
        w.prev_left = L; w.prev_right = R
        w.prev_change = 1
    else:
        w.prev_change = 0
        w.prev_left = 0; w.prev_right = 0
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
    # pain blood
    if w.health < 25:
        if rnd.bounded(w.health + 6) == 0:
            if rnd.bounded(3) == 0: rnd.bounded(3)
            nobj_create1(BLOOD, w.vel_x, w.vel_y, w.pos_x, w.pos_y, 0, w.index)
    # death
    died = w.health <= 0
    if died:
        worm_die(w)
    # anim frame
    af = ((cycle & 31) >> 3) if (w.animate and not died) else 0
    x = (w.aim_angle >> 16) - 12
    if w.direction != 0: x -= 49
    x >>= 3
    x = 0 if x < 0 else (6 if x > 6 else x)
    if w.direction != 0: x = 6 - x
    w.cur = x + ANIM[af]

for w in worms:
    w.aim_speed = 0
    w.ai_state = {"fire":0,"jump":0,"change":0,"left":0,"right":0,"up":0,"down":0}
    w.ai_reacts = [0,0,0,0]

def fnv(b):
    h = 2166136261
    for v in b:
        h = ((h ^ v) * 16777619) & 0xFFFFFFFF
    return h

# ---- AUTOTEST script ----
# worm0: scripted like slice 4; worm1: AI (scripted-free)
CK_MARKS = (112, 137, 162, 180, 262, 287, 312, 337, 362, 387, 412,
            37, 62, 87, 612, 637, 662, 712, 762, 812, 862, 912, 962,
            1012, 1062, 1112, 1162, 1212)
FRAMES = 1260
AI_ON = [bool(int(x)) for x in (sys.argv[1] if len(sys.argv) > 1 else "01").strip()] or [0,0]
if len(AI_ON) < 2: AI_ON += [0]*(2-len(AI_ON))
print("AI_ON %s" % "".join(map(str, AI_ON)))

for frame in range(FRAMES):
    frame_g[0] = frame
    cyc = frame + 1
    ph = frame % 420
    R = 1 if ph < 120 else 0
    J = 1 if ph == 120 else 0
    U = 1 if ph >= 330 else 0
    D = 1 if (ph >= 100 and ph < 118) or (ph >= 344 and ph < 352) else 0
    F = 1 if (ph in (110, 135, 160, 185, 210, 346, 351)) else 0
    L = 1 if ((ph >= 210 and ph < 330) or ph == 341) else 0
    CH = 1 if (ph in (340, 341, 342)) else 0

    # ---- AI phase (upstream localController: order (i+phase)%size, cycles==frame here) ----
    phase = frame % 2
    for k in range(2):
        w = worms[(k + phase) % 2]
        if AI_ON[w.index]: ai_process(w)

    # ---- viewport shake decay (upstream: top of processFrame) ----
    for vi in range(2):
        if g_shake[vi] > 0: g_shake[vi] -= 4000

    # ---- process objects ----
    process_sobjects(cyc)
    process_wobjects(cyc)
    process_nobjects(cyc)
    process_bobjects()

    # ---- worms (fixed order) ----
    b = worms[0].ai_state
    if AI_ON[0]:
        s_inp = (b["left"], b["right"], b["up"], b["down"], b["jump"], b["fire"], b["change"])
    else:
        s_inp = (L, R, U, D, J, F, CH)
    a = worms[1].ai_state
    if AI_ON[1]:
        a_inp = (a["left"], a["right"], a["up"], a["down"], a["jump"], a["fire"], a["change"])
    else:
        a_inp = s_inp
    worm_process(worms[0], s_inp, frame)
    worm_process(worms[1], a_inp, frame)

    # ---- AI control trace (after worms, matching ROM dump point) ----
    a = worms[1].ai_state
    print("AI f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d aim=%d w=%d" % (
        frame, a["fire"], a["jump"], a["change"], a["left"], a["right"], a["up"], a["down"],
        worms[1].aim_angle >> 16, worms[1].weapon))
    b = worms[0].ai_state
    if AI_ON[0]:
        print("A0 f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d" % (
            frame, b["fire"], b["jump"], b["change"], b["left"], b["right"], b["up"], b["down"]))
    else:
        print("A0 f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d" % (frame, F, J, CH, L, R, U, D))

    # ---- viewport follow + shake rand (upstream: after worms) ----
    process_viewports()

    # ---- trace ----
    if frame % 5 == 2 or 175 <= frame <= 190:
        for j in sorted(wobjs):
            o = wobjs[j]
            print("WT f=%d i=%d x=%d y=%d vx=%d vy=%d t=%d ty=%d ow=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["time"], o["type"], o["owner"]))
    if frame % 5 == 1:
        print("NN f=%d n=%d" % (frame, len(nobjs)))
        cnt = 0
        for j in sorted(nobjs):
            if cnt >= 16: break
            o = nobjs[j]; cnt += 1
            print("NT f=%d i=%d x=%d y=%d vx=%d vy=%d t=%d ty=%d ow=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["time"], o["type"], o["owner"]))
    if frame % 5 == 3:
        for j in sorted(sobjs):
            o = sobjs[j]
            print("ST f=%d i=%d x=%d y=%d cur=%d ad=%d ty=%d" % (
                frame, j, o["x"], o["y"], o["cur"], o["ad"], o["type"]))
    if frame % 5 == 4:
        for j in range(min(len(bobjs), 12)):
            o = bobjs[j]
            print("BO f=%d i=%d x=%d y=%d vx=%d vy=%d co=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["color"]))
    print("RG f=%d %08x,%08x" % (frame, rnd.x, rnd.c))
    _w1 = worms[1]
    print("CM f=%d p=%d,%d v=%d,%d a=%d as=%d dir=%d rv=%d,%d,%d,%d" % (
        frame, _w1.pos_x >> 16, _w1.pos_y >> 16, _w1.vel_x, _w1.vel_y,
        _w1.aim_angle >> 16, _w1.aim_speed >> 16, _w1.direction,
        _w1.ai_reacts[0], _w1.ai_reacts[1], _w1.ai_reacts[2], _w1.ai_reacts[3]))
    if ph in CK_MARKS or frame % 60 == 59 or frame == FRAMES-1:
        th = fnv(data)
        wc = len(wobjs); nc = len(nobjs); sc = len(sobjs); bc = len(bobjs)
        oh = 2166136261
        for j in sorted(wobjs):
            o = wobjs[j]
            bb = struct.pack("<10i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"],
                             o["owner"], o["type"], 1 if o.get("has_hit") else 0, 0)
            h2 = oh
            for v in bb: h2 = ((h2 ^ v) * 16777619) & 0xFFFFFFFF
            oh = h2
        for j in sorted(nobjs):
            o = nobjs[j]
            bb = struct.pack("<8i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"], o["type"], o["owner"])
            h2 = oh
            for v in bb: h2 = ((h2 ^ v) * 16777619) & 0xFFFFFFFF
            oh = h2
        for j in sorted(sobjs):
            o = sobjs[j]
            bb = struct.pack("<5i", o["x"], o["y"], o["cur"], o["ad"], o["type"])
            h2 = oh
            for v in bb: h2 = ((h2 ^ v) * 16777619) & 0xFFFFFFFF
            oh = h2
        for o in bobjs:
            bb = struct.pack("<5i", o["x"], o["y"], o["vx"], o["vy"], o["color"])
            h2 = oh
            for v in bb: h2 = ((h2 ^ v) * 16777619) & 0xFFFFFFFF
            oh = h2
        print("CK frame=%d terr=%08x rng=%08x,%08x wc=%d nc=%d sc=%d bc=%d obj=%08x kl=%d vp=%d,%d|%d,%d shk=%d,%d" % (
            frame, th, rnd.x, rnd.c, wc, nc, sc, bc, oh, g_last_killed,
            v_x[0], v_y[0], v_x[1], v_y[1], g_shake[0], g_shake[1]))
        for w in worms:
            ww = w.wwp[w.wslot]
            print("CW %d p=%d,%d v=%d,%d hp=%d vis=%d kt=%d lr=%d,%d aim=%d dir=%d cur=%d w=%d wslot=%d am=%d dly=%d ld=%d lv=%d k=%d sf=%d" % (
                w.index, w.pos_x >> 16, w.pos_y >> 16, w.vel_x, w.vel_y, w.health, w.visible,
                w.killed_timer, w.lr_x, w.lr_y, w.aim_angle >> 16, w.direction, w.cur, w.weapon,
                w.wslot, ww["ammo"], ww["delay"], ww["loading"], w.lives, w.kills,
                w.leave_shell_timer))

# game over check
for w in worms:
    if w.lives <= 0:
        print("OVR f=%d winner=%d lives=%d,%d" % (frame_g[0], w.index ^ 1, worms[0].lives, worms[1].lives))
