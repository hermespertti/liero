#!/usr/bin/env python3
"""Host mirror of ROM worm physics (slice 2). Compares scripted trace."""
import re, sys

LW, LH = 504, 350
data = bytearray(open('ref/terrain.bin', 'rb').read())
txt = open('assets/baked.h').read()
flags = [int(v) for v in re.search(r"mat_flags\[256\]=\{(.*?)\};", txt, re.S).group(1).split(",") if v.strip()]

FRIC_M, FRIC_D = 89, 100
GRAVITY, JUMP = 1500, 56064
WALK, MAXV, BOUNCE = 3000, 29184, 53248
AACC, MAIMV, AF_M, AF_D = 4000, 70000, 83, 100
AMINR, AMAXR, AMAXL, AMINL = 64, 116, 12, 64
ANIM = [0, 7, 0, 14]
COL = {0: [(-1,-4),(0,-4),(1,-4)],
       1: [(1,-3),(1,-2),(1,-1),(1,0),(1,1),(1,2),(1,3)],
       2: [(-1,4),(0,4),(1,4)],
       3: [(-1,-3),(-1,-2),(-1,-1),(-1,0),(-1,1),(-1,2),(-1,3)]}
RF_DOWN, RF_LEFT, RF_UP, RF_RIGHT = 0, 1, 2, 3

def i32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v >= 0x80000000 else v

def tdiv(a, b):  # C trunc toward zero
    q = abs(a) // abs(b)
    return i32(q if (a >= 0) == (b >= 0) else -q)

def solid(x, y):
    idx = x + y*LW
    if idx < 0 or idx >= LW*LH: return 1
    return (flags[data[idx]] & 8) == 0

# carve test chamber (same as ROM, post-gen)
for y in range(156, 180):
    for x in range(100, 180):
        data[y*LW + x] = 1
for y in range(168, 180):
    for x in range(140, 160):
        data[y*LW + x] = 1

def rc(x, y, d):
    return sum(1 for dx, dy in COL[d] if solid(x+dx, y+dy))

class W:
    pass
w = W()
sx = 115; sy = 176
for y in range(156, 180):
    if rc(sx, y, RF_UP) >= 1: sy = y; break
w.pos_x = sx << 16; w.pos_y = sy << 16
w.vel_x = w.vel_y = 0
w.aim_angle = 64 << 16; w.aim_speed = 0
w.direction = 1; w.able_jump = 1; w.animate = 0; w.cur = 0
print("SPAWN x=%d y=%d" % (sx, sy))

prev_jump = 0
acc = 0x811c9dc5  # will match if ROM uses same; compare positions instead
def process(left, right, up, down, jump, cycle):
    global prev_jump
    nx = i32(w.pos_x + w.vel_x); ny = i32(w.pos_y + w.vel_y)
    inx = nx >> 16; iny = ny >> 16
    for d in range(4):
        w.reacts = getattr(w, 'reacts', [0,0,0,0])
        w.reacts[d] = rc(inx, iny, d)
        if inx < 4: w.reacts[RF_RIGHT] += 5
        elif inx > LW - 5: w.reacts[RF_LEFT] += 5
        if iny < 5: w.reacts[RF_DOWN] += 5
        elif iny > LH - 6: w.reacts[RF_UP] += 5
    ru = w.reacts[RF_UP]; rd = w.reacts[RF_DOWN]; rl = w.reacts[RF_LEFT]; rr = w.reacts[RF_RIGHT]
    if rd < 2 and ru > 0 and (rl > 0 or rr > 0):
        w.pos_y = i32(w.pos_y - (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        w.reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); w.reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    if w.reacts[RF_UP] < 2 and w.reacts[RF_DOWN] > 0 and (w.reacts[RF_LEFT] > 0 or w.reacts[RF_RIGHT] > 0):
        w.pos_y = i32(w.pos_y + (1<<16))
        ny = i32(w.pos_y + w.vel_y); iny = ny >> 16
        w.reacts[RF_LEFT] = rc(inx, iny, RF_LEFT); w.reacts[RF_RIGHT] = rc(inx, iny, RF_RIGHT)
    # aiming
    if w.aim_speed != 0:
        w.aim_angle = i32(w.aim_angle + w.aim_speed)
        if not up and not down:
            w.aim_speed = tdiv(w.aim_speed * AF_M, AF_D)
        if w.direction == 1:
            if (w.aim_angle >> 16) > AMAXR: w.aim_speed = 0; w.aim_angle = AMAXR << 16
            if (w.aim_angle >> 16) < AMINR: w.aim_speed = 0; w.aim_angle = AMINR << 16
        else:
            if (w.aim_angle >> 16) < AMAXL: w.aim_speed = 0; w.aim_angle = AMAXL << 16
            if (w.aim_angle >> 16) > AMINL: w.aim_speed = 0; w.aim_angle = AMINL << 16
    if up:
        if w.direction == 0:
            if w.aim_speed < MAIMV: w.aim_speed = i32(w.aim_speed + AACC)
        else:
            if w.aim_speed > -MAIMV: w.aim_speed = i32(w.aim_speed - AACC)
    if down:
        if w.direction == 1:
            if w.aim_speed < MAIMV: w.aim_speed = i32(w.aim_speed + AACC)
        else:
            if w.aim_speed > -MAIMV: w.aim_speed = i32(w.aim_speed - AACC)
    # jump
    if jump and not prev_jump and w.reacts[RF_UP] > 0 and w.able_jump:
        w.vel_y = i32(w.vel_y - JUMP); w.able_jump = 0
    if not jump: w.able_jump = 1
    prev_jump = jump
    # physics
    if w.reacts[RF_UP] > 0:
        w.vel_x = tdiv(w.vel_x * FRIC_M, FRIC_D)
    ax = abs(w.vel_x); ay = abs(w.vel_y)
    rh = w.reacts[RF_LEFT if w.vel_x >= 0 else RF_RIGHT]
    rv = w.reacts[RF_UP if w.vel_y >= 0 else RF_DOWN]
    if w.vel_x and rh:
        w.vel_x = i32(-tdiv(w.vel_x, 3)) if ax > BOUNCE else 0
    if w.vel_y and rv:
        w.vel_y = i32(-tdiv(w.vel_y, 3)) if ay > BOUNCE else 0
    if w.reacts[RF_UP] == 0:
        w.vel_y = i32(w.vel_y + GRAVITY)
    if w.reacts[RF_LEFT if w.vel_x >= 0 else RF_RIGHT] < 2:
        w.pos_x = i32(w.pos_x + w.vel_x)
    if w.reacts[RF_UP if w.vel_y >= 0 else RF_DOWN] < 2:
        w.pos_y = i32(w.pos_y + w.vel_y)
    # movement
    w.animate = 1 if (left or right) else 0
    if left and not right:
        if w.vel_x > -MAXV: w.vel_x = i32(w.vel_x - WALK)
        if w.direction != 0:
            w.aim_speed = 0
            if w.aim_angle >= (64<<16): w.aim_angle = i32((128<<16) - w.aim_angle)
            w.direction = 0
    if not left and right:
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

# scripted trace identical to ROM AUTOTEST
for frame in range(720):
    ph = frame % 420
    L = 1 if (ph >= 210 and ph < 330) else 0
    R = 1 if ph < 120 else 0
    U = 1 if ph >= 330 else 0
    J = 1 if ph == 120 else 0
    process(L, R, U, 0, J, frame)
    if True:
        print("F frame=%d wpos=%d,%d wvel=%d,%d dir=%d frame_idx=%d" % (
            frame+1, w.pos_x >> 16, w.pos_y >> 16, w.vel_x, w.vel_y, w.direction, w.cur))
