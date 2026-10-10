// ============ slice 4: combat (weapons, worm damage, explosion sobjects, blood) =====
// Table-driven mirror of weapon.cpp / nobject.cpp / bobject.cpp / sobject.cpp with
// shared upstream mwc stream. Params come from assets/combat.h (baked TC).
// Omitted symmetrically (not in RNG stream on either side): bonuses, ninjarope, AI.

#ifdef COMBAT
#include "../assets/combat.h"
#endif

#define MAX_WOBJ 256
#define MAX_NOBJ 512
#define MAX_BOBJ 700
#define MAX_SOBJ 700

typedef struct {
    int32_t x, y;       // 16.16
    int32_t vx, vy;
    int cur_frame;
    int time_left;
    int owner;
    int type;           // c_wparam index
    uint8_t has_hit;
    uint8_t used;
} WObj;
static WObj wobj[MAX_WOBJ];

typedef struct {
    int32_t x, y;
    int32_t vx, vy;
    int cur_frame;
    int time_left;
    int type;           // c_nparam index
    uint8_t used;
} NObj;
static NObj nobj[MAX_NOBJ];

typedef struct {
    int32_t x, y;
    int32_t vx, vy;
    int color;
    uint8_t used;
} BObj;
static BObj bobj[MAX_BOBJ];

typedef struct {
    int x, y;           // int (sobject uses integer pixel pos)
    int cur_frame;
    int anim_delay;
    int type;           // c_sparam index
    uint8_t used;
} SObj;
static SObj sobj[MAX_SOBJ];

// dirtball weapon (legacy slice-3 constants; used when COMBAT off)
#define W_GRAVITY     1000
#define W_SPEED       200
#define W_DIST        12000
#define W_DELAY       20
#define W_LOADING     225
#define W_AMMO        5
#define W_STARTFRAME  87
#define W_BOUNCE      30
#define W_TTIME       80
#define W_TTIMEV      20
#define W_SPLINTER    5
#define W_DIRT        4
#define W_FIRECONE    0
// dirt nobject
#define N_TTIME       1
#define N_DIRT        4

#define MAX_WWP 5
static struct { int ammo, delay_left, loading_left; } wwp[MAX_WWP];
static int fire_cone = 0;
static uint8_t prev_fire = 0;
static int leave_shell_timer = 0;
static int cur_weapon = IDX_DIRTBALL; // current weapon index

static int8_t w_available[MAX_WWP];    // which weapon slots are enabled
static int8_t w_slot_widx[MAX_WWP];    // slot -> c_wparam index

static void correct_shadow(int x1, int y1, int x2, int y2) {
    if (x1 < 0) x1 = 0;
    if (y1 < 3) y1 = 3;
    if (x2 > LW-3) x2 = LW-3;
    if (y2 > LH) y2 = LH;
    for (int x = x1; x < x2; ++x)
        for (int y = y1; y < y2; ++y) {
            int i = y*LW + x;
            int j = (y-3)*LW + x+3;
            uint8_t pix = level_data[i];
            if ((level_mat[i] & M_SEE) && (level_mat[j] & (M_DIRT|M_DIRT2|M_ROCK))) {
                uint8_t v = (uint8_t)(pix + 4);
                level_data[i] = v; level_mat[i] = mat_flags[v];
            } else if (pix >= 164 && pix <= 167 && !(level_mat[j] & (M_DIRT|M_DIRT2|M_ROCK))) {
                uint8_t v = (uint8_t)(pix - 4);
                level_data[i] = v; level_mat[i] = mat_flags[v];
            }
        }
}

static int dirtrock_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 1;
    return (level_mat[idx] & (M_DIRT|M_DIRT2|M_ROCK)) != 0;
}
static int rock_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 1;
    return (level_mat[idx] & M_ROCK) != 0;
}
static int anydirt_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 0;
    return (level_mat[idx] & (M_DIRT|M_DIRT2)) != 0;
}
static int inside_lvl(int x, int y) { return x >= 0 && x < LW && y >= 0 && y < LH; }

// pool alloc: exactObjectList getFreeObject (lowest free) / newObjectReuse (last fallback)
static int wobj_new(void) { for (int i = 0; i < MAX_WOBJ; ++i) if (!wobj[i].used) { wobj[i].used = 1; return i; } return MAX_WOBJ-1; }
static int nobj_new(void) { for (int i = 0; i < MAX_NOBJ; ++i) if (!nobj[i].used) { nobj[i].used = 1; return i; } return MAX_NOBJ-1; }
static int sobj_new(void) { for (int i = 0; i < MAX_SOBJ; ++i) if (!sobj[i].used) { sobj[i].used = 1; return i; } return MAX_SOBJ-1; }
static void wobj_free(int i) { wobj[i].used = 0; }
static void nobj_free(int i) { nobj[i].used = 0; }
static void sobj_free(int i) { sobj[i].used = 0; }

// FastObjectList for bobjects: getFreeObject appends at count (NOT lowest-free!).
static int bobj_count = 0;
static int bobj_new(void) {
    if (bobj_count >= MAX_BOBJ) return MAX_BOBJ - 1;  // newObjectReuse limit fallback
    int i = bobj_count++;
    bobj[i].used = 1;
    return i;
}
static void bobj_free_idx(int i) {
    // FastObjectList::free swaps arr[--count] into this slot
    if (i < bobj_count - 1) bobj[i] = bobj[bobj_count - 1];
    bobj[bobj_count - 1].used = 0;
    bobj_count--;
}

static void level_set_pixel(int x, int y, uint8_t v) {
    if (x < 0 || x >= LW || y < 0 || y >= LH) return;
    level_data[y*LW + x] = v;
    level_mat[y*LW + x] = mat_flags[v];
}

// ---------- sobject blow-up damage (small_explosion etc.) ----------
static void sobj_create(int t, int x, int y, int owner_idx);

// ---------- nobject create helpers (create1/create2 + create core) ----------
static void nobj_create_core(int t, int32_t vx, int32_t vy, int32_t px, int32_t py, int color, int owner);
static void nobj_create1(int t, int32_t vx, int32_t vy, int32_t px, int32_t py, int color, int owner) {
    int dist = c_nparam[t][NK_distribution];
    if (dist) {
        vx += dist - (int32_t)rnd(dist * 2u);
        vy += dist - (int32_t)rnd(dist * 2u);
    }
    nobj_create_core(t, vx, vy, px, py, color, owner);
}
static void nobj_create2(int t, int angle, int32_t bvx, int32_t bvy, int32_t px, int32_t py, int color, int owner) {
    const int32_t* p = c_nparam[t];
    int speed = p[NK_speed], speedV = p[NK_speedV];
    int realSpeed = speed - (int)rnd(speedV);
    int32_t vx = bvx + (int32_t)(((int64_t)cs_x[angle & 127] * realSpeed) / 100);
    int32_t vy = bvy + (int32_t)(((int64_t)cs_y[angle & 127] * realSpeed) / 100);
    int dist = p[NK_distribution];
    if (dist) {
        vx += (int32_t)rnd(dist * 2u) - dist;
        vy += (int32_t)rnd(dist * 2u) - dist;
    }
    nobj_create_core(t, vx, vy, px, py, color, owner);
    // create2: obj.pos += obj.vel AFTER create
    int last = MAX_NOBJ - 1;
    for (int i = MAX_NOBJ - 1; i >= 0; --i) if (nobj[i].used && nobj[i].type == t) { last = i; break; }
    // NOTE: caller must re-position the newest; handled in core return - we add velocity offset here:
    (void)last;
}

// find a free nobj slot, initialize; returns index
static int nobj_new_init(int t, int32_t vx, int32_t vy, int32_t px, int32_t py, int color) {
    const int32_t* p = c_nparam[t];
    int i = nobj_new();
    nobj[i].x = px; nobj[i].y = py; nobj[i].vx = vx; nobj[i].vy = vy; nobj[i].type = t;
    int sf = p[NK_startFrame];
    if (sf > 0) {
        nobj[i].cur_frame = (int)rnd(p[NK_numFrames] + 1u);
    } else if (color != 0) {
        nobj[i].cur_frame = color;
    } else {
        nobj[i].cur_frame = p[NK_colorBullets];
    }
    nobj[i].time_left = p[NK_timeToExplo];
    if (p[NK_timeToExploV]) nobj[i].time_left -= (int)rnd(p[NK_timeToExploV]);
    return i;
}
static void nobj_create_core(int t, int32_t vx, int32_t vy, int32_t px, int32_t py, int color, int owner) {
    (void)owner;
    int i = nobj_new_init(t, vx, vy, px, py, color);
    // create2 post-offset needs it: for create2 the pos advance happens after; caller sets bvx.
    // We apply the create2 "+=vel" only when called via nobj_create2: use a flag via py marker.
    (void)i;
}

// ---------- explosion sobject ----------
static void sobj_create(int t, int x, int y, int owner_idx) {
    int i = sobj_new();
    const int32_t* p = c_sparam[t];
    int dr = p[SK_detectRange];
    sobj[i].x = x; sobj[i].y = y; sobj[i].type = t; sobj[i].cur_frame = 0; sobj[i].anim_delay = p[SK_animDelay];
    if (p[SK_flash] > 0) g_screen_flash = p[SK_flash];
    (void)owner_idx;

    int damage = p[SK_damage];
    if (damage > 0) {
        // worms (single worm here: worm)
        int wix = worm.pos_x >> 16, wiy = worm.pos_y >> 16;
        if (wix < x + dr && wix > x - dr && wiy < y + dr && wiy > y - dr) {
            int delta = wix - x;
            int power = dr - (delta < 0 ? -delta : delta);
            int power_sum = power;
            if (worm.vel_x > -131072 && worm.vel_x < 131072) { // |vel| < itof(2)=131072
                if (delta > 0) worm.vel_x += p[SK_blowAway] * power; else worm.vel_x -= p[SK_blowAway] * power;
            }
            delta = wiy - y;
            power = dr - (delta < 0 ? -delta : delta);
            power_sum = (power_sum + power) / 2;
            if (worm.vel_y > -131072 && worm.vel_y < 131072) {
                if (delta > 0) worm.vel_y += p[SK_blowAway] * power; else worm.vel_y -= p[SK_blowAway] * power;
            }
            int z = damage * power_sum;
            if (dr) z /= dr;
            worm_do_damage(z, owner_idx);
            int blood_amount = 100 * power_sum / 100; // settings->blood=100
            if (blood_amount > 0) {
                for (int b = 0; b < blood_amount; ++b) {
                    int angle = (int)rnd(128);
                    int32_t bvx = worm.vel_x / 3, bvy = worm.vel_y / 3;
                    int j = nobj_new_init(IDX_BLOOD, bvx, bvy, worm.pos_x, worm.pos_y, 0);
                    nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy; // create2 post
                }
            }
            if (rnd(3) == 0) { rnd(3); } // snd rand burn (sound off)
        }
        // wobjects affectByExplosions
        int obj_blow = p[SK_blowAway] / 3;
        for (int k = 0; k < MAX_WOBJ; ++k) if (wobj[k].used && c_wparam[wobj[k].type][WK_affectByExplosions]) {
            int ox = wobj[k].x >> 16, oy = wobj[k].y >> 16;
            if (ox < x + dr && ox > x - dr && oy < y + dr && oy > y - dr) {
                int d = ox - x, pw = dr - (d < 0 ? -d : d);
                if (pw > 0) { if (d > 0) wobj[k].vx += obj_blow * pw; else if (d < 0) wobj[k].vx -= obj_blow * pw; }
                d = oy - y; pw = dr - (d < 0 ? -d : d);
                if (pw > 0) { if (d > 0) wobj[k].vy += obj_blow * pw; else wobj[k].vy -= obj_blow * pw; }
                if (c_wparam[wobj[k].type][WK_chainExplosion]) wobj_blow_up(k, owner_idx);
            }
        }
        // nobjects affectByExplosions
        for (int k = 0; k < MAX_NOBJ; ++k) if (nobj[k].used && c_nparam[nobj[k].type][NK_affectByExplosions]) {
            int ox = nobj[k].x >> 16, oy = nobj[k].y >> 16;
            if (ox < x + dr && ox > x - dr && oy < y + dr && oy > y - dr) {
                int d = ox - x, pw = dr - (d < 0 ? -d : d);
                if (pw > 0) { if (d > 0) nobj[k].vx += obj_blow * pw; else if (d < 0) nobj[k].vx -= obj_blow * pw; }
                d = oy - y; pw = dr - (d < 0 ? -d : d);
                if (pw > 0) { if (d > 0) nobj[k].vy += obj_blow * pw; else nobj[k].vy -= obj_blow * pw; }
            }
        }
        // dirt spatter: rect x-width..x+width, width=detectRange/2
        int width = dr / 2;
        int rx1 = x - width, ry1 = y - width, rx2 = x + width + 1, ry2 = y + width + 1;
        if (rx1 < 0) rx1 = 0; if (ry1 < 0) ry1 = 0;
        if (rx2 > LW) rx2 = LW; if (ry2 > LH) ry2 = LH;
        for (int py = ry1; py < ry2; ++py)
            for (int px = rx1; px < rx2; ++px) {
                if (anydirt_at(px, py) && rnd(8) == 0) {
                    int pix = level_data[py*LW + px];
                    int angle = (int)rnd(128);
                    int j = nobj_new_init(IDX_PART2, 0, 0, px << 16, py << 16, pix);
                    nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy; // create2 post
                }
            }
    } // damage

    if (p[SK_dirtEffect] >= 0) {
        dirt_effect(p[SK_dirtEffect], x - 7, y - 7);
        correct_shadow(x - 10, y - 10, x + 11, y + 11);
    }
}

// ---------- wobject blow up (splinters + createOnExp + dirt) ----------
static void wobj_blow_up(int i, int cause_idx) {
    WObj* o = &wobj[i];
    int ti = o->type;
    const int32_t* p = c_wparam[ti];
    int px = o->x >> 16, py = o->y >> 16;
    int32_t velX = o->vx, velY = o->vy;
    wobj_free(i);

    int coe = p[WK_createOnExp];
    if (coe >= 0) sobj_create(coe, px, py, cause_idx);

    int splinters = p[WK_splinterAmount];
    if (splinters > 0 && p[WK_splinterScatter] == 0) {
        int stype = p[WK_splinterType];
        for (int k = 0; k < splinters; ++k) {
            int angle = (int)rnd(128);
            int colorSub = (int)rnd(2);
            const int32_t* np = c_nparam[stype];
            int realSpeed = np[NK_speed] - (int)rnd(np[NK_speedV]);
            int32_t vx = (int32_t)(((int64_t)cs_x[angle & 127] * realSpeed) / 100);
            int32_t vy = (int32_t)(((int64_t)cs_y[angle & 127] * realSpeed) / 100);
            int dist = np[NK_distribution];
            if (dist) { vx += (int32_t)rnd(dist * 2u) - dist; vy += (int32_t)rnd(dist * 2u) - dist; }
            int color = p[WK_splinterColour] - colorSub;
            int j = nobj_new_init(stype, vx, vy, o->x, o->y, color);
            nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy; // create2 pos+=vel
        }
    } else if (splinters > 0) {
        int stype = p[WK_splinterType];
        for (int k = 0; k < splinters; ++k) {
            int colorSub = (int)rnd(2);
            const int32_t* np = c_nparam[stype];
            int dist = np[NK_distribution];
            int32_t vx = velX, vy = velY;
            if (dist) { vx += dist - (int32_t)rnd(dist * 2u); vy += dist - (int32_t)rnd(dist * 2u); }
            int color = p[WK_splinterColour] - colorSub;
            int j = nobj_new_init(stype, vx, vy, o->x, o->y, color);
        }
    }

    if (p[WK_dirtEffect] >= 0) {
        dirt_effect(p[WK_dirtEffect], px - 7, py - 7);
        correct_shadow(px - 10, py - 10, px + 11, py + 11);
    }
}

// ---------- worm sprite worm-material hit test ----------
static int check_for_spec_worm_hit(int x, int y, int dist) {
    if (!worm.visible) return 0;
    int f = 16 + worm.current_frame;
    if (f >= LARGE_FRAMES) f = 16;
    const uint8_t* ws = large_stamps + (size_t)f * 256;
    int deltaX = x - (worm.pos_x >> 16) + 7;
    int deltaY = y - (worm.pos_y >> 16) + 5;
    int x1 = deltaX - dist, y1 = deltaY - dist, x2 = deltaX + dist + 1, y2 = deltaY + dist + 1;
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if (x2 > 16) x2 = 16; if (y2 > 16) y2 = 16;
    for (int cy = y1; cy < y2; ++cy)
        for (int cx = x1; cx < x2; ++cx)
            if (mat_flags[ws[cy*16 + cx]] & M_WORM) return 1;
    return 0;
}

// ---------- fire wobject (generic STNormal) ----------
static void fire_wobj(void) {
    const int32_t* p = c_wparam[cur_weapon];
    int aa = (int)(worm.aim_angle >> 16) & 127;
    int speed = p[WK_speed]; if (p[WK_affectByWorm] && speed < 100) speed = 100;
    int fx = (int)(((int64_t)cs_x[aa] * (p[WK_detectDistance] + 5))) + worm.pos_x;
    int fy = (int)(((int64_t)cs_y[aa] * (p[WK_detectDistance] + 5))) + worm.pos_y - (1 << 16);
    int fvx = p[WK_affectByWorm] ? (int)(((int64_t)worm.vel_x * 100) / speed) : 0;
    int fvy = p[WK_affectByWorm] ? (int)(((int64_t)worm.vel_y * 100) / speed) : 0;
    int parts = p[WK_parts];
    for (int k = 0; k < parts; ++k) {
        int vx = (int)(((int64_t)cs_x[aa] * speed) / 100) + fvx;
        int vy = (int)(((int64_t)cs_y[aa] * speed) / 100) + fvy;
        int dist = p[WK_distribution];
        if (dist) { vx += (int)rnd(dist * 2u) - dist; vy += (int)rnd(dist * 2u) - dist; }
        int cur;
        if (p[WK_startFrame] >= 0) {
            if (p[WK_shotType] == 0) { cur = 0; if (p[WK_loopAnim] && p[WK_numFrames]) cur = (int)rnd(p[WK_numFrames] + 1u); }
            else cur = 0;
        } else {
            cur = p[WK_colorBullets] - (int)rnd(2);
        }
        int i = wobj_new();
        wobj[i].x = fx; wobj[i].y = fy; wobj[i].vx = vx; wobj[i].vy = vy;
        wobj[i].cur_frame = cur; wobj[i].owner = 0; wobj[i].type = cur_weapon; wobj[i].has_hit = 0;
        wobj[i].time_left = p[WK_timeToExplo];
        if (p[WK_timeToExploV]) wobj[i].time_left -= (int)rnd(p[WK_timeToExploV]);
    }
    // recoil
    int recoil = p[WK_recoil];
    worm.vel_x -= (int)(((int64_t)cs_x[aa] * recoil) / 100);
    worm.vel_y -= (int)(((int64_t)cs_y[aa] * recoil) / 100);
    // leaveShells
    if (p[WK_leaveShells] > 0 && rnd(p[WK_leaveShells]) == 0) leave_shell_timer = p[WK_leaveShellDelay];
    fire_cone = p[WK_fireCone];
}

static void worm_try_fire(uint8_t fire_held) {
    int slot = 0; // single active weapon slot for the test worm
    if (fire_held && !prev_fire && wwp[slot].loading_left == 0 && wwp[slot].delay_left <= 0) {
        --wwp[slot].ammo;
        wwp[slot].delay_left = c_wparam[cur_weapon][WK_delay];
        fire_wobj();
    }
    prev_fire = fire_held;
}

static void worm_weapons(void) {
    int slot = 0;
    if (wwp[slot].delay_left >= 0) --wwp[slot].delay_left;
    if (wwp[slot].ammo <= 0) {
        int lt = (100 * c_wparam[cur_weapon][WK_loadingTime]) / 100; if (!lt) lt = 1;
        wwp[slot].loading_left = lt;
        wwp[slot].ammo = c_wparam[cur_weapon][WK_ammo];
    }
    if (wwp[slot].loading_left > 0) --wwp[slot].loading_left;
    if (fire_cone > 0) --fire_cone;
    // shells
    if (leave_shell_timer > 0) {
        if (--leave_shell_timer <= 0) {
            int32_t vy = -(int32_t)rnd(20000);
            int32_t vx = (int32_t)rnd(16000) - 8000;
            int j = nobj_new_init(IDX_SHELLS, vx, vy, worm.pos_x, worm.pos_y, 0);
            nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy; // create1 doesn't advance pos
            // NOTE: create1 does NOT add pos+=vel; leave as is above sets pos directly.
        }
    }
}

// ---------- wobject process (generic) ----------
static void process_wobjects(uint64_t cycle) {
    (void)cycle;
    for (int i = 0; i < MAX_WOBJ; ++i) {
        if (!wobj[i].used) continue;
        WObj* o = &wobj[i];
        const int32_t* p = c_wparam[o->type];
        int do_explode = 0, do_remove = 0;
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int bounce = p[WK_bounce];
        if (bounce > 0) {
            int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;
            if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
                if (bounce != 100) { o->vx = -(o->vx * bounce) / 100; o->vy = (o->vy * 4) / 5; }
                else o->vx = -o->vx;
            }
            if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
                if (bounce != 100) { o->vy = -(o->vy * bounce) / 100; o->vx = (o->vx * 4) / 5; }
                else o->vy = -o->vy;
            }
        }
        // multSpeed
        if (p[WK_multSpeed] != 100) { o->vx = o->vx * p[WK_multSpeed] / 100; o->vy = o->vy * p[WK_multSpeed] / 100; }
        // objTrail: none for dirtball/handgun
        // recompute inewPos, clip pos, hit test
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = (LW - 1) << 16;
        if (ny >= LH) o->y = (LH - 1) << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            if (bounce == 0) {
                if (p[WK_explGround]) do_explode = 1; else { o->vx = 0; o->vy = 0; }
            }
            // bounce!=0: no velocity change here
        } else {
            o->vy += p[WK_gravity];
            if (p[WK_numFrames] > 0 && ((cycle & 7) == 0)) {
                if (!p[WK_loopAnim]) { if (++o->cur_frame > p[WK_numFrames]) o->cur_frame = 0; }
                else { if (o->vx < 0) { if (--o->cur_frame < 0) o->cur_frame = p[WK_numFrames]; }
                       else if (o->vx > 0) { if (++o->cur_frame > p[WK_numFrames]) o->cur_frame = 0; } }
            }
        }
        if (p[WK_timeToExplo] > 0) { if (--o->time_left < 0) do_explode = 1; }
        // worm hit
        if ((p[WK_hitDamage] || p[WK_blowAway] || p[WK_bloodOnHit] || p[WK_wormCollide])
            && check_for_spec_worm_hit(o->x >> 16, o->y >> 16, p[WK_detectDistance])) {
            worm.vel_x += (int)(((int64_t)o->vx * p[WK_blowAway]) / 100);
            worm.vel_y += (int)(((int64_t)o->vy * p[WK_blowAway]) / 100);
            worm_do_damage(p[WK_hitDamage], o->owner);
            if (!o->has_hit) o->has_hit = 1;
            int blood = (100 * p[WK_bloodOnHit]) / 100;
            for (int b = 0; b < blood; ++b) {
                int angle = (int)rnd(128);
                int32_t bvx = o->vx / 3, bvy = o->vy / 3;
                int j = nobj_new_init(IDX_BLOOD, bvx, bvy, worm.pos_x, worm.pos_y, 0);
                nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy;
            }
            if (p[WK_hitDamage] > 0 && worm.health > 0 && rnd(3) == 0) { rnd(3); } // snd burn
            if (p[WK_wormCollide] && rnd(p[WK_wormCollide]) == 0) {
                if (p[WK_wormExplode]) do_explode = 1;
                do_remove = 1;
            }
        }
        if (do_explode) { wobj_blow_up(i, o->owner); }
        else if (do_remove) { wobj_free(i); }
    }
}

// ---------- nobject process (generic) ----------
static void nobj_explode(int i) {
    NObj* o = &nobj[i];
    const int32_t* p = c_nparam[o->type];
    int px = o->x >> 16, py = o->y >> 16;
    int coe = p[NK_createOnExp];
    if (coe >= 0) sobj_create(coe, px, py, 0);
    if (p[NK_dirtEffect] >= 0) { dirt_effect(p[NK_dirtEffect], px - 7, py - 7); correct_shadow(px - 10, py - 10, px + 11, py + 11); }
    int spl = p[NK_splinterAmount];
    if (spl > 0) {
        int st = p[NK_splinterType];
        for (int k = 0; k < spl; ++k) {
            int angle = (int)rnd(128);
            int cs = (int)rnd(2);
            int color = p[NK_splinterColour]; if (color < 0) color = -cs; else color = color - cs;
            const int32_t* np = c_nparam[st];
            int realSpeed = np[NK_speed] - (int)rnd(np[NK_speedV]);
            int32_t vx = (int32_t)(((int64_t)cs_x[angle & 127] * realSpeed) / 100);
            int32_t vy = (int32_t)(((int64_t)cs_y[angle & 127] * realSpeed) / 100);
            int dist = np[NK_distribution];
            if (dist) { vx += (int32_t)rnd(dist * 2u) - dist; vy += (int32_t)rnd(dist * 2u) - dist; }
            int j = nobj_new_init(st, vx, vy, o->x, o->y, color);
            nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy;
        }
    }
    nobj_free(i);
}

static void process_nobjects(uint64_t cycle) {
    for (int i = 0; i < MAX_NOBJ; ++i) {
        if (!nobj[i].used) continue;
        NObj* o = &nobj[i];
        const int32_t* p = c_nparam[o->type];
        int bounced = 0, do_explode = 0;
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;
        int bounce = p[NK_bounce];
        if (bounce > 0) {
            if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
                if (bounce != 100) { o->vx = -(o->vx * bounce) / 100; o->vy = (o->vy * 4) / 5; }
                else o->vx = -o->vx;
                bounced = 1;
            }
            if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
                if (bounce != 100) { o->vy = -(o->vy * bounce) / 100; o->vx = (o->vx * 4) / 5; }
                else o->vy = -o->vy;
                bounced = 1;
            }
        }
        // bloodTrail
        if (p[NK_bloodTrail] && p[NK_bloodTrailDelay] > 0 && (cycle % p[NK_bloodTrailDelay]) == 0)
            create_bobject(o->x, o->y, o->vx / 4, o->vy / 4);
        // leaveObj trail (sobject)
        if (!bounced && p[NK_leaveObjDelay] != 0 && p[NK_leaveObj] >= 0 && (cycle % p[NK_leaveObjDelay]) == 0)
            sobj_create(p[NK_leaveObj], o->x >> 16, o->y >> 16, 0);
        // recompute inewPos, clip
        nx = (o->x + o->vx) >> 16; ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = (LW - 1) << 16;
        if (ny >= LH) o->y = (LH - 1) << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            if (bounce == 0) {
                if (p[SK_unused0]) {} // placeholder
            }
            // generic: on ground hit
            if (bounce == 0) {
                if (p[NK_explGround]) do_explode = 1;
                else { o->vx = 0; o->vy = 0; }
            }
            if (bounce > 0) {
                // dirt: explGround handled below via timeLeft
                if (p[NK_explGround]) {
                    int sf = p[NK_startFrame];
                    if (sf > 0 && p[NK_drawOnMap]) {
                        blit_image_on_map(sf + o->cur_frame, ix - 3, iy - 3, 7, 7);
                        correct_shadow(ix - 8, iy - 8, ix + 9, iy + 9);
                    }
                    do_explode = 1;
                }
            }
        } else {
            o->vy += p[NK_gravity];
            if (p[NK_numFrames] > 0 && ((cycle & 7) == 0)) {
                if (!p[NK_bounce]) {} // no-op
                if (o->vx > 0) { if (++o->cur_frame > p[NK_numFrames]) o->cur_frame = 0; }
                else if (o->vx < 0) { if (--o->cur_frame < 0) o->cur_frame = p[NK_numFrames]; }
            }
        }
        if (p[NK_timeToExplo] > 0) { if (--o->time_left <= 0) do_explode = 1; }
        // worm hit (nobject)
        if (p[NK_hitDamage] > 0 && check_for_spec_worm_hit(o->x >> 16, o->y >> 16, p[NK_detectDistance])) {
            worm.vel_x += (int)(((int64_t)o->vx * p[NK_blowAway]) / 100);
            worm.vel_y += (int)(((int64_t)o->vy * p[NK_blowAway]) / 100);
            worm_do_damage(p[NK_hitDamage], 0);
            if (p[NK_hitDamage] > 0 && worm.health > 0 && rnd(3) == 0) rnd(3);
            int blood = (100 * p[NK_bloodOnHit]) / 100;
            for (int b = 0; b < blood; ++b) {
                int angle = (int)rnd(128);
                int32_t bvx = o->vx / 3, bvy = o->vy / 3;
                int j = nobj_new_init(IDX_BLOOD, bvx, bvy, worm.pos_x, worm.pos_y, 0);
                nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy;
            }
            if (p[NK_wormExplode]) do_explode = 1;
            else if (p[NK_wormDestroy]) nobj_free(i);
        }
        if (do_explode) { nobj_explode(i); }
    }
}

static void create_bobject(int32_t px, int32_t py, int32_t vx, int32_t vy) {
    int i = bobj_new();
    bobj[i].color = (int)rnd(C_NUMBLOOD) + C_FIRSTBLOOD;
    bobj[i].x = px; bobj[i].y = py; bobj[i].vx = vx; bobj[i].vy = vy;
}

static void process_bobjects(void) {
    // iterate indices 0..bobj_count-1 (FastObjectList); swap-remove shrinks count
    for (int i = 0; i < bobj_count; ) {
        BObj* o = &bobj[i];
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int remove = 0;
        if (!inside_lvl(ix, iy)) remove = 1;
        else {
            int c = level_data[iy*LW + ix];
            if ((mat_flags[c] & M_BACK) == 0) remove = (level_mat[iy*LW + ix] & M_BACK) != 0; // background check via mat
            // background via material flag
            if ((level_mat[iy*LW + ix] & M_BACK)) o->vy += C_BOBJGRAV;
            if ((c >= 1 && c <= 2) || (c >= 77 && c <= 79)) { level_set_pixel(ix, iy, (uint8_t)(77 + rnd(3))); remove = 1; }
            else if (anydirt_at(ix, iy)) { level_set_pixel(ix, iy, (uint8_t)(82 + rnd(3))); remove = 1; }
            else if (rock_at(ix, iy)) { level_set_pixel(ix, iy, (uint8_t)(85 + rnd(3))); remove = 1; }
        }
        if (remove) { bobj_free_idx(i); /* don't ++i, swapped-in element now at i */ }
        else ++i;
    }
}

static void process_sobjects(uint64_t cycle) {
    (void)cycle;
    for (int i = 0; i < MAX_SOBJ; ++i) {
        if (!sobj[i].used) continue;
        SObj* o = &sobj[i];
        const int32_t* p = c_sparam[o->type];
        if (--o->anim_delay <= 0) {
            o->anim_delay = p[SK_animDelay];
            ++o->cur_frame;
            if (o->cur_frame > p[SK_numFrames]) sobj_free(i);
        }
    }
}
