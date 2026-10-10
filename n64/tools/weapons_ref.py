#!/usr/bin/env python3
"""Host mirror of ROM weapons+combat (slice 4): multi-weapon (dirtball/handgun),
sobject explosions, blood nobjects + blood-trail bobjects, shells, worm damage,
death and respawn. Generates terrain via bake_assets (exact RNG stream), carves the
WORM test chamber, runs the scripted AUTOTEST inputs and prints CK/WT/NT trace lines
byte-identical to the ROM debugf output."""
import os, re, struct, sys, json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bake_assets import MWC, load_cfg, read_tga, generate, TC, LEVEL_W, LEVEL_H

W, H = LEVEL_W, LEVEL_H
DIRT, DIRT2, ROCK, BACK, SEE, WORMF = 1, 2, 4, 8, 16, 32
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

# ---- cossin ----
txt = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "baked.h")).read()
CSX = [int(v) for v in re.search(r"cs_x\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]
CSY = [int(v) for v in re.search(r"cs_y\[128\]=\{(.*?)\};", txt, re.S).group(1).split(",")]

# ---- combat param tables (baked twin of assets/combat.h) ----
P = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ref", "combat_params.json")))
WP, NP, SP = P["w"], P["n"], P["s"]
IDX = P["idx"]; C = P["const"]
BLOOD = IDX["blood"]; SHELLS = IDX["shells"]; PART2 = IDX["part2"]
DIRTN = IDX["dirt"]; WPARTS = 0
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

# ---- carve chamber (idx160: background+seeThrough air to worm AND bullets) ----
for y in range(156, 180):
    for x in range(100, 180):
        sp(x, y, 160)

# ---- reactions (slice 2) ----
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
w.health = 100; w.lives = 5; w.visible = 1; w.ready = 0
w.killed_timer = 0; w.lr_x = 0; w.lr_y = 0
print("SPAWN x=%d y=%d" % (sx, sy))

# ---- combat state ----
wwp = [{"ammo": WP[DIRTBALL][WK["ammo"]], "delay": 0, "loading": 0} for _ in range(5)]
wobjs = {}          # idx -> dict (ExactObjectList: lowest free)
nobjs = {}          # idx -> dict (ExactObjectList: lowest free)
sobjs = {}          # idx -> dict (ExactObjectList: lowest free)
bobjs = []          # list of dicts (FastObjectList: append / swap-pop)
prev_fire = 0
prev_change = 0
prev_left = 0
leave_shell_timer = 0
cur_wslot = 0
cur_weapon = DIRTBALL
frame_g = [0]
g_screen_flash = 0
g_shake = 0
g_last_killed = -1
g_last_killed_by = -1
WEAPONS_LIST = [DIRTBALL, HANDGUN]
MAX_WOBJ, MAX_NOBJ, MAX_SOBJ, MAX_BOBJ = 256, 512, 700, 700

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
            pool[i] = True
            return i
    return cap - 1

# ---- nobject create core / create1 / create2 ----
def nobj_core(t, vx, vy, px_, py_, color):
    p = NP[t]
    i = alloc(nobjs, MAX_NOBJ)
    if p[NK["startFrame"]] > 0: cf = rnd.bounded(p[NK["numFrames"]] + 1)
    elif color != 0:            cf = color
    else:                       cf = p[NK["colorBullets"]]
    tl = p[NK["timeToExplo"]]
    if p[NK["timeToExploV"]]: tl -= rnd.bounded(p[NK["timeToExploV"]])
    nobjs[i] = {"x": i32(px_), "y": i32(py_), "vx": i32(vx), "vy": i32(vy),
                "cur": cf, "time": tl, "type": t}
    return i

def nobj_create1(t, vx, vy, px_, py_, color):
    d = NP[t][NK["distribution"]]
    if d:
        vx += d - rnd.bounded(d*2)
        vy += d - rnd.bounded(d*2)
    nobj_core(t, vx, vy, px_, py_, color)

def nobj_create2(t, angle, bvx, bvy, px_, py_, color):
    p = NP[t]
    rs = p[NK["speed"]] - rnd.bounded(p[NK["speedV"]])
    vx = bvx + tdiv(CSX[angle & 127] * rs, 100)
    vy = bvy + tdiv(CSY[angle & 127] * rs, 100)
    d = p[NK["distribution"]]
    if d:
        vx += rnd.bounded(d*2) - d
        vy += rnd.bounded(d*2) - d
    i = nobj_core(t, vx, vy, px_, py_, color)
    nobjs[i]["x"] = i32(nobjs[i]["x"] + nobjs[i]["vx"])
    nobjs[i]["y"] = i32(nobjs[i]["y"] + nobjs[i]["vy"])

# ---- sobject create ----
def sobj_create(t, x, y, owner_idx):
    global g_screen_flash, g_shake
    p = SP[t]
    i = alloc(sobjs, MAX_SOBJ)
    if p[SK["startSound"]] >= 0: rnd.bounded(p[SK["numSounds"]])
    sobjs[i] = {"x": x-8, "y": y-8, "cur": 0, "ad": p[SK["animDelay"]], "type": t}
    if p[SK["flash"]] > g_screen_flash: g_screen_flash = p[SK["flash"]]
    shk = p[SK["shake"]] << 16
    if shk > g_shake: g_shake = shk
    damage = p[SK["damage"]]
    if damage > 0:
        dr = p[SK["detectRange"]]
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
                do_damage(z, owner_idx)
                blood_amount = power_sum   # settings->blood=100
                for _ in range(blood_amount):
                    angle = rnd.bounded(128)
                    nobj_create2(BLOOD, angle, tdiv(w.vel_x,3), tdiv(w.vel_y,3), w.pos_x, w.pos_y, 0)
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
                    nobj_create2(PART2, angle, 0, 0, px_ << 16, py_ << 16, pix)
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
                         p[WK["splinterColour"]] - colorSub)
    elif splinters > 0:
        for _ in range(splinters):
            colorSub = rnd.bounded(2)
            nobj_create1(p[WK["splinterType"]], velX, velY, px_<<16, py_<<16,
                         p[WK["splinterColour"]] - colorSub)
    if p[WK["dirtEffect"]] >= 0:
        dirt_effect(p[WK["dirtEffect"]], px_-7, py_-7)
        correct_shadow(px_-10, py_-10, px_+11, py_+11)

# ---- worm sprite hit test ----
def check_spec_worm_hit(x, y, dist):
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

def do_damage(amount, by_idx):
    global g_last_killed_by
    if amount > 0:
        w.health -= amount
        print("DMG f=%d a=%d hp=%d by=%d" % (frame_g[0], amount, w.health, by_idx))
        if w.health <= 0: g_last_killed_by = by_idx

# ---- fire (STNormal subset) ----
def fire_wobj():
    global leave_shell_timer
    p = WP[cur_weapon]
    aa = (w.aim_angle >> 16) & 127
    fx = i32(CSX[aa] * (p[WK["detectDistance"]] + 5) + w.pos_x)
    fy = i32(CSY[aa] * (p[WK["detectDistance"]] + 5) + w.pos_y - (1 << 16))
    if p[WK["leaveShells"]] > 0 and rnd.bounded(p[WK["leaveShells"]]) == 0:
        leave_shell_timer = p[WK["leaveShellDelay"]]
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
            vx += rnd.bounded(d*2) - d
            vy += rnd.bounded(d*2) - d
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
                    "time": tl, "owner": 0, "type": cur_weapon, "has_hit": 0}
    recoil = p[WK["recoil"]]
    w.vel_x = i32(w.vel_x - tdiv(CSX[aa] * recoil, 100))
    w.vel_y = i32(w.vel_y - tdiv(CSY[aa] * recoil, 100))

def try_fire(fire_held, change_held):
    global prev_fire
    ww = wwp[0]
    if fire_held and not prev_fire and not change_held and ww["loading"] == 0 and ww["delay"] <= 0:
        ww["ammo"] -= 1
        ww["delay"] = WP[cur_weapon][WK["delay"]]
        fire_wobj()
    prev_fire = fire_held

def change_weapon(change_held, left_held, right_held):
    global prev_change, prev_left, cur_wslot, cur_weapon, leave_shell_timer
    if change_held:
        if left_held and not prev_left:
            cur_wslot -= 1
            if cur_wslot < 0: cur_wslot = len(WEAPONS_LIST)-1
            cur_weapon = WEAPONS_LIST[cur_wslot]
            wwp[0]["ammo"] = WP[cur_weapon][WK["ammo"]]
            wwp[0]["loading"] = 0
            leave_shell_timer = 0
        if right_held:
            cur_wslot += 1
            if cur_wslot >= len(WEAPONS_LIST): cur_wslot = 0
            cur_weapon = WEAPONS_LIST[cur_wslot]
            wwp[0]["ammo"] = WP[cur_weapon][WK["ammo"]]
            wwp[0]["loading"] = 0
            leave_shell_timer = 0
    prev_left = left_held if change_held else 0
    prev_change = change_held

def worm_weapons():
    global leave_shell_timer
    for ww in wwp:
        if ww["delay"] >= 0:
            ww["delay"] -= 1
    ww = wwp[0]
    if ww["ammo"] <= 0:
        lt = (100 * WP[cur_weapon][WK["loadingTime"]]) // 100
        if lt == 0: lt = 1
        ww["loading"] = lt
        ww["ammo"] = WP[cur_weapon][WK["ammo"]]
    if ww["loading"] > 0:
        ww["loading"] -= 1
    if leave_shell_timer > 0:
        leave_shell_timer -= 1
        if leave_shell_timer <= 0:
            vy = -rnd.bounded(20000)
            vx = rnd.bounded(16000) - 8000
            nobj_create1(SHELLS, vx, vy, w.pos_x, w.pos_y, 0)

# ---- wobject process (STNormal subset) ----
def process_wobjects(cyc):
    for i in sorted(list(wobjs.keys())):
        o = wobjs[i]
        p = WP[o["type"]]
        do_explode = 0; do_remove = 0
        o["x"] = i32(o["x"] + o["vx"]); o["y"] = i32(o["y"] + o["vy"])
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
        if (p[WK["hitDamage"]] or p[WK["blowAway"]] or p[WK["bloodOnHit"]] or p[WK["wormCollide"]]) \
           and check_spec_worm_hit(o["x"] >> 16, o["y"] >> 16, p[WK["detectDistance"]]):
            w.vel_x = i32(w.vel_x + tdiv(o["vx"] * p[WK["blowAway"]], 100))
            w.vel_y = i32(w.vel_y + tdiv(o["vy"] * p[WK["blowAway"]], 100))
            do_damage(p[WK["hitDamage"]], o["owner"])
            if not o.get("has_hit"): o["has_hit"] = 1
            blood = p[WK["bloodOnHit"]]   # *settings->blood(100)/100
            for _ in range(blood):
                angle = rnd.bounded(128)
                nobj_create2(BLOOD, angle, tdiv(o["vx"],3), tdiv(o["vy"],3), o["x"], o["y"], 0)
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
    if p[NK["createOnExp"]] >= 0: sobj_create(p[NK["createOnExp"]], px_, py_, cause)
    if p[NK["dirtEffect"]] >= 0:
        dirt_effect(p[NK["dirtEffect"]], px_-7, py_-7)
        correct_shadow(px_-10, py_-10, px_+11, py_+11)
    for _ in range(p[NK["splinterAmount"]]):
        angle = rnd.bounded(128)
        cs = rnd.bounded(2)
        nobj_create2(p[NK["splinterType"]], angle, 0, 0, px_<<16, py_<<16,
                     p[NK["splinterColour"]] - cs)
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
                sobj_create(p[NK["leaveObj"]], o["x"] >> 16, o["y"] >> 16, 0)
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
        if not do_explode and p[NK["hitDamage"]] > 0 \
           and check_spec_worm_hit(o["x"] >> 16, o["y"] >> 16, p[NK["detectDistance"]]):
            w.vel_x = i32(w.vel_x + tdiv(o["vx"] * p[NK["blowAway"]], 100))
            w.vel_y = i32(w.vel_y + tdiv(o["vy"] * p[NK["blowAway"]], 100))
            do_damage(p[NK["hitDamage"]], 0)
            if rnd.bounded(3) == 0: rnd.bounded(3)   # pain snd pick BEFORE blood
            blood = p[NK["bloodOnHit"]]
            for _ in range(blood):
                angle = rnd.bounded(128)
                nobj_create2(BLOOD, angle, tdiv(o["vx"],3), tdiv(o["vy"],3), o["x"], o["y"], 0)
            if p[NK["wormExplode"]]: do_explode = 1
            elif p[NK["wormDestroy"]]: nobjs.pop(i, None); freed = 1
        if not freed and do_explode: nobj_explode(i, 0)

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
            bobjs[i] = bobjs[-1]
            bobjs.pop()
        else:
            i += 1

# ---- sobject process ----
def process_sobjects(cyc):
    for i in sorted(list(sobjs.keys())):
        o = sobjs[i]
        p = SP[o["type"]]
        o["ad"] -= 1
        if o["ad"] <= 0:
            o["ad"] = p[SK["animDelay"]]
            o["cur"] += 1
            if o["cur"] > p[SK["numFrames"]]: sobjs.pop(i, None)

# ---- viewport shake (burns two rand calls) ----
def process_shake():
    global g_shake
    rs = g_shake >> 16
    if rs > 0:
        rnd.bounded(rs*2); rnd.bounded(rs*2)
    if g_shake > 0: g_shake -= 4000

# ---- worm death / respawn ----
def worm_die():
    global leave_shell_timer, prev_fire, g_last_killed
    leave_shell_timer = 0
    rnd.bounded(3)          # death snd pick
    g_last_killed = 0
    w.visible = 0
    w.killed_timer = 150
    w.lives -= 1
    print("DIE f=%d lives=%d by=%d" % (frame_g[0], w.lives, g_last_killed_by))
    for _ in range(120):
        angle = rnd.bounded(128)
        nobj_create2(BLOOD, angle, tdiv(w.vel_x,3), tdiv(w.vel_y,3), w.pos_x, w.pos_y, 0)
    a = 7
    while a <= 105:
        nobj_create2(WPARTS, a + rnd.bounded(14), tdiv(w.vel_x,3), tdiv(w.vel_y,3), w.pos_x, w.pos_y, 0)
        a += 14
    prev_fire = 0

def check_respawn_pos(oldx, oldy, x, y):
    if abs(oldx) <= C["WormMinSpawnDistLast"] and abs(oldy - y) <= C["WormMinSpawnDistLast"]: return False
    if abs(oldx - x) <= C["WormMinSpawnDistEnemy"] and abs(oldy - y) <= C["WormMinSpawnDistEnemy"]: return False
    x1 = max(x-3, 0); y1 = max(y-4, 0)
    x2 = min(x+3, W-1); y2 = min(y+4, H-1)
    for j in range(y1, y2):
        for k in range(x1, x2):
            if rockq(k, j): return False
    return True

def worm_begin_respawn():
    temp_x = w.pos_x >> 16; temp_y = w.pos_y >> 16
    trials = 0
    while True:
        sx2 = C["WormSpawnRectX"] + rnd.bounded(C["WormSpawnRectW"])
        sy2 = C["WormSpawnRectY"] + rnd.bounded(C["WormSpawnRectH"])
        while sy2 + 4 < H and back(sx2, sy2 + 4): sy2 += 1
        trials += 1
        if trials >= 50000: break
        if check_respawn_pos(temp_x, temp_y, sx2, sy2): break
    w.pos_x = sx2 << 16; w.pos_y = sy2 << 16
    w.lr_x = temp_x - 80; w.lr_y = temp_y - 80
    w.killed_timer = -1

def worm_dead_process(fire_held):
    if fire_held: w.ready = 1
    if w.killed_timer > 0: w.killed_timer -= 1
    if w.killed_timer == 0: worm_begin_respawn()
    if w.killed_timer < 0:
        dest_x = (w.pos_x >> 16) - 80
        dest_y = (w.pos_y >> 16) - 80
        for _ in range(4):
            if w.lr_x < dest_x: w.lr_x += 1
            elif w.lr_x > dest_x: w.lr_x -= 1
            if w.lr_y < dest_y: w.lr_y += 1
            elif w.lr_y > dest_y: w.lr_y -= 1
        w.lr_x = max(0, min(w.lr_x, W-158))
        w.lr_y = max(0, min(w.lr_y, H-158))
        dest_x = max(0, min(dest_x, W-158))
        dest_y = max(0, min(dest_y, H-158))
        if dest_x-5 < w.lr_x < dest_x+5 and dest_y-5 < w.lr_y < dest_y+5 and w.ready:
            ipx = w.pos_x >> 16; ipy = w.pos_y >> 16
            dirt_effect(0, ipx-7, ipy-7)
            correct_shadow(ipx-10, ipy-10, ipx+11, ipy+11)
            w.ready = 0
            w.visible = 1
            w.vel_x = 0; w.vel_y = 0
            w.health = 100
            print("RSP f=%d x=%d y=%d" % (frame_g[0], w.pos_x >> 16, w.pos_y >> 16))
            if rnd.next() & 1: w.aim_angle = 32 << 16; w.direction = 0
            else: w.aim_angle = 96 << 16; w.direction = 1

# ---- worm process ----
prev_jump = 0
def worm_process(L, R, U, D, J, F, CH, cycle):
    global prev_jump
    if cycle == 360: do_damage(97, 1)   # AUTOTEST: near-lethal hit
    if cycle == 420: do_damage(99, 1)   # AUTOTEST: lethal hit
    if not w.visible:
        worm_dead_process(F)
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
    # weapons + fire (before physics, upstream order; fire blocked while Change)
    worm_weapons()
    if CH: change_weapon(CH, L, R)
    try_fire(F, CH)
    if not CH:
        change_weapon(CH, L, R)
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
    # movement (skipped while Change held)
    if CH:
        w.animate = 0
    else:
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
            nobj_create1(BLOOD, w.vel_x, w.vel_y, w.pos_x, w.pos_y, 0)
    # death
    died = w.health <= 0
    if died:
        worm_die()
    # anim frame update (upstream runs it this frame even after death)
    af = ((cycle & 31) >> 3) if (w.animate and not died) else 0
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

CK_MARKS = (112, 137, 162, 180, 262, 287, 312, 337, 362, 387, 412, 437, 462, 487, 512, 537, 562, 587, 37, 62, 87)

for frame in range(1260):
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
    # process order: sobjects -> wobjects -> nobjects -> bobjects -> worms -> shake
    process_sobjects(cyc)
    process_wobjects(cyc)
    process_nobjects(cyc)
    process_bobjects()
    worm_process(L, R, U, D, J, F, CH, frame)
    process_shake()
    if 100 <= frame <= 640 and frame % 5 == 2:
        for j in sorted(wobjs):
            o = wobjs[j]
            print("WT f=%d i=%d x=%d y=%d vx=%d vy=%d t=%d ty=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["time"], o["type"]))
    if 100 <= frame <= 640 and frame % 5 == 1:
        print("NN f=%d n=%d" % (frame, len(nobjs)))
        cnt = 0
        for j in sorted(nobjs):
            if cnt >= 12: break
            o = nobjs[j]; cnt += 1
            print("NT f=%d i=%d x=%d y=%d vx=%d vy=%d t=%d ty=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["time"], o["type"]))
    if 100 <= frame <= 640 and frame % 5 == 3:
        for j in sorted(sobjs):
            o = sobjs[j]
            print("ST f=%d i=%d x=%d y=%d cur=%d ad=%d ty=%d" % (
                frame, j, o["x"], o["y"], o["cur"], o["ad"], o["type"]))
    if 100 <= frame <= 640 and frame % 5 == 4:
        for j in range(min(len(bobjs), 8)):
            o = bobjs[j]
            print("BO f=%d i=%d x=%d y=%d vx=%d vy=%d co=%d" % (
                frame, j, o["x"], o["y"], o["vx"], o["vy"], o["color"]))
    if ph in CK_MARKS or frame % 60 == 59:
        th = fnv(data)
        wc = len(wobjs); nc = len(nobjs); sc = len(sobjs); bc = len(bobjs)
        oh = 2166136261
        for j in sorted(wobjs):
            o = wobjs[j]
            bb = struct.pack("<9i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"],
                             o["owner"], o["type"], 1 if o.get("has_hit") else 0)
            h2 = oh
            for v in bb: h2 = ((h2 ^ v) * 16777619) & 0xFFFFFFFF
            oh = h2
        for j in sorted(nobjs):
            o = nobjs[j]
            bb = struct.pack("<7i", o["x"], o["y"], o["vx"], o["vy"], o["cur"], o["time"], o["type"])
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
        print("CK frame=%d terr=%08x rng=%08x,%08x wc=%d nc=%d sc=%d bc=%d obj=%08x wpos=%d,%d wvel=%d,%d hp=%d vis=%d kt=%d lr=%d,%d ammo=%d dly=%d ld=%d aim=%d wcur=%d w=%d" % (
            frame, th, rnd.x, rnd.c, wc, nc, sc, bc, oh,
            w.pos_x >> 16, w.pos_y >> 16, w.vel_x, w.vel_y,
            w.health, w.visible, w.killed_timer, w.lr_x, w.lr_y,
            wwp[0]["ammo"], wwp[0]["delay"], wwp[0]["loading"], w.aim_angle >> 16,
            w.cur, cur_weapon))
