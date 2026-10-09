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

typedef struct { uint8_t left, right, up, down, jump; } WormInput;

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
    for (int y = 156; y < 180; ++y)
        for (int x = 100; x < 180; ++x) {
            level_data[y*LW + x] = 1;
            level_mat[y*LW + x] = mat_flags[1];
        }
    // small step so walking tests edge reactions
    for (int y = 168; y < 180; ++y)
        for (int x = 140; x < 160; ++x) {
            level_data[y*LW + x] = 1;
            level_mat[y*LW + x] = mat_flags[1];
        }
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
        surface_t* fb = display_get();
        rdpq_attach(fb, NULL);
#ifndef NORENDER
        render_view(camx, 0);
#ifdef WORM
        draw_worm(camx, 0);
#endif
#endif
        rdpq_detach();
        display_show(fb);

        joypad_poll();
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
        (void)jin;
        WormInput win = {0,0,0,0,0};
#ifdef WORM
#ifdef AUTOTEST
        // scripted: walk right 120f, jump, walk left 120f, aim up 60f, repeat
        int ph = (int)(frame % 420);
        if (ph < 120) win.right = 1;
        else if (ph == 120) win.jump = 1;
        else if (ph >= 210 && ph < 330) win.left = 1;
        else if (ph >= 330) win.up = 1;
#else
        win.left  = jin.btn.d_left  != 0;
        win.right = jin.btn.d_right != 0;
        win.up    = jin.btn.d_up    != 0;
        win.down  = jin.btn.d_down  != 0;
        win.jump  = jin.btn.a       != 0;
#endif
        worm_process(win, frame);
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
        ++frame;
    }
}
