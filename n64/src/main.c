// liero64 slice 1: terrain generation + RDPQ run-length renderer.
// Generator mirrors src/game/level.cpp (generateDirtPattern/generateRandom/
// makeShadow) and blit.cpp (blitStone/drawDirtEffect) byte-for-byte with the
// upstream mwc RNG. Probe prints an FNV-1a hash of the level for the host
// reference (tools/bake_assets.py) to check parity.
#include <libdragon.h>
#include <stdio.h>
#include <string.h>
#include "../assets/baked.h"

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

typedef struct { uint8_t left, right, up, down, jump, fire; } WormInput;

#ifdef WEAPONS
static void worm_weapons(void);
static void worm_try_fire(uint8_t fire);
#endif

static void worm_process(WormInput in, uint64_t cycle) {
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
    worm_try_fire(in.fire);
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

    // processMovement: walk + facing flip
    worm.animate = (in.left || in.right) ? 1 : 0;
    if (in.left && !in.right) {
        if (worm.vel_x > -MAX_VEL) worm.vel_x -= WALK_VEL;
        if (worm.direction != 0) {
            worm.aim_speed = 0;
            if (worm.aim_angle >= (64<<16)) worm.aim_angle = (128<<16) - worm.aim_angle;
            worm.direction = 0;
        }
    }
    if (!in.left && in.right) {
        if (worm.vel_x < MAX_VEL) worm.vel_x += WALK_VEL;
        if (worm.direction != 1) {
            worm.aim_speed = 0;
            if (worm.aim_angle <= (64<<16)) worm.aim_angle = (128<<16) - worm.aim_angle;
            worm.direction = 1;
        }
    }

    int anim = worm.animate ? ((cycle & 31) >> 3) : 0;
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
// nobject.cpp NObject create/process with the shared upstream mwc stream.
// NOTE: this block is moved below after slice 2 (needs worm struct); marker.
#define MAX_WOBJ 256
#define MAX_NOBJ 512

typedef struct {
    int32_t x, y;       // 16.16
    int32_t vx, vy;
    int cur_frame;
    int time_left;
    int owner;
    uint8_t used;
} WObj;
static WObj wobj[MAX_WOBJ];

typedef struct {
    int32_t x, y;
    int32_t vx, vy;
    int cur_frame;
    int time_left;
    uint8_t used;
} NObj;
static NObj nobj[MAX_NOBJ];

// dirtball weapon (openliero params)
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

static struct { int ammo, delay_left, loading_left; } wwp[5];
static int fire_cone = 0;
static uint8_t prev_fire = 0;

static void correct_shadow(int x1, int y1, int x2, int y2) {
    // upstream rect intersect (0,3,LW-3,LH)
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

static void wobj_free(int i) { wobj[i].used = 0; }
static void nobj_free(int i) { nobj[i].used = 0; }

static int wobj_new(void) {  // exactObjectList getFreeObject: lowest free index
    for (int i = 0; i < MAX_WOBJ; ++i) if (!wobj[i].used) { wobj[i].used = 1; return i; }
    return MAX_WOBJ - 1;
}
static int nobj_new(void) {
    for (int i = 0; i < MAX_NOBJ; ++i) if (!nobj[i].used) { nobj[i].used = 1; return i; }
    return MAX_NOBJ - 1;
}

static void blow_up(int i) {
    WObj* o = &wobj[i];
    int px = o->x >> 16, py = o->y >> 16;
    wobj_free(i);
    // splinters: angle, colorSub per splinter, then create2 (rand inside)
    for (int k = 0; k < W_SPLINTER; ++k) {
        int angle = (int)rnd(128);
        int colorSub = (int)rnd(2);
        int realSpeed = 400 - (int)rnd(800);           // dirt: speed=400 speedV=800
        int vx = (int)(((int64_t)cs_x[angle & 127] * realSpeed) / 100);
        int vy = (int)(((int64_t)cs_y[angle & 127] * realSpeed) / 100);
        // dirt distribution=10000: two rand calls in create2
        vx += (int)rnd(20000) - 10000;
        vy += (int)rnd(20000) - 10000;
        // create(): startFrame=0, color!=0? color = 0 - colorSub; timeToExploV=0 -> no rand
        int j = nobj_new();
        nobj[j].x = px << 16; nobj[j].y = py << 16;
        nobj[j].vx = vx; nobj[j].vy = vy;
        nobj[j].x += nobj[j].vx; nobj[j].y += nobj[j].vy;  // create2 pos+=vel
        nobj[j].cur_frame = -colorSub;  // color = splinterColour(0) - colorSub
        nobj[j].time_left = N_TTIME;
    }
    // dirt effect carve + shadow
    dirt_effect(W_DIRT, px - 7, py - 7);
    correct_shadow(px - 10, py - 10, px + 11, py + 11);
}

static void fire_wobj(void) {
    // W::fire: firing pos
    int aa = (int)(worm.aim_angle >> 16) & 127;
    int fx = ((int)cs_x[aa] * 5) + worm.pos_x;        // detectDistance=0: cs*(0+5), fixed
    int fy = ((int)cs_y[aa] * 5) + worm.pos_y - (1 << 16);
    // affectByWorm: firingVel = worm.vel*100/speed
    int fvx = (int)(((int64_t)worm.vel_x * 100) / W_SPEED);
    int fvy = (int)(((int64_t)worm.vel_y * 100) / W_SPEED);
    // parts loop (parts=1)
    int angle = aa;
    int vx = (int)(((int64_t)cs_x[angle] * W_SPEED) / 100) + fvx;
    int vy = (int)(((int64_t)cs_y[angle] * W_SPEED) / 100) + fvy;
    vx += (int)rnd(W_DIST * 2) - W_DIST;
    vy += (int)rnd(W_DIST * 2) - W_DIST;
    int cur = 0;  // shotType 0, loopAnim false, startFrame>=0
    int tl = W_TTIME - (int)rnd(W_TTIMEV);
    int i = wobj_new();
    wobj[i].x = fx; wobj[i].y = fy; wobj[i].vx = vx; wobj[i].vy = vy;
    wobj[i].cur_frame = cur; wobj[i].time_left = tl; wobj[i].owner = 0;
    // recoil (0): skip rand-free branch entirely
    fire_cone = W_FIRECONE;
}

static void worm_try_fire(uint8_t fire_held) {
    if (fire_held && !prev_fire && wwp[0].loading_left == 0 && wwp[0].delay_left <= 0) {
        --wwp[0].ammo;
        wwp[0].delay_left = W_DELAY;
        fire_wobj();
    }
    prev_fire = fire_held;
}

static void worm_weapons(void) {
    for (int i = 0; i < 5; ++i)
        if (wwp[i].delay_left >= 0) --wwp[i].delay_left;
    if (wwp[0].ammo <= 0) {
        int lt = (100 * W_LOADING) / 100; if (!lt) lt = 1;
        wwp[0].loading_left = lt;
        wwp[0].ammo = W_AMMO;
    }
    if (wwp[0].loading_left > 0) --wwp[0].loading_left;
    if (fire_cone > 0) --fire_cone;
}

static void process_wobjects(uint64_t cycle) {
    for (int i = 0; i < MAX_WOBJ; ++i) {
        if (!wobj[i].used) continue;
        WObj* o = &wobj[i];
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;          // ipos
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;  // inewPos
        int inside_ix = ix < 0 ? 0 : (ix >= LW ? LW-1 : ix);
        int inside_iy = iy < 0 ? 0 : (iy >= LH ? LH-1 : iy);
        // bounce>0 path (dirtball bounce=30)
        if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
            o->vx = -(o->vx * W_BOUNCE) / 100;          // !=100: *30/100
            o->vy = (o->vy * 4) / 5;
        }
        if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
            o->vy = -(o->vy * W_BOUNCE) / 100;
            o->vx = (o->vx * 4) / 5;
        }
        // multSpeed 100 skip; objTrail -1 skip; collideWithObjects false skip
        // inewPos computed once after bounce; clip pos with it; test same value
        nx = (o->x + o->vx) >> 16; ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = (LW - 1) << 16;
        if (ny >= LH) o->y = (LH - 1) << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            // bounce!=0: no velocity change; gravity skipped
            (void)inside_ix; (void)inside_iy;
        } else {
            o->vy += W_GRAVITY;
            // numFrames=0: no anim
        }
        if (--o->time_left < 0) blow_up(i);
        // worm hit block: hitDamage/blowAway/bloodOnHit/wormCollide all 0 -> skipped
    }
}

static void process_nobjects(uint64_t cycle) {
    (void)cycle;
    for (int i = 0; i < MAX_NOBJ; ++i) {
        if (!nobj[i].used) continue;
        NObj* o = &nobj[i];
        o->x += o->vx; o->y += o->vy;
        int ix = o->x >> 16, iy = o->y >> 16;                    // ipos
        int nx = (o->x + o->vx) >> 16, ny = (o->y + o->vy) >> 16;  // inewPos
        // nobject bounce has no ==100 special-case: always *bounce/100 + *4/5 cross-damp
        if ((nx < 0 || nx >= LW) || dirtrock_at(nx, iy)) {
            o->vx = -(o->vx * 100) / 100;   // bounce=100
            o->vy = (o->vy * 4) / 5;
        }
        if ((ny < 0 || ny >= LH) || dirtrock_at(ix, ny)) {
            o->vy = -(o->vy * 100) / 100;
            o->vx = (o->vx * 4) / 5;
        }
        // re-clip: "Yes, we do this again" — upstream recomputes inewPos once here
        nx = (o->x + o->vx) >> 16; ny = (o->y + o->vy) >> 16;
        if (nx < 0) o->x = 0;
        if (ny < 0) o->y = 0;
        if (nx >= LW) o->x = LW << 16;
        if (ny >= LH) o->y = LH << 16;
        int hit = (nx < 0 || nx >= LW || ny < 0 || ny >= LH) || dirtrock_at(nx, ny);
        if (hit) {
            o->vx = 0; o->vy = 0;            // explGround false
        }
        // dirt gravity=0, leaveObj none, bloodTrail off, numFrames 0
        if (--o->time_left <= 0) {
            dirt_effect(N_DIRT, (o->x >> 16) - 7, (o->y >> 16) - 7);
            correct_shadow((o->x >> 16) - 10, (o->y >> 16) - 10, (o->x >> 16) + 11, (o->y >> 16) + 11);
            nobj_free(i);
        }
    }
}

// render wobjects: startFrame>0 -> small sprite 7x7 at pos-3; else pixel curFrame
static void draw_wobjects(int camx, int camy) {
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int i = 0; i < MAX_WOBJ; ++i) {
        if (!wobj[i].used) continue;
        int px = (wobj[i].x >> 16) - 3 - camx;
        int py = (wobj[i].y >> 16) - 3 - camy;
        int f = W_STARTFRAME + wobj[i].cur_frame;  // dirtball startFrame 87
        if (f >= SMALL_FRAMES) f = 87;
        const uint8_t* src = small_pix + (size_t)f * 49;
        for (int y = 0; y < 7; ++y) {
            int dy = py + y;
            if (dy < 0 || dy >= VH) continue;
            for (int x = 0; x < 7; ++x) {
                uint8_t idx = src[y*7 + x];
                if (idx == 0) continue;
                int dx = px + x;
                if (dx < 0 || dx >= VW) continue;
                uint16_t c = pal_rgb555[idx];
                int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
                rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
            }
        }
    }
    // dirt nobjects: single pixel, idx -curFrame (0/-1) skipped
    for (int i = 0; i < MAX_NOBJ; ++i) {
        if (!nobj[i].used) continue;
        int cf = nobj[i].cur_frame;
        if (cf == 0 || cf == -1) continue;
        int dx = (nobj[i].x >> 16) - camx;
        int dy = (nobj[i].y >> 16) - camy;
        if (dx < 0 || dx >= VW || dy < 0 || dy >= VH) continue;
        uint16_t c = pal_rgb555[-cf & 255];
        int r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
        rdpq_set_prim_color(RGBA32((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255));
        rdpq_fill_rectangle(dx, dy, dx+1, dy+1);
    }
}

// wobj/nobj state hash (debug channel)
static uint32_t g_obj_acc = 2166136261u;
static void worm_hash_weap(void) {
    uint8_t b[24];
    int n = 0;
    int wcount = 0, ncount = 0;
    for (int i = 0; i < MAX_WOBJ && n < 24; ++i) {
        if (!wobj[i].used) continue;
        if (++wcount > 2) break;
        b[n++] = (uint8_t)(wobj[i].x >> 8); b[n++] = (uint8_t)(wobj[i].x);
        b[n++] = (uint8_t)(wobj[i].y >> 8); b[n++] = (uint8_t)(wobj[i].y);
        b[n++] = (uint8_t)(wobj[i].vx >> 8); b[n++] = (uint8_t)(wobj[i].vx);
        b[n++] = (uint8_t)(wobj[i].vy >> 8); b[n++] = (uint8_t)(wobj[i].vy);
    }
    for (int i = 0; i < MAX_NOBJ && n < 24; ++i) {
        if (!nobj[i].used) continue;
        if (++ncount > 1) break;
        b[n++] = (uint8_t)(nobj[i].x >> 8); b[n++] = (uint8_t)(nobj[i].x);
        b[n++] = (uint8_t)(nobj[i].y >> 8); b[n++] = (uint8_t)(nobj[i].y);
    }
    b[n++] = (uint8_t)wcount; b[n++] = (uint8_t)ncount;
    b[n++] = (uint8_t)wwp[0].ammo; b[n++] = (uint8_t)(wwp[0].delay_left + 128);
    g_obj_acc = fnv2(g_obj_acc, b, n);
}

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
    wwp[0].ammo = W_AMMO;
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
#ifdef WEAPONS
        process_wobjects(cycle);
        process_nobjects(cycle);
#ifdef WEAPONS_AUTOTEST_DUMP
        if (frame >= 108 && frame <= 265 && (frame % 5) == 2) {
            for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used)
                debugf("WT f=%llu i=%d x=%ld y=%ld vx=%ld vy=%ld t=%d\n",
                    (unsigned long long)frame, j,
                    (long)wobj[j].x, (long)wobj[j].y,
                    (long)wobj[j].vx, (long)wobj[j].vy, wobj[j].time_left);
        }
#endif
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
        // aim at floor then fire dirtballs at start of each cycle period
        if (ph >= 100 && ph < 118) win.down = 1;
        if (ph == 110 || ph == 135 || ph == 160 || ph == 185 || ph == 210) win.fire = 1;
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
        worm_hash_weap();
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

        if (frame < 40 || (frame % 60) == 0)
            debugf("F frame=%llu cam=%d wpos=%ld,%ld wvel=%ld,%ld wacc=%08lx\n",
                (unsigned long long)frame, camx,
                (long)(worm.pos_x >> 16), (long)(worm.pos_y >> 16),
                (long)worm.vel_x, (long)worm.vel_y,
                (unsigned long)g_worm_acc);
#ifdef WEAPONS
        // full-state checkpoints: terrain + rng + worm + object pools
        {
            int ck = 0;
#ifdef AUTOTEST
            int ph2 = (int)(frame % 420);
            if (ph2 == 112 || ph2 == 137 || ph2 == 162 || ph2 == 180) ck = 1;
#else
            if ((frame % 60) == 5) ck = 1;
#endif
            if ((frame % 60) == 59) ck = 1;
            if (ck) {
                uint32_t th = fnv(level_data, LW*LH);
                int wc = 0, nc = 0;
                uint32_t oh = 2166136261u;
                uint8_t ab[28];
                for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used) {
                    wc++;
                    int32_t v[7] = {wobj[j].x, wobj[j].y, wobj[j].vx, wobj[j].vy,
                                    wobj[j].cur_frame, wobj[j].time_left, wobj[j].owner};
                    for (int k = 0; k < 7; ++k) {
                        ab[k*4+0] = (uint8_t)(v[k]);
                        ab[k*4+1] = (uint8_t)(v[k] >> 8);
                        ab[k*4+2] = (uint8_t)(v[k] >> 16);
                        ab[k*4+3] = (uint8_t)(v[k] >> 24);
                    }
                    oh = fnv2(oh, ab, 28);
                }
                for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) {
                    nc++;
                    int32_t v[6] = {nobj[j].x, nobj[j].y, nobj[j].vx, nobj[j].vy,
                                    nobj[j].cur_frame, nobj[j].time_left};
                    for (int k = 0; k < 6; ++k) {
                        ab[k*4+0] = (uint8_t)(v[k]);
                        ab[k*4+1] = (uint8_t)(v[k] >> 8);
                        ab[k*4+2] = (uint8_t)(v[k] >> 16);
                        ab[k*4+3] = (uint8_t)(v[k] >> 24);
                    }
                    oh = fnv2(oh, ab, 24);
                }
                debugf("CK frame=%llu terr=%08lx rng=%08lx,%08lx wc=%d nc=%d obj=%08lx wpos=%ld,%ld wvel=%ld,%ld ammo=%d dly=%d ld=%d aim=%ld\n",
                    (unsigned long long)frame, (unsigned long)th,
                    (unsigned long)rng_x, (unsigned long)rng_c, wc, nc,
                    (unsigned long)oh,
                    (long)(worm.pos_x >> 16), (long)(worm.pos_y >> 16),
                    (long)worm.vel_x, (long)worm.vel_y,
                    wwp[0].ammo, wwp[0].delay_left, wwp[0].loading_left,
                    (long)(worm.aim_angle >> 16));
            }
        }
#endif
        ++frame;
    }
}
