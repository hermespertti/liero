// liero64 slice 1: terrain generation + RDPQ run-length renderer.
// Generator mirrors src/game/level.cpp (generateDirtPattern/generateRandom/
// makeShadow) and blit.cpp (blitStone/drawDirtEffect) byte-for-byte with the
// upstream mwc RNG. Probe prints an FNV-1a hash of the level for the host
// reference (tools/bake_assets.py) to check parity.
#include <libdragon.h>
#include <stdio.h>
#include <string.h>
#include "../assets/baked.h"
#include "../assets/combat.h"

#define LW 504
#define LH 350
#define VW 320
#define VH 240

// ---- material flags (mirror material.hpp enum) ----
#define M_DIRT   1
#define M_DIRT2  2
#define M_ROCK   4
#define M_BACK   8
#define M_SEE    16

static uint32_t fnv(const uint8_t* p, size_t n);
static void stage_hash(const char* tag);

static uint8_t level_data[LW*LH];
static uint8_t level_mat[LW*LH];

// ---- mwc RNG (gvl::mwc, exact) ----
static uint32_t rng_x = 0x1337, rng_c = 9413207;
static inline uint32_t rng_next(void) {
    uint64_t t = 2083801278ULL * rng_x + rng_c;
    rng_c = (uint32_t)(t >> 32);
    rng_x = (uint32_t)t;
    return rng_x;
}
static inline uint32_t rnd(uint32_t max) {
    return (uint32_t)(((uint64_t)rng_next() * max) >> 32);
}

static inline void sp(int x, int y, uint8_t v) {
    level_data[y*LW+x] = v;
    level_mat[y*LW+x] = mat_flags[v];
}
static inline uint8_t px(int x, int y) { return level_data[y*LW+x]; }

static void blit_stone(const uint8_t* img, int x, int y) {
    int x1 = x < 0 ? 0 : x, x2 = x+16 > LW ? LW : x+16;
    int y1 = y < 0 ? 0 : y, y2 = y+16 > LH ? LH : y+16;
    for (int my = y1; my < y2; ++my) {
        int sy = my - y;
        for (int mx = x1; mx < x2; ++mx) {
            uint8_t c = img[sy*16 + (mx - x)];
            if (c) sp(mx, my, c);
        }
    }
}

static void dirt_effect(int eff, int x, int y) {
    const TexEnt* tex = &tex_tab[eff];
    const uint8_t* t_frame = large_stamps + ((size_t)(tex->sFrame + rnd(tex->rFrame)) * 256);
    const uint8_t* m_frame = large_stamps + ((size_t)tex->mFrame * 256);
    int x1 = x < 0 ? 0 : x, x2 = x+16 > LW ? LW : x+16;
    int y1 = y < 0 ? 0 : y, y2 = y+16 > LH-1 ? LH-1 : y+16;  // clip (0,0,W,H-1)
    for (int my = y1; my < y2; ++my) {
        int sy = my - y;
        for (int mx = x1; mx < x2; ++mx) {
            uint8_t c = m_frame[sy*16 + (mx - x)];
            int i = my*LW + mx;
            uint8_t mf = level_mat[i];
            if (tex->nDrawBack) {
                if (c == 6 && (mf & (M_DIRT|M_DIRT2))) {
                    uint8_t v = t_frame[((my & 15) << 4) + (mx & 15)];
                    level_data[i] = v; level_mat[i] = mat_flags[v];
                } else if (c == 1) {
                    if (mf & M_DIRT2)      { level_data[i] = 2; level_mat[i] = mat_flags[2]; }
                    else if (mf & M_DIRT)  { level_data[i] = 1; level_mat[i] = mat_flags[1]; }
                }
            } else {
                if ((c == 10 || c == 6)) {
                    if (mf & M_BACK) {
                        uint8_t v = t_frame[((my & 15) << 4) + (mx & 15)];
                        level_data[i] = v; level_mat[i] = mat_flags[v];
                    }
                } else if (c == 2) {
                    if (mf & M_BACK) { level_data[i] = 2; level_mat[i] = mat_flags[2]; }
                } else if (c == 1) {
                    if (mf & M_BACK) { level_data[i] = 1; level_mat[i] = mat_flags[1]; }
                }
            }
        }
    }
}

static void generate_dirt_pattern(void) {
    sp(0, 0, (uint8_t)(rnd(7) + 12));
    for (int y = 1; y < LH; ++y)
        sp(0, y, (uint8_t)(((rnd(7) + 12) + px(0, y-1)) >> 1));
    for (int x = 1; x < LW; ++x)
        sp(x, 0, (uint8_t)(((rnd(7) + 12) + px(x-1, 0)) >> 1));
    for (int y = 1; y < LH; ++y)
        for (int x = 1; x < LW; ++x)
            sp(x, y, (uint8_t)((px(x-1, y) + px(x, y-1) + rnd(8) + 12) / 3));
    int count = rnd(100);
    for (int i = 0; i < count; ++i) {
        int x = rnd(LW) - 8, y = rnd(LH) - 8;
        const uint8_t* img = large_stamps + ((size_t)(rnd(4) + 69) * 256);
        for (int cy = 0; cy < 16; ++cy) {
            int my = cy + y;
            if (my >= LH) break;
            if (my < 0) continue;
            for (int cx = 0; cx < 16; ++cx) {
                int mx = cx + x;
                if (mx >= LW) break;
                if (mx < 0) continue;
                uint8_t srcPix = img[(cy << 4) + cx];
                if (srcPix > 0) {
                    uint8_t p = px(mx, my);
                    sp(mx, my, (p > 176 && p < 180) ? (uint8_t)((srcPix + p) / 2) : srcPix);
                }
            }
        }
    }

    count = rnd(15);
    for (int i = 0; i < count; ++i) {
        int x = rnd(LW) - 8;
        int y = rnd(LH) - 8;
        int which = rnd(4) + 56;   // separate statement: exact RNG order
        blit_stone(large_stamps + ((size_t)which * 256), x, y);
    }
}

static int is_no_rock(int size, int x, int y) {
    int x1 = x < 0 ? 0 : x, x2 = x+size+1 > LW ? LW : x+size+1;
    int y1 = y < 0 ? 0 : y, y2 = y+size+1 > LH ? LH : y+size+1;
    for (int yy = y1; yy < y2; ++yy)
        for (int xx = x1; xx < x2; ++xx)
            if (level_mat[yy*LW+xx] & M_ROCK) return 0;
    return 1;
}

static const int stone_tab[3][4] = {
    {98, 60, 61, 62},
    {63, 75, 85, 86},
    {89, 90, 97, 96}
};

static void make_shadow(void) {
    for (int x = 0; x < LW-3; ++x)
        for (int y = 3; y < LH; ++y) {
            int i = y*LW+x, j = (y-3)*LW+x+3;
            if ((level_mat[i] & M_SEE) && (level_mat[j] & (M_DIRT|M_DIRT2|M_ROCK)))
                sp(x, y, (uint8_t)(px(x, y) + 4));
            if (px(x, y) >= 12 && px(x, y) <= 18 && (level_mat[j] & M_ROCK)) {
                int v = px(x, y) - 2;
                sp(x, y, (uint8_t)(v < 12 ? 12 : v));
            }
        }
    for (int x = 0; x < LW; ++x)
        if (level_mat[(LH-1)*LW + x] & M_BACK)
            sp(x, LH-1, 13);
}

static void generate_random(void) {
    generate_dirt_pattern();
    stage_hash("dirt");


    int count = rnd(50) + 5;
    for (int i = 0; i < count; ++i) {
        int cx = rnd(LW) - 8, cy = rnd(LH) - 8;
        int dx = rnd(11) - 5, dy = rnd(5) - 2;
        int count2 = rnd(12);
        for (int j = 0; j < count2; ++j) {
            int count3 = rnd(5);
            for (int k = 0; k < count3; ++k) {
                cx += dx; cy += dy;
                dirt_effect(1, cx, cy);
            }
            cx -= (count3 + 1) * dx;
            cy -= (count3 + 1) * dy;
            cx += rnd(7) - 3;
            cy += rnd(15) - 7;
        }
    }
    stage_hash("trails");

    count = rnd(15) + 5;
    for (int i = 0; i < count; ++i) {
        int cx, cy;
        do {
            cx = rnd(LW) - 16;
            cy = (rnd(4) == 0) ? LH - 1 - rnd(20) : rnd(LH) - 16;
        } while (!is_no_rock(32, cx, cy));
        int rock = rnd(3);
        blit_stone(large_stamps + ((size_t)stone_tab[rock][0] * 256), cx, cy);
        blit_stone(large_stamps + ((size_t)stone_tab[rock][1] * 256), cx+16, cy);
        blit_stone(large_stamps + ((size_t)stone_tab[rock][2] * 256), cx, cy+16);
        blit_stone(large_stamps + ((size_t)stone_tab[rock][3] * 256), cx+16, cy+16);
    }
    stage_hash("bigrocks");

    count = rnd(25) + 5;
    for (int i = 0; i < count; ++i) {
        int cx, cy;
        do {
            cx = rnd(LW) - 8;
            cy = (rnd(5) == 0) ? LH - 1 - rnd(13) : rnd(LH) - 8;
        } while (!is_no_rock(15, cx, cy));
        int which = rnd(6) + 3;
        blit_stone(large_stamps + ((size_t)which * 256), cx, cy);
    }
    stage_hash("smallrocks");

    make_shadow();
    stage_hash("shadow");
}

static void stage_hash(const char* tag) {
    debugf("STAGE %s hash=%08lx\n", tag, (unsigned long)fnv(level_data, sizeof(level_data)));
}

// ---- rendering: per-scanline run-length flat fills ----
#ifdef PATTERN
static void render_view(int camx, int camy) {
    // deterministic test pattern: index = ((x>>4) + 2*y) & 255 mapped through palette
    (void)camx; (void)camy;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int y = 0; y < VH; ++y) {
        int x = 0;
        while (x < VW) {
            uint8_t idx = (uint8_t)(((x >> 4) + 2*y) & 255);
            int x0 = x;
            while (x < VW && (uint8_t)(((x >> 4) + 2*y) & 255) == idx) ++x;
            uint16_t c = pal_rgb555[idx];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(x0, y, x, y+1);   // exclusive coords (verified vs t3d-sdk examples)
        }
    }
}
#else
static void render_view(int camx, int camy) {
    for (int y = 0; y < VH; ++y) {
        int ly = camy + y;
        if (ly >= LH) break;
        const uint8_t* row = &level_data[ly*LW + camx];
        int w = LW - camx; if (w > VW) w = VW;
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_dithering(DITHER_NONE_NONE);
        int x = 0;
        while (x < w) {
            uint8_t idx = row[x];
            int x0 = x;
            while (x < w && row[x] == idx) ++x;
            uint16_t c = pal_rgb555[idx];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(x0, y, x, y+1);
        }
    }
}
#endif

#ifdef PATTERN
// STATIC: render once into a fixed surface; VI scans it continuously
// (display_show with 1 buffer). Avoids frame-buffer confusion in captures.
static surface_t pat_surf;
static void render_static(void) {
    rdpq_attach(&pat_surf, NULL);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int y = 0; y < VH; ++y) {
        int x = 0;
        while (x < VW) {
            uint8_t idx = (uint8_t)(((x >> 4) + 2*y) & 255);
            int x0 = x;
            while (x < VW && (uint8_t)(((x >> 4) + 2*y) & 255) == idx) ++x;
            uint16_t c = pal_rgb555[idx];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(x0, y, x, y+1);
        }
    }
    rdpq_detach_show();
}
#endif

static uint32_t fnv(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// =================== slice 2: worm (upstream process order, fixed 16.16) ===
#define WORM_FRIC_M 89
#define WORM_FRIC_D 100
#define GRAVITY     1500
#define JUMP_FORCE  56064
#define WALK_VEL    3000
#define MAX_VEL     29184
#define MIN_BOUNCE  53248
#define AIM_ACC     4000
#define MAX_AIM_VEL 70000
#define AIM_FRIC_M  83
#define AIM_FRIC_D  100
// aim angle limits (units: 1/128 turn; fixed<<16)
#define AIM_MIN_R   64
#define AIM_MAX_R   116
#define AIM_MAX_L   12
#define AIM_MIN_L   64

enum { RF_DOWN=0, RF_LEFT=1, RF_UP=2, RF_RIGHT=3 };

typedef struct { int x, y; } pt8;
static const pt8 col_pts[4][7] = {
    { {-1,-4},{0,-4},{1,-4},{0,0},{0,0},{0,0},{0,0} }, // DOWN
    { {1,-3},{1,-2},{1,-1},{1,0},{1,1},{1,2},{1,3} },   // LEFT
    { {-1,4},{0,4},{1,4},{0,0},{0,0},{0,0},{0,0} },     // UP
    { {-1,-3},{-1,-2},{-1,-1},{-1,0},{-1,1},{-1,2},{-1,3} }, // RIGHT
};
static const int col_pts_n[4] = {3,7,3,7};
static const int worm_anim_tab[4] = {0,7,0,14};

typedef struct {
    int32_t pos_x, pos_y;   // 16.16
    int32_t vel_x, vel_y;
    int32_t aim_angle, aim_speed;
    uint8_t reacts[4];
    uint8_t direction;       // 0 left, 1 right
    uint8_t color_idx;
    uint8_t animate;
    uint8_t able_to_jump;
    uint8_t current_frame;
    // combat (slice 4)
    int health;
    int lives;
    int visible;
    int ready;
    int killed_timer;
    int lr_x, lr_y;        // logicRespawn
} Worm;
static Worm worm;

// solid if index has Background flag -> NOT blocking; else blocking incl OOB
static inline int worm_solid(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 1;
    return (mat_flags[level_data[idx]] & 8) == 0;
}

static int reaction_count(int nx, int ny, int dir) {
    int n = 0;
    for (int i = 0; i < col_pts_n[dir]; ++i)
        if (worm_solid(nx + col_pts[dir][i].x, ny + col_pts[dir][i].y)) ++n;
    return n;
}

static void calc_reaction(int nx, int ny, int dir) {
    worm.reacts[dir] = (uint8_t)reaction_count(nx, ny, dir);
}

static inline int tdiv(int a, int b) {  // C truncation toward zero
    return a / b;
}

static int angle_frame(void) {
    int x = (worm.aim_angle >> 16) - 12;
    if (worm.direction != 0) x -= 49;
    x >>= 3;
    if (x < 0) x = 0; else if (x > 6) x = 6;
    if (worm.direction != 0) x = 6 - x;
    return x;
}

typedef struct { uint8_t left, right, up, down, jump, fire, change; } WormInput;

#ifdef WEAPONS
static void worm_weapons(void);
static void worm_try_fire(uint8_t fire, uint8_t change);
static void worm_change_weapon(uint8_t change, uint8_t left, uint8_t right);
static void worm_die(void);
static void worm_dead_process(uint8_t fire_held);
static void do_damage(int amount, int by_idx);
static uint32_t rndm(int m);
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color);
static int g_frame;
#endif

static void worm_process(WormInput in, uint64_t cycle) {
#ifdef WEAPONS
    if (g_frame == 360) do_damage(97, 1);   // AUTOTEST: near-lethal hit
    if (g_frame == 420) do_damage(99, 1);   // AUTOTEST: lethal hit
    if (!worm.visible) { worm_dead_process(in.fire); return; }
#endif
    int32_t next_x = worm.pos_x + worm.vel_x;
    int32_t next_y = worm.pos_y + worm.vel_y;
    int inx = next_x >> 16, iny = next_y >> 16;

    // reactions + wall edges, per upstream: edge checks run inside dir loop
    for (int d = 0; d < 4; ++d) {
        calc_reaction(inx, iny, d);
        if (inx < 4) worm.reacts[RF_RIGHT] += 5;
        else if (inx > LW - 5) worm.reacts[RF_LEFT] += 5;
        if (iny < 5) worm.reacts[RF_DOWN] += 5;
        else if (iny > LH - 6) worm.reacts[RF_UP] += 5;
    }
    if (worm.reacts[RF_DOWN] < 2 && worm.reacts[RF_UP] > 0 &&
        (worm.reacts[RF_LEFT] > 0 || worm.reacts[RF_RIGHT] > 0)) {
        worm.pos_y -= 1<<16;
        next_y = worm.pos_y + worm.vel_y; iny = next_y >> 16;
        calc_reaction(inx, iny, RF_LEFT); calc_reaction(inx, iny, RF_RIGHT);
    }
    if (worm.reacts[RF_UP] < 2 && worm.reacts[RF_DOWN] > 0 &&
        (worm.reacts[RF_LEFT] > 0 || worm.reacts[RF_RIGHT] > 0)) {
        worm.pos_y += 1<<16;
        next_y = worm.pos_y + worm.vel_y; iny = next_y >> 16;
        calc_reaction(inx, iny, RF_LEFT); calc_reaction(inx, iny, RF_RIGHT);
    }

    // processAiming
    if (worm.aim_speed != 0) {
        worm.aim_angle += worm.aim_speed;
        if (!in.up && !in.down)
            worm.aim_speed = tdiv(worm.aim_speed * AIM_FRIC_M, AIM_FRIC_D);
        if (worm.direction == 1) {
            if ((worm.aim_angle >> 16) > AIM_MAX_R) { worm.aim_speed = 0; worm.aim_angle = AIM_MAX_R << 16; }
            if ((worm.aim_angle >> 16) < AIM_MIN_R) { worm.aim_speed = 0; worm.aim_angle = AIM_MIN_R << 16; }
        } else {
            if ((worm.aim_angle >> 16) < AIM_MAX_L) { worm.aim_speed = 0; worm.aim_angle = AIM_MAX_L << 16; }
            if ((worm.aim_angle >> 16) > AIM_MIN_L) { worm.aim_speed = 0; worm.aim_angle = AIM_MIN_L << 16; }
        }
    }
    if (in.up) {
        if (worm.direction == 0) { if (worm.aim_speed < MAX_AIM_VEL) worm.aim_speed += AIM_ACC; }
        else { if (worm.aim_speed > -MAX_AIM_VEL) worm.aim_speed -= AIM_ACC; }
    }
    if (in.down) {
        if (worm.direction == 1) { if (worm.aim_speed < MAX_AIM_VEL) worm.aim_speed += AIM_ACC; }
        else { if (worm.aim_speed > -MAX_AIM_VEL) worm.aim_speed -= AIM_ACC; }
    }

    // processTasks: jump (edge-triggered, ground contact RF_UP > 0)
    static uint8_t prev_jump = 0;
    if (in.jump && !prev_jump && worm.reacts[RF_UP] > 0 && worm.able_to_jump) {
        worm.vel_y -= JUMP_FORCE;
        worm.able_to_jump = 0;
    }
    if (!in.jump) worm.able_to_jump = 1;
    prev_jump = in.jump;

#ifdef WEAPONS
    // upstream: processWeapons then fire-check, both before processPhysics
    worm_weapons();
    if (in.change) worm_change_weapon(in.change, in.left, in.right);
    worm_try_fire(in.fire, in.change);
    if (!in.change) worm_change_weapon(0, in.left, in.right);
#endif

    // processPhysics
    if (worm.reacts[RF_UP] > 0)
        worm.vel_x = tdiv(worm.vel_x * WORM_FRIC_M, WORM_FRIC_D);
    int32_t ax = worm.vel_x < 0 ? -worm.vel_x : worm.vel_x;
    int32_t ay = worm.vel_y < 0 ? -worm.vel_y : worm.vel_y;
    uint8_t rh = worm.reacts[worm.vel_x >= 0 ? RF_LEFT : RF_RIGHT];
    uint8_t rv = worm.reacts[worm.vel_y >= 0 ? RF_UP : RF_DOWN];
    if (worm.vel_x && rh) {
        if (ax > MIN_BOUNCE) worm.vel_x = -tdiv(worm.vel_x, 3);
        else worm.vel_x = 0;
    }
    if (worm.vel_y && rv) {
        if (ay > MIN_BOUNCE) worm.vel_y = -tdiv(worm.vel_y, 3);
        else worm.vel_y = 0;
    }
    if (worm.reacts[RF_UP] == 0) worm.vel_y += GRAVITY;
    if (worm.reacts[worm.vel_x >= 0 ? RF_LEFT : RF_RIGHT] < 2) worm.pos_x += worm.vel_x;
    if (worm.reacts[worm.vel_y >= 0 ? RF_UP : RF_DOWN] < 2) worm.pos_y += worm.vel_y;

    // processMovement: walk + facing flip (skipped while Change held, upstream)
#ifdef WEAPONS
    if (in.change) worm.animate = 0;
    else
#endif
    worm.animate = (in.left || in.right) ? 1 : 0;
    if (!in.change && in.left && !in.right) {
        if (worm.vel_x > -MAX_VEL) worm.vel_x -= WALK_VEL;
        if (worm.direction != 0) {
            worm.aim_speed = 0;
            if (worm.aim_angle >= (64<<16)) worm.aim_angle = (128<<16) - worm.aim_angle;
            worm.direction = 0;
        }
    }
    if (!in.change && !in.left && in.right) {
        if (worm.vel_x < MAX_VEL) worm.vel_x += WALK_VEL;
        if (worm.direction != 1) {
            worm.aim_speed = 0;
            if (worm.aim_angle <= (64<<16)) worm.aim_angle = (128<<16) - worm.aim_angle;
            worm.direction = 1;
        }
    }

#ifdef WEAPONS
    // pain blood: health < 25 (settings->health/4)
    if (worm.health < 25) {
        if (rndm(worm.health + 6) == 0) {
            if (rndm(3) == 0) rndm(3);
            nobj_create1(IDX_BLOOD, worm.vel_x, worm.vel_y, worm.pos_x, worm.pos_y, 0);
        }
    }
    int died = 0;
    if (worm.health <= 0) { worm_die(); died = 1; }
    int anim = (worm.animate && !died) ? ((cycle & 31) >> 3) : 0;
#else
    int anim = worm.animate ? ((cycle & 31) >> 3) : 0;
#endif
    worm.current_frame = (uint8_t)(angle_frame() + worm_anim_tab[anim]);
}

static void worm_init(int px, int py) {
    worm.pos_x = px << 16; worm.pos_y = py << 16;
    worm.vel_x = worm.vel_y = 0;
    worm.aim_angle = 64 << 16; worm.aim_speed = 0;
    worm.direction = 1; worm.color_idx = 0;
    worm.able_to_jump = 1; worm.animate = 0; worm.current_frame = 0;
}

// draw worm sprite over terrain: per-row run fills, skip transparent 0
static void draw_worm(int camx, int camy) {
    int f = 16 + worm.current_frame;
    if (f >= LARGE_FRAMES) f = 16;
    const uint8_t* src = large_stamps + f*256;
    int sx = (worm.pos_x >> 16) - 7 - camx;
    int sy = (worm.pos_y >> 16) - 5 - camy;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int y = 0; y < 16; ++y) {
        int dy = sy + y;
        if (dy < 0 || dy >= VH) continue;
        int x = 0;
        while (x < 16) {
            int px = worm.direction == 1 ? x : 15 - x;
            uint8_t idx = src[y*16 + px];
            if (idx == 0) { ++x; continue; }
            int x0 = x;
            uint8_t prev_idx = idx;
            ++x;
            while (x < 16) {
                int p2 = worm.direction == 1 ? x : 15 - x;
                uint8_t i2 = src[y*16 + p2];
                if (i2 == 0 || i2 != prev_idx) break;
                prev_idx = i2; ++x;
            }
            uint16_t c = pal_rgb555[idx];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(sx + x0, dy, sx + x, dy+1);
        }
    }
}

static uint32_t fnv2(uint32_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static uint32_t g_worm_acc = 2166136261u;

// =================== slice 3: weapons/projectiles (dirtball) ================
// Mirrors weapon.cpp Weapon::fire + WObject::process + blowUpObject and
#ifdef WEAPONS
// =================== slice 4: table-driven combat (weapons/nobjects/sobjects/bobjects) ===
// Port of upstream weapon.cpp/nobject.cpp/sobject.cpp/bobject.cpp STNormal subset.
// All params baked from TC into assets/combat.h; host mirror tools/weapons_ref.py.
#define MAX_WOBJ 256
#define MAX_NOBJ 512
#define MAX_SOBJ 700
#define MAX_BOBJ 700
#define M_WORM   32

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
    int x, y;
    int cur;
    int ad;
    int type;           // c_sparam index
    uint8_t used;
} SObj;
static SObj sobj[MAX_SOBJ];

typedef struct { int32_t x, y; int32_t vx, vy; int color; } BObj;
static BObj bobj[MAX_BOBJ];
static int bobj_count = 0;   // FastObjectList live range [0,count)

static struct { int ammo, delay_left, loading_left; } wwp[5];
static int fire_cone = 0;
static uint8_t prev_fire = 0;
static uint8_t key_change_pressed = 0;
static uint8_t prev_left = 0, prev_right = 0;
static int cur_wslot = 0;
static int cur_weapon = IDX_DIRTBALL;
static int leave_shell_timer = 0;
static int g_screen_flash = 0;
static int g_shake = 0;
static int g_last_killed_by = -1;

static const int weapons_list[2] = { IDX_DIRTBALL, IDX_HANDGUN };
#define WEAPONS_LIST_N 2

static inline uint32_t rndm(int m) {   // upstream rand(int): unsigned-wrap bounded, always burns
    return rnd((uint32_t)m);
}

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
static int anydirt_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 0;
    return (level_mat[idx] & (M_DIRT|M_DIRT2)) != 0;
}
static int back_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 0;
    return (level_mat[idx] & M_BACK) != 0;
}
static int rock_at(int x, int y) {
    unsigned int idx = (unsigned int)(x + y*LW);
    if (idx >= (unsigned int)(LW*LH)) return 1;
    return (level_mat[idx] & M_ROCK) != 0;
}

static int wobj_new(void) {  // ExactObjectList: lowest free index
    for (int i = 0; i < MAX_WOBJ; ++i) if (!wobj[i].used) { wobj[i].used = 1; return i; }
    return MAX_WOBJ - 1;
}
static int nobj_new(void) {
    for (int i = 0; i < MAX_NOBJ; ++i) if (!nobj[i].used) { nobj[i].used = 1; return i; }
    return MAX_NOBJ - 1;
}
static int sobj_new(void) {
    for (int i = 0; i < MAX_SOBJ; ++i) if (!sobj[i].used) { sobj[i].used = 1; return i; }
    return MAX_SOBJ - 1;
}
static void wobj_free(int i) { wobj[i].used = 0; }
static void nobj_free(int i) { nobj[i].used = 0; }
static void sobj_free(int i) { sobj[i].used = 0; }

static void blit_small(int f, int x, int y) {   // blitImageOnMap 7x7
    if (f < 0 || (size_t)(f+1)*49 > SMALL_FRAMES*49) return;
    const uint8_t* src = small_pix + (size_t)f * 49;
    for (int cy = 0; cy < 7; ++cy) {
        int my = y + cy;
        if (my < 0 || my >= LH) continue;
        for (int cx = 0; cx < 7; ++cx) {
            int mx = x + cx;
            if (mx < 0 || mx >= LW) continue;
            uint8_t c = src[cy*7 + cx];
            if (!c) continue;
            int i = my*LW + mx;
            uint8_t n = (level_mat[i] & (M_DIRT|M_DIRT2|M_BACK)) ? c : (uint8_t)(c + 3);
            level_data[i] = n; level_mat[i] = mat_flags[n];
        }
    }
}

// forward
static void sobj_create(int t, int x, int y, int owner_idx);
static void wobj_blow_up(int i, int cause_idx);
static void nobj_create2(int t, int angle, int bvx, int bvy, int px_, int py_, int color);
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color);

// NObjectType::create core (exact RNG order: startFrame rand, color, tte rand)
static int nobj_core(int t, int vx, int vy, int px_, int py_, int color) {
    const int32_t* p = c_nparam[t];
    int i = nobj_new();
    int cf;
    if (p[NK_startFrame] > 0) cf = (int)rndm(p[NK_numFrames] + 1);
    else if (color != 0)       cf = color;
    else                       cf = p[NK_colorBullets];
    int tl = p[NK_timeToExplo];
    if (p[NK_timeToExplo] > 0 && p[NK_timeToExploV])
        tl -= (int)rndm(p[NK_timeToExploV]);
    nobj[i].x = px_; nobj[i].y = py_;
    nobj[i].vx = vx; nobj[i].vy = vy;
    nobj[i].cur_frame = cf; nobj[i].time_left = tl;
    nobj[i].type = t;
    return i;
}
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color) {
    int d = c_nparam[t][NK_distribution];
    if (d) {
        vx += d - (int)rndm(d*2);   // upstream create1: distribution - rand
        vy += d - (int)rndm(d*2);
    }
    nobj_core(t, vx, vy, px_, py_, color);
}
static void nobj_create2(int t, int angle, int bvx, int bvy, int px_, int py_, int color) {
    const int32_t* p = c_nparam[t];
    int rs = p[NK_speed] - (int)rndm(p[NK_speedV]);
    int vx = bvx + tdiv(cs_x[angle & 127] * rs, 100);
    int vy = bvy + tdiv(cs_y[angle & 127] * rs, 100);
    int d = p[NK_distribution];
    if (d) {
        vx += (int)rndm(d*2) - d;
        vy += (int)rndm(d*2) - d;
    }
    int i = nobj_core(t, vx, vy, px_, py_, color);
    nobj[i].x += nobj[i].vx; nobj[i].y += nobj[i].vy;
}

static void create_bobject(int px_, int py_, int vx, int vy) {
    if (bobj_count >= MAX_BOBJ) return;   // drop at cap (host rule)
    BObj* o = &bobj[bobj_count++];
    o->x = px_; o->y = py_; o->vx = vx; o->vy = vy;
    o->color = (int)rndm(C_NUMBLOOD) + C_FIRSTBLOOD;
}

// SObjectType::create: sound rand -> shake/flash -> worm dmg -> blood -> pain -> wobj/nobj affect -> spatter -> dirt
static void sobj_create(int t, int x, int y, int owner_idx) {
    const int32_t* p = c_sparam[t];
    int i = sobj_new();
    if (p[SK_startSound] >= 0) rndm(p[SK_numSounds]);
    sobj[i].x = x - 8; sobj[i].y = y - 8;
    sobj[i].cur = 0; sobj[i].ad = p[SK_animDelay]; sobj[i].type = t;
    if (p[SK_flash] > g_screen_flash) g_screen_flash = p[SK_flash];
    int shk = p[SK_shake] << 16;
    if (shk > g_shake) g_shake = shk;
    int damage = p[SK_damage];
    if (damage > 0) {
        int dr = p[SK_detectRange];
        int wix = worm.pos_x >> 16, wiy = worm.pos_y >> 16;
        if (wix < x + dr && wix > x - dr && wiy < y + dr && wiy > y - dr) {
            int delta = wix - x;
            int power = dr - (delta < 0 ? -delta : delta);
            int power_sum = power;
            if (worm.vel_x > -131072 && worm.vel_x < 131072) {
                if (delta > 0) worm.vel_x += p[SK_blowAway] * power;
                else           worm.vel_x -= p[SK_blowAway] * power;
            }
            delta = wiy - y;
            power = dr - (delta < 0 ? -delta : delta);
            power_sum = (power_sum + power) / 2;
            if (worm.vel_y > -131072 && worm.vel_y < 131072) {
                if (delta > 0) worm.vel_y += p[SK_blowAway] * power;
                else           worm.vel_y -= p[SK_blowAway] * power;
            }
            int z = damage * power_sum;
            if (dr) z = tdiv(z, dr);
            if (worm.health > 0) {
                do_damage(z, owner_idx);
                int blood_amount = power_sum;   // settings->blood = 100
                for (int k = 0; k < blood_amount; ++k) {
                    int angle = (int)rndm(128);
                    nobj_create2(IDX_BLOOD, angle, tdiv(worm.vel_x,3), tdiv(worm.vel_y,3), worm.pos_x, worm.pos_y, 0);
                }
                if (rndm(3) == 0) rndm(3);   // pain sound pick
            }
        }
        int obj_blow = p[SK_blowAway] / 3;
        for (int k = 0; k < MAX_WOBJ; ++k) if (wobj[k].used) {
            if (c_wparam[wobj[k].type][WK_affectByExplosions]) {
                int ox = wobj[k].x >> 16, oy = wobj[k].y >> 16;
                if (ox < x + dr && ox > x - dr && oy < y + dr && oy > y - dr) {
                    int d = ox - x, pw = dr - (d < 0 ? -d : d);
                    if (pw > 0) { if (d > 0) wobj[k].vx += obj_blow*pw; else if (d < 0) wobj[k].vx -= obj_blow*pw; }
                    d = oy - y; pw = dr - (d < 0 ? -d : d);
                    if (pw > 0) { if (d > 0) wobj[k].vy += obj_blow*pw; else wobj[k].vy -= obj_blow*pw; }
                    if (c_wparam[wobj[k].type][WK_chainExplosion]) wobj_blow_up(k, owner_idx);
                }
            }
        }
        for (int k = 0; k < MAX_NOBJ; ++k) if (nobj[k].used) {
            if (c_nparam[nobj[k].type][NK_affectByExplosions]) {
                int ox = nobj[k].x >> 16, oy = nobj[k].y >> 16;
                if (ox < x + dr && ox > x - dr && oy < y + dr && oy > y - dr) {
                    int d = ox - x, pw = dr - (d < 0 ? -d : d);
                    if (pw > 0) { if (d > 0) nobj[k].vx += obj_blow*pw; else if (d < 0) nobj[k].vx -= obj_blow*pw; }
                    d = oy - y; pw = dr - (d < 0 ? -d : d);
                    if (pw > 0) { if (d > 0) nobj[k].vy += obj_blow*pw; else nobj[k].vy -= obj_blow*pw; }
                }
            }
        }
        int width = dr / 2;
        int x1 = x - width < 0 ? 0 : x - width;
        int y1 = y - width < 0 ? 0 : y - width;
        int x2 = x + width + 1 > LW ? LW : x + width + 1;
        int y2 = y + width + 1 > LH ? LH : y + width + 1;
        for (int yy = y1; yy < y2; ++yy)
            for (int xx = x1; xx < x2; ++xx)
                if (anydirt_at(xx, yy) && rndm(8) == 0) {
                    uint8_t pix = level_data[yy*LW + xx];
                    int angle = (int)rndm(128);
                    nobj_create2(IDX_PART2, angle, 0, 0, xx << 16, yy << 16, pix);
                }
    }
    if (p[SK_dirtEffect] >= 0) {
        dirt_effect(p[SK_dirtEffect], x - 7, y - 7);
        correct_shadow(x - 10, y - 10, x + 11, y + 11);
    }
}

// WObject::blowUpObject: free first -> createOnExp -> splinters -> dirt
static void wobj_blow_up(int i, int cause_idx) {
    WObj* o = &wobj[i];
    int px_ = o->x >> 16, py_ = o->y >> 16;
    int velX = o->vx, velY = o->vy;
    int wtype = o->type;
    wobj_free(i);
    const int32_t* p = c_wparam[wtype];
    if (p[WK_createOnExp] >= 0) sobj_create(p[WK_createOnExp], px_, py_, cause_idx);
    int splinters = p[WK_splinterAmount];
    if (splinters > 0) {
        if (p[WK_splinterScatter] == 0) {
            for (int k = 0; k < splinters; ++k) {
                int angle = (int)rndm(128);
                int colorSub = (int)rndm(2);
                nobj_create2(p[WK_splinterType], angle, 0, 0, px_ << 16, py_ << 16, p[WK_splinterColour] - colorSub);
            }
        } else {
            for (int k = 0; k < splinters; ++k) {
                int colorSub = (int)rndm(2);
                nobj_create1(p[WK_splinterType], velX, velY, px_ << 16, py_ << 16, p[WK_splinterColour] - colorSub);
            }
        }
    }
    if (p[WK_dirtEffect] >= 0) {
        dirt_effect(p[WK_dirtEffect], px_ - 7, py_ - 7);
        correct_shadow(px_ - 10, py_ - 10, px_ + 11, py_ + 11);
    }
}

// checkForSpecWormHit: mirrored to match draw_worm
static int check_spec_worm_hit(int x, int y, int dist) {
    if (!worm.visible) return 0;
    int f = 16 + worm.current_frame;
    if (f >= LARGE_FRAMES) f = 16;
    const uint8_t* ws = large_stamps + (size_t)f * 256;
    int dX = x - (worm.pos_x >> 16) + 7;
    int dY = y - (worm.pos_y >> 16) + 5;
    int x1 = dX - dist < 0 ? 0 : dX - dist;
    int y1 = dY - dist < 0 ? 0 : dY - dist;
    int x2 = dX + dist + 1 > 16 ? 16 : dX + dist + 1;
    int y2 = dY + dist + 1 > 16 ? 16 : dY + dist + 1;
    for (int cy = y1; cy < y2; ++cy)
        for (int cx = x1; cx < x2; ++cx) {
            int pi = worm.direction == 1 ? cx : 15 - cx;
            if (mat_flags[ws[cy*16 + pi]] & M_WORM) return 1;
        }
    return 0;
}

static void do_damage(int amount, int by_idx) {
    if (amount > 0) {
        worm.health -= amount;
#ifdef AUTOTEST
        debugf("DMG f=%d a=%d hp=%d by=%d\n", g_frame, amount, worm.health, by_idx);
#endif
        if (worm.health <= 0) g_last_killed_by = by_idx;
    }
}

// Weapon::fire STNormal subset + recoil + shells rand (before parts loop)
static void fire_wobj(void) {
    const int32_t* p = c_wparam[cur_weapon];
    int aa = (int)(worm.aim_angle >> 16) & 127;
    int fx = tdiv(cs_x[aa] * (p[WK_detectDistance] + 5), 1) + worm.pos_x;
    int fy = tdiv(cs_y[aa] * (p[WK_detectDistance] + 5), 1) + worm.pos_y - (1 << 16);
    if (p[WK_leaveShells] > 0 && rndm(p[WK_leaveShells]) == 0)
        leave_shell_timer = p[WK_leaveShellDelay];
    int speed = p[WK_speed];
    int fvx = 0, fvy = 0;
    if (p[WK_affectByWorm]) {
        if (speed < 100) speed = 100;
        fvx = tdiv(worm.vel_x * 100, speed);
        fvy = tdiv(worm.vel_y * 100, speed);
    }
    for (int part = 0; part < p[WK_parts]; ++part) {
        int vx = tdiv(cs_x[aa] * speed, 100) + fvx;
        int vy = tdiv(cs_y[aa] * speed, 100) + fvy;
        int d = p[WK_distribution];
        if (d) {
            vx += (int)rndm(d*2) - d;
            vy += (int)rndm(d*2) - d;
        }
        int cur = 0;
        if (p[WK_startFrame] >= 0) {
            if (p[WK_shotType] == 0 && p[WK_loopAnim])
                cur = p[WK_numFrames] ? (int)rndm(p[WK_numFrames] + 1) : (int)rndm(2);
        } else {
            cur = p[WK_colorBullets] - (int)rndm(2);
        }
        int tl = p[WK_timeToExplo];
        if (p[WK_timeToExploV])
            tl -= (int)rndm(p[WK_timeToExploV]);
        int i = wobj_new();
        wobj[i].x = fx; wobj[i].y = fy; wobj[i].vx = vx; wobj[i].vy = vy;
        wobj[i].cur_frame = cur; wobj[i].time_left = tl; wobj[i].owner = 0;
        wobj[i].type = cur_weapon; wobj[i].has_hit = 0;
    }
    int recoil = p[WK_recoil];
    worm.vel_x -= tdiv(cs_x[aa] * recoil, 100);
    worm.vel_y -= tdiv(cs_y[aa] * recoil, 100);
}

static void worm_try_fire(uint8_t fire_held, uint8_t change_held) {
    if (fire_held && !prev_fire && !change_held && wwp[0].loading_left == 0 && wwp[0].delay_left <= 0) {
        --wwp[0].ammo;
        wwp[0].delay_left = c_wparam[cur_weapon][WK_delay];
        fire_wobj();
    }
    prev_fire = fire_held;
}

// processWeaponChange: keyChangePressed release-once, pressedOnce(L/R) via prev-edge tracking
static void worm_change_weapon(uint8_t change_held, uint8_t left_held, uint8_t right_held) {
    if (change_held) {
        if (!key_change_pressed) {
            prev_left = 0; prev_right = 0;   // release(Left/Right)
            key_change_pressed = 1;
        }
        fire_cone = 0; worm.animate = 0;
        if (wwp[0].loading_left == 0 || 1) {   // available() || loadChange(true)
            if (left_held && !prev_left) {
                if (--cur_wslot < 0) cur_wslot = WEAPONS_LIST_N - 1;
                cur_weapon = weapons_list[cur_wslot];
                wwp[0].ammo = c_wparam[cur_weapon][WK_ammo];
                wwp[0].loading_left = 0;
                leave_shell_timer = 0;
            }
            if (right_held && !prev_right) {
                if (++cur_wslot >= WEAPONS_LIST_N) cur_wslot = 0;
                cur_weapon = weapons_list[cur_wslot];
                wwp[0].ammo = c_wparam[cur_weapon][WK_ammo];
                wwp[0].loading_left = 0;
                leave_shell_timer = 0;
            }
        }
    } else {
        key_change_pressed = 0;
    }
    prev_left = change_held ? left_held : 0;
    prev_right = change_held ? right_held : 0;
}

static void worm_weapons(void) {
    for (int i = 0; i < 5; ++i)
        if (wwp[i].delay_left >= 0) --wwp[i].delay_left;
    if (wwp[0].ammo <= 0) {
        int lt = (100 * c_wparam[cur_weapon][WK_loadingTime]) / 100; if (!lt) lt = 1;
        wwp[0].loading_left = lt;
        wwp[0].ammo = c_wparam[cur_weapon][WK_ammo];
    }
    if (wwp[0].loading_left > 0) --wwp[0].loading_left;
    if (fire_cone > 0) --fire_cone;
    if (leave_shell_timer > 0) {
        if (--leave_shell_timer <= 0) {
            int vy = -(int)rndm(20000);
            int vx = (int)rndm(16000) - 8000;
            nobj_create1(IDX_SHELLS, vx, vy, worm.pos_x, worm.pos_y, 0);
        }
    }
}

static void process_wobjects(uint64_t cycle) {
    for (int i = 0; i < MAX_WOBJ; ++i) {
        if (!wobj[i].used) continue;
        WObj* o = &wobj[i];
        const int32_t* p = c_wparam[o->type];
        int do_explode = 0, do_remove = 0;
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;
        int bounce = p[WK_bounce];
        if (bounce > 0) {
            if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
                if (bounce != 100) { o->vx = -tdiv(o->vx*bounce, 100); o->vy = tdiv(o->vy*4, 5); }
                else o->vx = -o->vx;
            }
            if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
                if (bounce != 100) { o->vy = -tdiv(o->vy*bounce, 100); o->vx = tdiv(o->vx*4, 5); }
                else o->vy = -o->vy;
            }
        }
        if (p[WK_multSpeed] != 100) {
            o->vx = tdiv(o->vx*p[WK_multSpeed], 100);
            o->vy = tdiv(o->vy*p[WK_multSpeed], 100);
        }
        // objTrail/partTrail/collideWithObjects: -1/0 for slice-4 weapons -> skipped
        nx = (o->x + o->vx) >> 16; ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = (LW - 1) << 16;
        if (ny >= LH) o->y = (LH - 1) << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            if (bounce == 0) {
                if (p[WK_explGround]) do_explode = 1;
                else { o->vx = 0; o->vy = 0; }
            }
        } else {
            o->vy += p[WK_gravity];
            if (p[WK_numFrames] > 0 && (cycle & 7) == 0) {
                if (!p[WK_loopAnim]) {
                    if (++o->cur_frame > p[WK_numFrames]) o->cur_frame = 0;
                } else {
                    if (o->vx < 0) { if (--o->cur_frame < 0) o->cur_frame = p[WK_numFrames]; }
                    else if (o->vx > 0) { if (++o->cur_frame > p[WK_numFrames]) o->cur_frame = 0; }
                }
            }
        }
        if (p[WK_timeToExplo] > 0) {
            if (--o->time_left < 0) do_explode = 1;
        }
        if ((p[WK_hitDamage] || p[WK_blowAway] || p[WK_bloodOnHit] || p[WK_wormCollide])
            && check_spec_worm_hit(o->x >> 16, o->y >> 16, p[WK_detectDistance])) {
            worm.vel_x += tdiv(o->vx * p[WK_blowAway], 100);
            worm.vel_y += tdiv(o->vy * p[WK_blowAway], 100);
            do_damage(p[WK_hitDamage], o->owner);
            o->has_hit = 1;
            int blood = p[WK_bloodOnHit];   // *settings->blood(100)/100
            for (int k = 0; k < blood; ++k) {
                int angle = (int)rndm(128);
                nobj_create2(IDX_BLOOD, angle, tdiv(o->vx,3), tdiv(o->vy,3), o->x, o->y, 0);
            }
            if (p[WK_hitDamage] > 0 && worm.health > 0 && rndm(3) == 0) rndm(3);
            if (p[WK_wormCollide] && rndm(p[WK_wormCollide]) == 0) {
                if (p[WK_wormExplode]) do_explode = 1;
                do_remove = 1;
            }
        }
        if (do_explode) wobj_blow_up(i, o->owner);
        else if (do_remove) wobj_free(i);
    }
}

static void nobj_explode(int i, int cause) {
    NObj* o = &nobj[i];
    const int32_t* p = c_nparam[o->type];
    int px_ = o->x >> 16, py_ = o->y >> 16;
    if (p[NK_createOnExp] >= 0) sobj_create(p[NK_createOnExp], px_, py_, cause);
    if (p[NK_dirtEffect] >= 0) {
        dirt_effect(p[NK_dirtEffect], px_ - 7, py_ - 7);
        correct_shadow(px_ - 10, py_ - 10, px_ + 11, py_ + 11);
    }
    for (int k = 0; k < p[NK_splinterAmount]; ++k) {
        int angle = (int)rndm(128);
        int cs = (int)rndm(2);
        nobj_create2(p[NK_splinterType], angle, 0, 0, px_ << 16, py_ << 16, p[NK_splinterColour] - cs);
    }
    nobj_free(i);
}

static void process_nobjects(uint64_t cycle) {
    for (int i = 0; i < MAX_NOBJ; ++i) {
        if (!nobj[i].used) continue;
        NObj* o = &nobj[i];
        const int32_t* p = c_nparam[o->type];
        int bounced = 0, do_explode = 0, freed = 0;
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;
        int bounce = p[NK_bounce];
        if (bounce > 0) {
            if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
                o->vx = -tdiv(o->vx*bounce, 100); o->vy = tdiv(o->vy*4, 5); bounced = 1;
            }
            if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
                o->vy = -tdiv(o->vy*bounce, 100); o->vx = tdiv(o->vx*4, 5); bounced = 1;
            }
        }
        if (p[NK_bloodTrail] && p[NK_bloodTrailDelay] > 0 && (cycle % p[NK_bloodTrailDelay]) == 0)
            create_bobject(o->x, o->y, tdiv(o->vx,4), tdiv(o->vy,4));
        // "Yes, we do this again."
        nx = (o->x + o->vx) >> 16; ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = LW << 16;
        if (ny >= LH) o->y = LH << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            o->vx = 0; o->vy = 0;
            if (p[NK_explGround]) {
                if (p[NK_startFrame] > 0 && p[NK_drawOnMap]) {
                    blit_small(p[NK_startFrame] + o->cur_frame, ix - 3, iy - 3);
                    correct_shadow(ix - 8, iy - 8, ix + 9, iy + 9);
                }
                do_explode = 1;
            }
        } else {
            if (!bounced && p[NK_leaveObjDelay] != 0 && p[NK_leaveObj] >= 0
                && (cycle % p[NK_leaveObjDelay]) == 0)
                sobj_create(p[NK_leaveObj], o->x >> 16, o->y >> 16, 0);
            o->vy += p[NK_gravity];
            if (p[NK_numFrames] > 0 && (cycle & 7) == 0) {
                if (o->vx > 0)      { if (++o->cur_frame > p[NK_numFrames]) o->cur_frame = 0; }
                else if (o->vx < 0) { if (--o->cur_frame < 0) o->cur_frame = p[NK_numFrames]; }
            }
        }
        if (p[NK_timeToExplo] > 0) {
            if (--o->time_left <= 0) do_explode = 1;
        }
        if (!do_explode && p[NK_hitDamage] > 0
            && check_spec_worm_hit(o->x >> 16, o->y >> 16, p[NK_detectDistance])) {
            worm.vel_x += tdiv(o->vx * p[NK_blowAway], 100);
            worm.vel_y += tdiv(o->vy * p[NK_blowAway], 100);
            do_damage(p[NK_hitDamage], 0);
            if (rndm(3) == 0) rndm(3);   // pain pick BEFORE blood (nobject order)
            int blood = p[NK_bloodOnHit];
            for (int k = 0; k < blood; ++k) {
                int angle = (int)rndm(128);
                nobj_create2(IDX_BLOOD, angle, tdiv(o->vx,3), tdiv(o->vy,3), o->x, o->y, 0);
            }
            if (p[NK_wormExplode]) do_explode = 1;
            else if (p[NK_wormDestroy]) { nobj_free(i); freed = 1; }
        }
        if (!freed && do_explode) nobj_explode(i, 0);
    }
}

static void process_bobjects(void) {
    int i = 0;
    while (i < bobj_count) {
        BObj* o = &bobj[i];
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;
        int remove = 0;
        if (ix < 0 || ix >= LW || iy < 0 || iy >= LH) remove = 1;
        else {
            if (back_at(ix, iy)) o->vy += C_BOBJGRAV;
            uint8_t c = level_data[iy*LW + ix];
            if ((c >= 1 && c <= 2) || (c >= 77 && c <= 79)) {
                uint8_t n = (uint8_t)(77 + rndm(3)); level_data[iy*LW+ix] = n; level_mat[iy*LW+ix] = mat_flags[n]; remove = 1;
            } else if (anydirt_at(ix, iy)) {
                uint8_t n = (uint8_t)(82 + rndm(3)); level_data[iy*LW+ix] = n; level_mat[iy*LW+ix] = mat_flags[n]; remove = 1;
            } else if (rock_at(ix, iy)) {
                uint8_t n = (uint8_t)(85 + rndm(3)); level_data[iy*LW+ix] = n; level_mat[iy*LW+ix] = mat_flags[n]; remove = 1;
            }
        }
        if (remove) { bobj[i] = bobj[bobj_count - 1]; --bobj_count; }
        else ++i;
    }
}

static void process_sobjects(uint64_t cycle) {
    (void)cycle;
    for (int i = 0; i < MAX_SOBJ; ++i) if (sobj[i].used) {
        const int32_t* p = c_sparam[sobj[i].type];
        if (--sobj[i].ad <= 0) {
            sobj[i].ad = p[SK_animDelay];
            if (++sobj[i].cur > p[SK_numFrames]) sobj_free(i);
        }
    }
}

// viewport shake: burns two rand calls while active
static void process_shake(void) {
    int rs = g_shake >> 16;
    if (rs > 0) { rndm(rs*2); rndm(rs*2); }
    if (g_shake > 0) g_shake -= 4000;
}

// ---- death / respawn ----
static void worm_die(void) {
    leave_shell_timer = 0;
    rndm(3);   // death snd pick
#ifdef AUTOTEST
    debugf("DIE f=%d lives=%d by=%d\n", g_frame, worm.lives - 1, g_last_killed_by);
#endif
    worm.visible = 0;
    worm.killed_timer = 150;
    --worm.lives;
    for (int k = 0; k < 120; ++k) {
        int angle = (int)rndm(128);
        nobj_create2(IDX_BLOOD, angle, tdiv(worm.vel_x,3), tdiv(worm.vel_y,3), worm.pos_x, worm.pos_y, 0);
    }
    for (int a = 7; a <= 105; a += 14)
        nobj_create2(0, a + (int)rndm(14), tdiv(worm.vel_x,3), tdiv(worm.vel_y,3), worm.pos_x, worm.pos_y, 0);
    prev_fire = 0;
}

static int check_respawn_pos(int oldx, int oldy, int x, int y) {
    int dy = oldy - y;
    if ((oldx < 0 ? -oldx : oldx) <= C_SPAWN_LAST && (dy < 0 ? -dy : dy) <= C_SPAWN_LAST) return 0;
    int dx = oldx - x;
    if ((dx < 0 ? -dx : dx) <= C_SPAWN_ENEMY && (dy < 0 ? -dy : dy) <= C_SPAWN_ENEMY) return 0;
    int x1 = x - 3 < 0 ? 0 : x - 3;
    int y1 = y - 4 < 0 ? 0 : y - 4;
    int x2 = x + 3 > LW-1 ? LW-1 : x + 3;
    int y2 = y + 4 > LH-1 ? LH-1 : y + 4;
    for (int j = y1; j < y2; ++j)
        for (int k = x1; k < x2; ++k)
            if (rock_at(k, j)) return 0;
    return 1;
}

static void worm_begin_respawn(void) {
    int temp_x = worm.pos_x >> 16, temp_y = worm.pos_y >> 16;
    int sx2 = temp_x, sy2 = temp_y, trials = 0;
    while (1) {
        sx2 = C_SPAWN_X + (int)rndm(C_SPAWN_W);
        sy2 = C_SPAWN_Y + (int)rndm(C_SPAWN_H);
        while (sy2 + 4 < LH && back_at(sx2, sy2 + 4)) ++sy2;
        if (++trials >= 50000) break;
        if (check_respawn_pos(temp_x, temp_y, sx2, sy2)) break;
    }
    worm.pos_x = sx2 << 16; worm.pos_y = sy2 << 16;
    worm.lr_x = temp_x - 80; worm.lr_y = temp_y - 80;
    worm.killed_timer = -1;
}

static void worm_dead_process(uint8_t fire_held) {
    if (fire_held) worm.ready = 1;
    if (worm.killed_timer > 0) --worm.killed_timer;
    if (worm.killed_timer == 0) worm_begin_respawn();
    if (worm.killed_timer < 0) {
        int dest_x = (worm.pos_x >> 16) - 80;
        int dest_y = (worm.pos_y >> 16) - 80;
        for (int k = 0; k < 4; ++k) {
            if (worm.lr_x < dest_x) ++worm.lr_x;
            else if (worm.lr_x > dest_x) --worm.lr_x;
            if (worm.lr_y < dest_y) ++worm.lr_y;
            else if (worm.lr_y > dest_y) --worm.lr_y;
        }
        if (worm.lr_x < 0) worm.lr_x = 0; else if (worm.lr_x > LW-158) worm.lr_x = LW-158;
        if (worm.lr_y < 0) worm.lr_y = 0; else if (worm.lr_y > LH-158) worm.lr_y = LH-158;
        if (dest_x < 0) dest_x = 0; else if (dest_x > LW-158) dest_x = LW-158;
        if (dest_y < 0) dest_y = 0; else if (dest_y > LH-158) dest_y = LH-158;
        if (dest_x-5 < worm.lr_x && worm.lr_x < dest_x+5 && dest_y-5 < worm.lr_y && worm.lr_y < dest_y+5 && worm.ready) {
            int ipx = worm.pos_x >> 16, ipy = worm.pos_y >> 16;
            dirt_effect(0, ipx - 7, ipy - 7);
            correct_shadow(ipx - 10, ipy - 10, ipx + 11, ipy + 11);
            worm.ready = 0; worm.visible = 1;
            worm.vel_x = 0; worm.vel_y = 0;
            worm.health = 100;
#ifdef AUTOTEST
            debugf("RSP f=%d x=%d y=%d\n", g_frame, ipx, ipy);
#endif
            if (rng_next() & 1) { worm.aim_angle = 32 << 16; worm.direction = 0; }
            else { worm.aim_angle = 96 << 16; worm.direction = 1; }
        }
    }
}

static void init_weapons(void) {
    for (int i = 0; i < 5; ++i) {
        wwp[i].ammo = (i == 0) ? c_wparam[cur_weapon][WK_ammo] : 0;
        wwp[i].delay_left = 0; wwp[i].loading_left = 0;
    }
    worm.health = 100; worm.lives = 5; worm.visible = 1;
    worm.ready = 0; worm.killed_timer = 0; worm.lr_x = 0; worm.lr_y = 0;
    cur_wslot = 0; cur_weapon = IDX_DIRTBALL; leave_shell_timer = 0;
}

// generic wobject/nobject sprite draw (upstream viewport rule: small sprite if startFrame>0, else pixel)
static void draw_wobjects(int camx, int camy) {
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int i = 0; i < MAX_WOBJ; ++i) if (wobj[i].used) {
        const int32_t* p = c_wparam[wobj[i].type];
        if (p[WK_startFrame] > -1) {
            int f = p[WK_startFrame] + wobj[i].cur_frame;
            if (f < 0 || (size_t)(f+1)*49 > (size_t)SMALL_FRAMES*49) continue;
            const uint8_t* src = small_pix + (size_t)f * 49;
            int px = (wobj[i].x >> 16) - 3 - camx;
            int py = (wobj[i].y >> 16) - 3 - camy;
            for (int y = 0; y < 7; ++y) {
                int dy = py + y;
                if (dy < 0 || dy >= VH) continue;
                for (int x = 0; x < 7; ++x) {
                    uint8_t idx = src[y*7 + x];
                    if (!idx) continue;
                    int dx = px + x;
                    if (dx < 0 || dx >= VW) continue;
                    uint16_t c = pal_rgb555[idx];
                    int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                    rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
                    rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
                }
            }
        } else if (wobj[i].cur_frame > 0) {
            int dx = (wobj[i].x >> 16) - camx;
            int dy = (wobj[i].y >> 16) - camy;
            if (dx < 0 || dx >= VW || dy < 0 || dy >= VH) continue;
            uint16_t c = pal_rgb555[wobj[i].cur_frame & 255];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
        }
    }
    for (int i = 0; i < MAX_NOBJ; ++i) if (nobj[i].used) {
        const int32_t* p = c_nparam[nobj[i].type];
        int cf = nobj[i].cur_frame;
        if (p[NK_startFrame] > 0) {
            int f = p[NK_startFrame] + cf;
            if (f < 0 || (size_t)(f+1)*49 > (size_t)SMALL_FRAMES*49) continue;
            const uint8_t* src = small_pix + (size_t)f * 49;
            int px = (nobj[i].x >> 16) - 3 - camx;
            int py = (nobj[i].y >> 16) - 3 - camy;
            for (int y = 0; y < 7; ++y) {
                int dy = py + y;
                if (dy < 0 || dy >= VH) continue;
                for (int x = 0; x < 7; ++x) {
                    uint8_t idx = src[y*7 + x];
                    if (!idx) continue;
                    int dx = px + x;
                    if (dx < 0 || dx >= VW) continue;
                    uint16_t c = pal_rgb555[idx];
                    int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                    rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
                    rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
                }
            }
        } else if (cf > 1) {
            int dx = (nobj[i].x >> 16) - camx;
            int dy = (nobj[i].y >> 16) - camy;
            if (dx < 0 || dx >= VW || dy < 0 || dy >= VH) continue;
            uint16_t c = pal_rgb555[cf & 255];
            int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
            rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
            rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
        }
    }
    for (int i = 0; i < MAX_SOBJ; ++i) if (sobj[i].used) {
        const int32_t* p = c_sparam[sobj[i].type];
        int f = p[SK_startFrame] + sobj[i].cur;
        if (f < 0 || (size_t)(f+1)*256 > (size_t)LARGE_FRAMES*256) continue;
        const uint8_t* src = large_stamps + (size_t)f * 256;
        int px = sobj[i].x - camx;
        int py = sobj[i].y - camy;
        for (int y = 0; y < 16; ++y) {
            int dy = py + y;
            if (dy < 0 || dy >= VH) continue;
            for (int x = 0; x < 16; ++x) {
                uint8_t idx = src[y*16 + x];
                if (!idx) continue;
                int dx = px + x;
                if (dx < 0 || dx >= VW) continue;
                uint16_t c = pal_rgb555[idx];
                int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
                rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
            }
        }
    }
    // bobjects: single pixel at fixed blood palette run 80/81
    for (int i = 0; i < bobj_count; ++i) {
        int dx = (bobj[i].x >> 16) - camx;
        int dy = (bobj[i].y >> 16) - camy;
        if (dx < 0 || dx >= VW || dy < 0 || dy >= VH) continue;
        uint16_t c = pal_rgb555[bobj[i].color & 255];
        int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
        rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
    }
}
#endif  // WEAPONS (combat pools + processing)
// wobj/nobj state hash (debug channel)
#ifdef WEAPONS
static uint32_t g_obj_acc = 2166136261u;
// full pools hash: matches host mirror byte layout (le int32 packed)
static void worm_hash_weap(uint32_t *oh_out, int *wc, int *nc, int *sc, int *bc) {
    uint32_t oh = 2166136261u;
    int w=0,n2=0,s2=0;
    uint8_t ab[36];
    for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used) {
        w++;
        int32_t v[9] = {wobj[j].x, wobj[j].y, wobj[j].vx, wobj[j].vy,
                        wobj[j].cur_frame, wobj[j].time_left, wobj[j].owner, wobj[j].type,
                        wobj[j].has_hit};
        for (int k = 0; k < 9; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 36);
    }
    for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) {
        n2++;
        int32_t v[7] = {nobj[j].x, nobj[j].y, nobj[j].vx, nobj[j].vy,
                        nobj[j].cur_frame, nobj[j].time_left, nobj[j].type};
        for (int k = 0; k < 7; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 28);
    }
    for (int j = 0; j < MAX_SOBJ; ++j) if (sobj[j].used) {
        s2++;
        int32_t v[5] = {sobj[j].x, sobj[j].y, sobj[j].cur, sobj[j].ad, sobj[j].type};
        for (int k = 0; k < 5; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 20);
    }
    for (int j = 0; j < bobj_count; ++j) {
        int32_t v[5] = {bobj[j].x, bobj[j].y, bobj[j].vx, bobj[j].vy, bobj[j].color};
        for (int k = 0; k < 5; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 20);
    }
    if (oh_out) *oh_out = oh;
    if (wc) *wc = w;
    if (nc) *nc = n2;
    if (sc) *sc = s2;
    if (bc) *bc = bobj_count;
    g_obj_acc = oh;
}
#endif  // WEAPONS hash

static void worm_hash(void) {
    uint8_t b[16];
    for (int i = 0; i < 4; ++i) {
        b[i]    = (uint8_t)(worm.pos_x >> (8*i));
        b[4+i]  = (uint8_t)(worm.pos_y >> (8*i));
        b[8+i]  = (uint8_t)(worm.vel_x >> (8*i));
        b[12+i] = (uint8_t)(worm.vel_y >> (8*i));
    }
    g_worm_acc = fnv2(g_worm_acc, b, 16);
}

int main(void) {
    debug_init_emulog();
    debugf("BOOT\n");
    rdpq_init();    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    joypad_init();

    generate_random();

    uint32_t hash = fnv(level_data, sizeof(level_data));
    debugf("GEN hash=%08lx\n", (unsigned long)hash);

    // carve a test chamber (post-hash: generator parity stays verifiable).
    // rect x 220..300, y floor at 180, 80x24 air (index 1 = dirt+background).
#ifdef WORM
    // chamber idx160 = background+seeThrough: air to worm AND to bullets
    for (int y = 156; y < 180; ++y)
        for (int x = 100; x < 180; ++x) {
            level_data[y*LW + x] = 160;
            level_mat[y*LW + x] = mat_flags[160];
        }
#ifdef WEAPONS
    init_weapons();
#endif
#endif

    // spawn: chamber floor with ground contact
    {
        int sx = 115, sy = 175;
        for (int y = 156; y < 180; ++y)
            if (reaction_count(sx, y, RF_UP) >= 1) { sy = y; break; }
        worm_init(sx, sy);
        debugf("SPAWN x=%d y=%d\n", sx, sy);
    }

    int camx = 0;
    uint64_t frame = 0;
    while (1) {
        joypad_poll();
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
        (void)jin;
        uint64_t cycle = frame + 1;
#ifndef WEAPONS
        (void)cycle;
#else
        g_frame = (int)frame;
        process_sobjects(cycle);
        process_wobjects(cycle);
        process_nobjects(cycle);
        process_bobjects();
#endif
#ifdef WORM
        WormInput win = {0,0,0,0,0,0};
#ifdef AUTOTEST
        // scripted: walk right 120f, jump, walk left 120f, aim up 60f, repeat
        int ph = (int)(frame % 420);
        if (ph < 120) win.right = 1;
        else if (ph == 120) win.jump = 1;
        else if (ph >= 210 && ph < 330) win.left = 1;
        else if (ph >= 330) win.up = 1;
#ifdef WEAPONS
        // aim at floor then fire dirtballs; change to handgun and fire
        if ((ph >= 100 && ph < 118) || (ph >= 344 && ph < 352)) win.down = 1;
        if (ph == 110 || ph == 135 || ph == 160 || ph == 185 || ph == 210 ||
            ph == 346 || ph == 351) win.fire = 1;
        if (ph == 341) win.left = 1;
        if (ph == 340 || ph == 341 || ph == 342) win.change = 1;
#endif
#else
        win.left  = jin.btn.d_left  != 0;
        win.right = jin.btn.d_right != 0;
        win.up    = jin.btn.d_up    != 0;
        win.down  = jin.btn.d_down  != 0;
        win.jump  = jin.btn.a       != 0;
#ifdef WEAPONS
        win.fire  = jin.btn.b       != 0;
#endif
#endif
        worm_process(win, frame);
#ifdef WEAPONS
        process_shake();
#endif
#ifdef WEAPONS_AUTOTEST_DUMP
        if (frame >= 100 && frame <= 640 && (frame % 5) == 2) {
            for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used)
                debugf("WT f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld t=%d ty=%d\n",
                    (int)frame, j,
                    (long)wobj[j].x, (long)wobj[j].y,
                    (long)wobj[j].vx, (long)wobj[j].vy, wobj[j].time_left, wobj[j].type);
        }
        if (frame >= 100 && frame <= 640 && (frame % 5) == 1) {
            int cnt = 0;
            for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) ++cnt;
            debugf("NN f=%d n=%d\n", (int)frame, cnt);
            cnt = 0;
            for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) {
                if (cnt++ >= 12) break;
                debugf("NT f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld t=%d ty=%d\n",
                    (int)frame, j,
                    (long)nobj[j].x, (long)nobj[j].y,
                    (long)nobj[j].vx, (long)nobj[j].vy, nobj[j].time_left, nobj[j].type);
            }
        }
        if (frame >= 100 && frame <= 640 && (frame % 5) == 3) {
            for (int j = 0; j < MAX_SOBJ; ++j) if (sobj[j].used)
                debugf("ST f=%d i=%d x=%d y=%d cur=%d ad=%d ty=%d\n",
                    (int)frame, j, sobj[j].x, sobj[j].y, sobj[j].cur, sobj[j].ad, sobj[j].type);
        }
        if (frame >= 100 && frame <= 640 && (frame % 5) == 4) {
            for (int j = 0; j < bobj_count && j < 8; ++j)
                debugf("BO f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld co=%d\n",
                    (int)frame, j,
                    (long)bobj[j].x, (long)bobj[j].y,
                    (long)bobj[j].vx, (long)bobj[j].vy, bobj[j].color);
        }
#endif
#endif

        surface_t* fb = display_get();
        rdpq_attach(fb, NULL);
#ifndef NORENDER
#ifdef WKILLHOLD
        if ((frame % 8) == 7) {
            render_view(camx, 0);
#ifdef WORM
            draw_worm(camx, 0);
#endif
#ifdef WEAPONS
            draw_wobjects(camx, 0);
#endif
        }
#else
        render_view(camx, 0);
#ifdef WORM
        draw_worm(camx, 0);
#endif
#ifdef WEAPONS
        draw_wobjects(camx, 0);
#endif
#endif
#endif
        rdpq_detach();
        display_show(fb);

#ifdef WEAPONS
        uint32_t oh_out; int wc, nc, sc, bc;
        worm_hash_weap(&oh_out, &wc, &nc, &sc, &bc);
#else
        uint32_t oh_out = 0; int wc = 0, nc = 0, sc = 0, bc = 0;
#endif
#ifdef WORM
        worm_hash();
#endif
#ifdef AUTOTEST
        int f = (int)frame;
        int pos = (f / 200) % 8;
        camx = pos * 26;
        if (camx > LW - VW) camx = LW - VW;
#endif

        if (frame < 40 || (frame % 120) == 0)
            debugf("F frame=%llu cam=%d wpos=%ld,%ld wvel=%ld,%ld wacc=%08lx\n",
                (unsigned long long)frame, camx,
                (long)(worm.pos_x >> 16), (long)(worm.pos_y >> 16),
                (long)worm.vel_x, (long)worm.vel_y,
                (unsigned long)g_worm_acc);
        // full-state checkpoints: terrain + rng + worm + object pools
        {
            int ck = 0;
#ifdef AUTOTEST
            int ph2 = (int)(frame % 420);
            if (ph2 == 112 || ph2 == 137 || ph2 == 162 || ph2 == 180 ||
                ph2 == 262 || ph2 == 287 || ph2 == 312 || ph2 == 337 ||
                ph2 == 362 || ph2 == 387 || ph2 == 412 ||
                ph2 == 37 || ph2 == 62 || ph2 == 87) ck = 1;
#else
            if ((frame % 60) == 5) ck = 1;
#endif
            if ((frame % 60) == 59) ck = 1;
            if (ck) {
                uint32_t th = fnv(level_data, LW*LH);
#ifdef WEAPONS
                debugf("CK frame=%d terr=%08lx rng=%08lx,%08lx wc=%d nc=%d sc=%d bc=%d obj=%08lx wpos=%ld,%ld wvel=%ld,%ld hp=%d vis=%d kt=%d lr=%d,%d ammo=%d dly=%d ld=%d aim=%ld wcur=%d w=%d\n",
                    (int)frame, (unsigned long)th,
                    (unsigned long)rng_x, (unsigned long)rng_c, wc, nc, sc, bc,
                    (unsigned long)oh_out,
                    (long)(worm.pos_x >> 16), (long)(worm.pos_y >> 16),
                    (long)worm.vel_x, (long)worm.vel_y,
                    worm.health, worm.visible, worm.killed_timer, worm.lr_x, worm.lr_y,
                    wwp[0].ammo, wwp[0].delay_left, wwp[0].loading_left,
                    (long)(worm.aim_angle >> 16), worm.current_frame, cur_weapon);
#else
                debugf("CK frame=%d terr=%08lx rng=%08lx,%08lx wc=0 nc=0 sc=0 bc=0 obj=%08lx wpos=%ld,%ld wvel=%ld,%ld hp=100 vis=1 kt=0 lr=0,0 ammo=0 dly=0 ld=0 aim=%ld wcur=%d w=0\n",
                    (int)frame, (unsigned long)th,
                    (unsigned long)rng_x, (unsigned long)rng_c,
                    (unsigned long)0,
                    (long)(worm.pos_x >> 16), (long)(worm.pos_y >> 16),
                    (long)worm.vel_x, (long)worm.vel_y,
                    (long)(worm.aim_angle >> 16), worm.current_frame);
#endif
            }
        }
        ++frame;
    }
}
