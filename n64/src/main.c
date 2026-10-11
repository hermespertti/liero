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
#ifdef WEAPONS_AUTOTEST_DUMP
static uint32_t rndm_(int m, int ln);   /* traced wrapper, defined below */
#endif

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

static int g_frame;
static void dirt_effect(int eff, int x, int y) {
#ifdef WEAPONS_AUTOTEST_DUMP
    if (g_frame >= 595 && g_frame <= 610) debugf("DS f=%d e=%d x=%d y=%d\n", g_frame, eff, x, y);
#endif
    const TexEnt* tex = &tex_tab[eff];
#ifdef WEAPONS_AUTOTEST_DUMP
    const uint8_t* t_frame = large_stamps + ((size_t)(tex->sFrame + rndm_((int)tex->rFrame, __LINE__)) * 256);
#else
    const uint8_t* t_frame = large_stamps + ((size_t)(tex->sFrame + rnd(tex->rFrame)) * 256);
#endif
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
#if !defined(NORENDER) && defined(PATTERN)
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
#elif !defined(NORENDER)
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

typedef struct { uint8_t left, right, up, down, jump, fire, change; } WKeys;
#define WormInput_WKeys_OK 1

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
    // duel (slice 5)
    int index;
    int wslot;
    int weapon;
    int last_killed_by;
    int kills;
    uint8_t prev_fire, prev_jump;
    uint8_t key_change_pressed;
    uint8_t prev_left, prev_right;
    int fire_cone;
    int leave_shell_timer;
    struct { int ammo, delay_left, loading_left; } wwp[2];
    // AI state (DumbLieroAI persistent control states)
    WKeys ai;
    WKeys keys;
    int ai_on;
    // per-worm viewport
    int vp_x, vp_y, shake;
} Worm;
#define N_WORMS 2
#ifdef FRAMES_AUTO_X
#define FRAMES_AUTO FRAMES_AUTO_X
#else
#define FRAMES_AUTO 1260
#endif
static Worm worms[N_WORMS];
#define worm worms[0]

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

static void calc_reaction(Worm* w, int nx, int ny, int dir) {
    w->reacts[dir] = (uint8_t)reaction_count(nx, ny, dir);
}

static inline int tdiv(int a, int b) {  // C truncation toward zero
    return a / b;
}

static int angle_frame(Worm* w) {
    int x = (w->aim_angle >> 16) - 12;
    if (w->direction != 0) x -= 49;
    x >>= 3;
    if (x < 0) x = 0; else if (x > 6) x = 6;
    if (w->direction != 0) x = 6 - x;
    return x;
}

typedef WKeys WormInput;
static int g_last_killed = -1;
#ifdef AI_VERSUS_AI
static int worms_ai_on[N_WORMS] = {1, 1};   // duel: both AI
#else
static int worms_ai_on[N_WORMS] = {0, 1};   // duel: worm1 = AI
#endif

#ifdef WEAPONS
static void worm_weapons(Worm* w);
static void worm_try_fire(Worm* w, uint8_t fire, uint8_t change);
static void worm_change_weapon(Worm* w, uint8_t change, uint8_t left, uint8_t right);
static void worm_die(Worm* w);
static void worm_dead_process(Worm* w, uint8_t fire_held);
static void do_damage(Worm* w, int amount, int by_idx);
static uint32_t rndm_(int m, int ln);
#define rndm(m) rndm_((m), __LINE__)
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color, int owner);
#endif

#ifdef WEAPONS
enum { AI_UP=0, AI_DOWN=1, AI_LEFT=2, AI_RIGHT=3, AI_FIRE=4, AI_CHANGE=5, AI_JUMP=6 };
static int isqrt_u32(unsigned int v) { int r = 0; for (int b = 1<<15; b; b >>= 1) { int t = r | b; if ((unsigned)t*t <= v) r = t; } return r; }

// DumbLieroAI (upstream worm.cpp:538) — control states persist in w->ai
static void ai_process(Worm* w) {
    Worm* target = NULL; int min_len = 0;
    for (int i = 0; i < N_WORMS; ++i) {
        Worm* o = &worms[i];
        if (o == w) continue;
        int dx = (w->pos_x >> 16) - (o->pos_x >> 16);
        int dy = (w->pos_y >> 16) - (o->pos_y >> 16);
        int len = dx*dx + dy*dy;
        if (!target || len < min_len) { target = o; min_len = len; }
    }
    if (!target) return;
    const int32_t* wp = c_wparam[w->weapon];
    int tte = wp[WK_timeToExplo];
    int max_dist;
    if (tte > 0 && tte < 500)
        max_dist = tdiv((tte - wp[WK_timeToExploV] / 2) * wp[WK_speed], 130);
    else
        max_dist = wp[WK_speed] - wp[WK_gravity] / 10;
    if (max_dist < 90) max_dist = 90;
    int ddx_i = (target->pos_x >> 16) - (w->pos_x >> 16);
    int ddy_i = (target->pos_y >> 16) - (w->pos_y >> 16);
    int real = isqrt_u32(ddx_i*ddx_i + ddy_i*ddy_i);
    // fire
    int fire = w->ai.fire;
    if (real < max_dist || !w->visible) {
        if (rndm(c_ai_k[fire][AI_FIRE]) == 0) fire = !fire;
    } else if (w->visible) {
        fire = 0;
    }
    int jump = w->ai.jump;
    if (rndm(c_ai_k[jump][AI_JUMP]) == 0) jump = !jump;
    int change = w->ai.change;
    if (rndm(c_ai_k[change][AI_CHANGE]) == 0) change = !change;
    int32_t ddx = 0, ddy = 0;
    if (real > 0) { ddx = tdiv((int32_t)ddx_i << 16, real); ddy = tdiv((int32_t)ddy_i << 16, real); }
    int dir = 1;
    while (dir < 128) {
        int ax_ = cs_x[dir] - ddx; if (ax_ < 0) ax_ = -ax_;
        int ay_ = cs_y[dir] - ddy; if (ay_ < 0) ay_ = -ay_;
        if (ax_ < 0xC00 && ay_ < 0xC00) break;
        ++dir;
    }
    int32_t adx = ddx < 0 ? -ddx : ddx;
    int32_t ady = ddy < 0 ? -ddy : ddy;
    if (dir >= 128) {
        if (ddx > 0) {
            if (ddy < 0) {
                if (ady > adx) dir = 64 + (int)rndm(16);
                else if (adx > ady) dir = 80 + (int)rndm(16);
                else dir = 80;
            } else {
                if (adx > ady) dir = 96 + (int)rndm(16);
                else dir = 116;
            }
        } else {
            if (ddy < 0) {
                if (ady > adx) dir = 48 + (int)rndm(16);
                else if (adx > ady) dir = 32 + (int)rndm(16);
                else dir = 48;
            } else {
                if (adx > ady) dir = 12 + (int)rndm(16);
                else dir = 12;
            }
        }
    }
    int left = w->ai.left, right = w->ai.right, up = w->ai.up, down = w->ai.down;
    if (change) {
        if (rndm(c_ai_k[left][AI_LEFT]) == 0) left = !left;
        if (rndm(c_ai_k[right][AI_RIGHT]) == 0) right = !right;
        up = 0; down = 0;   // ninjarope branch omitted (no rope)
    } else {
        if (real > max_dist) {
            right = (ddx_i > 0); left = (ddx_i <= 0);
        } else {
            left = 0; right = 0;
        }
        int aa = w->aim_angle >> 16;
        if (w->direction != 0) {
            if (dir < 64) left = 1;
            up   = (dir + 1 < aa);
            down = (dir - 1 > aa);
        } else {
            if (dir > 64) right = 1;
            up   = (dir - 1 > aa);
            down = (dir + 1 < aa);
        }
        if (left && w->reacts[RF_RIGHT]) {
            if (w->reacts[RF_DOWN] > 0) right = 1; else jump = 1;
        }
        if (right && w->reacts[RF_LEFT]) {
            if (w->reacts[RF_DOWN] > 0) left = 1; else jump = 1;
        }
    }
    w->ai.fire = fire; w->ai.jump = jump; w->ai.change = change;
    w->ai.left = left; w->ai.right = right; w->ai.up = up; w->ai.down = down;
}
#endif  // WEAPONS (ai_process)

static void worm_process(Worm* w, WormInput in, uint64_t cycle) {
#ifdef WEAPONS
    if (g_frame == 360 && w->index == 0) do_damage(w, 97, 1);   // AUTOTEST: near-lethal hit
    if (g_frame == 420 && w->index == 0) do_damage(w, 99, 1);   // AUTOTEST: lethal hit
    if (!w->visible) { worm_dead_process(w, in.fire); return; }
#endif
    int32_t next_x = w->pos_x + w->vel_x;
    int32_t next_y = w->pos_y + w->vel_y;
    int inx = next_x >> 16, iny = next_y >> 16;

    // reactions + wall edges, per upstream: edge checks run inside dir loop
    for (int d = 0; d < 4; ++d) {
        calc_reaction(w, inx, iny, d);
        if (inx < 4) w->reacts[RF_RIGHT] += 5;
        else if (inx > LW - 5) w->reacts[RF_LEFT] += 5;
        if (iny < 5) w->reacts[RF_DOWN] += 5;
        else if (iny > LH - 6) w->reacts[RF_UP] += 5;
    }
    if (w->reacts[RF_DOWN] < 2 && w->reacts[RF_UP] > 0 &&
        (w->reacts[RF_LEFT] > 0 || w->reacts[RF_RIGHT] > 0)) {
        w->pos_y -= 1<<16;
        next_y = w->pos_y + w->vel_y; iny = next_y >> 16;
        calc_reaction(w, inx, iny, RF_LEFT); calc_reaction(w, inx, iny, RF_RIGHT);
    }
    if (w->reacts[RF_UP] < 2 && w->reacts[RF_DOWN] > 0 &&
        (w->reacts[RF_LEFT] > 0 || w->reacts[RF_RIGHT] > 0)) {
        w->pos_y += 1<<16;
        next_y = w->pos_y + w->vel_y; iny = next_y >> 16;
        calc_reaction(w, inx, iny, RF_LEFT); calc_reaction(w, inx, iny, RF_RIGHT);
    }

    // processAiming
    if (w->aim_speed != 0) {
        w->aim_angle += w->aim_speed;
        if (!in.up && !in.down)
            w->aim_speed = tdiv(w->aim_speed * AIM_FRIC_M, AIM_FRIC_D);
        if (w->direction == 1) {
            if ((w->aim_angle >> 16) > AIM_MAX_R) { w->aim_speed = 0; w->aim_angle = AIM_MAX_R << 16; }
            if ((w->aim_angle >> 16) < AIM_MIN_R) { w->aim_speed = 0; w->aim_angle = AIM_MIN_R << 16; }
        } else {
            if ((w->aim_angle >> 16) < AIM_MAX_L) { w->aim_speed = 0; w->aim_angle = AIM_MAX_L << 16; }
            if ((w->aim_angle >> 16) > AIM_MIN_L) { w->aim_speed = 0; w->aim_angle = AIM_MIN_L << 16; }
        }
    }
    if (in.up) {
        if (w->direction == 0) { if (w->aim_speed < MAX_AIM_VEL) w->aim_speed += AIM_ACC; }
        else { if (w->aim_speed > -MAX_AIM_VEL) w->aim_speed -= AIM_ACC; }
    }
    if (in.down) {
        if (w->direction == 1) { if (w->aim_speed < MAX_AIM_VEL) w->aim_speed += AIM_ACC; }
        else { if (w->aim_speed > -MAX_AIM_VEL) w->aim_speed -= AIM_ACC; }
    }

    // processTasks: jump (edge-triggered, ground contact RF_UP > 0)
    if (in.jump && !w->prev_jump && w->reacts[RF_UP] > 0 && w->able_to_jump) {
        w->vel_y -= JUMP_FORCE;
        w->able_to_jump = 0;
    }
    if (!in.jump) w->able_to_jump = 1;
    w->prev_jump = in.jump;

#ifdef WEAPONS
    // upstream: processWeapons then fire-check, both before processPhysics
    worm_weapons(w);
    worm_try_fire(w, in.fire, in.change);                       // upstream: fire first
    if (in.change) worm_change_weapon(w, in.change, in.left, in.right);
    else worm_change_weapon(w, 0, in.left, in.right);          // keyChangePressed=false
#endif

    // processPhysics
    if (w->reacts[RF_UP] > 0)
        w->vel_x = tdiv(w->vel_x * WORM_FRIC_M, WORM_FRIC_D);
    int32_t ax = w->vel_x < 0 ? -w->vel_x : w->vel_x;
    int32_t ay = w->vel_y < 0 ? -w->vel_y : w->vel_y;
    uint8_t rh = w->reacts[w->vel_x >= 0 ? RF_LEFT : RF_RIGHT];
    uint8_t rv = w->reacts[w->vel_y >= 0 ? RF_UP : RF_DOWN];
    if (w->vel_x && rh) {
        if (ax > MIN_BOUNCE) w->vel_x = -tdiv(w->vel_x, 3);
        else w->vel_x = 0;
    }
    if (w->vel_y && rv) {
        if (ay > MIN_BOUNCE) w->vel_y = -tdiv(w->vel_y, 3);
        else w->vel_y = 0;
    }
    if (w->reacts[RF_UP] == 0) w->vel_y += GRAVITY;
    if (w->reacts[w->vel_x >= 0 ? RF_LEFT : RF_RIGHT] < 2) w->pos_x += w->vel_x;
    if (w->reacts[w->vel_y >= 0 ? RF_UP : RF_DOWN] < 2) w->pos_y += w->vel_y;

    // processMovement: walk + facing flip (skipped while Change held, upstream)
#ifdef WEAPONS
    if (in.change) w->animate = 0;
    else
#endif
    w->animate = (in.left || in.right) ? 1 : 0;
    if (!in.change && in.left && !in.right) {
        if (w->vel_x > -MAX_VEL) w->vel_x -= WALK_VEL;
        if (w->direction != 0) {
            w->aim_speed = 0;
            if (w->aim_angle >= (64<<16)) w->aim_angle = (128<<16) - w->aim_angle;
            w->direction = 0;
        }
    }
    if (!in.change && !in.left && in.right) {
        if (w->vel_x < MAX_VEL) w->vel_x += WALK_VEL;
        if (w->direction != 1) {
            w->aim_speed = 0;
            if (w->aim_angle <= (64<<16)) w->aim_angle = (128<<16) - w->aim_angle;
            w->direction = 1;
        }
    }

#ifdef WEAPONS
    // pain blood: health < 25 (settings->health/4)
    if (w->health < 25) {
        if (rndm(w->health + 6) == 0) {
            if (rndm(3) == 0) rndm(3);
            nobj_create1(IDX_BLOOD, w->vel_x, w->vel_y, w->pos_x, w->pos_y, 0, w->index);
        }
    }
    int died = 0;
    if (w->health <= 0) { worm_die(w); died = 1; }
    int anim = (w->animate && !died) ? ((cycle & 31) >> 3) : 0;
#else
    int anim = w->animate ? ((cycle & 31) >> 3) : 0;
#endif
    w->current_frame = (uint8_t)(angle_frame(w) + worm_anim_tab[anim]);
}

#ifdef WEAPONS
static const int weapons_list[2] = { IDX_DIRTBALL, IDX_HANDGUN };
#define WEAPONS_LIST_N 2
#endif

static void worm_init(Worm* w, int idx, int px, int py, int color) {
    w->pos_x = px << 16; w->pos_y = py << 16;
    w->vel_x = w->vel_y = 0;
    w->aim_angle = 64 << 16; w->aim_speed = 0;
    w->direction = 1; w->color_idx = color;
    w->able_to_jump = 1; w->animate = 0; w->current_frame = 0;
    w->index = idx;
    w->health = 100; w->lives = 5;
    w->visible = 1; w->ready = 0; w->killed_timer = 0;
    w->lr_x = px - 80; w->lr_y = py - 80;   // upstream logicRespawn = temp - (80,80)
#ifdef WEAPONS
    w->wslot = 0; w->weapon = weapons_list[idx];
    w->last_killed_by = -1; w->kills = 0;
    w->prev_fire = 0; w->key_change_pressed = 0;
    w->prev_left = 0; w->prev_right = 0; w->prev_jump = 0;
    w->fire_cone = 0; w->leave_shell_timer = 0;
    for (int k = 0; k < 2; ++k) {
        w->wwp[k].ammo = c_wparam[weapons_list[k]][WK_ammo];
        w->wwp[k].delay_left = 0; w->wwp[k].loading_left = 0;
    }
    memset(&w->ai, 0, sizeof(WKeys));
    memset(&w->keys, 0, sizeof(WKeys));
#endif
    w->vp_x = 0; w->vp_y = 0; w->shake = 0;
}

// draw worm sprite over terrain: per-row run fills, skip transparent 0
#ifndef NORENDER
static void draw_worm(Worm* w, int camx, int camy) {
    int f = 16 + w->current_frame;
    if (f >= LARGE_FRAMES) f = 16;
    const uint8_t* src = large_stamps + f*256;
    int sx = (w->pos_x >> 16) - 7 - camx;
    int sy = (w->pos_y >> 16) - 5 - camy;
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    for (int y = 0; y < 16; ++y) {
        int dy = sy + y;
        if (dy < 0 || dy >= VH) continue;
        int x = 0;
        while (x < 16) {
            int px = w->direction == 1 ? x : 15 - x;
            uint8_t idx = src[y*16 + px];
            if (idx == 0) { ++x; continue; }
            int x0 = x;
            uint8_t prev_idx = idx;
            ++x;
            while (x < 16) {
                int p2 = w->direction == 1 ? x : 15 - x;
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
#endif  // !NORENDER (draw_worm)

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
    int owner;          // worm index
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

static int g_screen_flash = 0;



static uint32_t rndm_(int m, int ln) {   // upstream rand(int): unsigned-wrap bounded, always burns
    uint32_t v = rnd((uint32_t)m);
#ifdef WEAPONS_AUTOTEST_DUMP
    if (g_frame >= 595 && g_frame <= 605) debugf("RR f=%d m=%d v=%u x=%08lx L=%d\n", g_frame, m, (unsigned)v, (unsigned long)rng_x, ln);
#endif
    return v;
}
#define rndm(m) rndm_((m), __LINE__)

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
static void nobj_create2(int t, int angle, int bvx, int bvy, int px_, int py_, int color, int owner);
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color, int owner);

// NObjectType::create core (exact RNG order: startFrame rand, color, tte rand)
static int nobj_core(int t, int vx, int vy, int px_, int py_, int color, int owner) {
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
    nobj[i].type = t; nobj[i].owner = owner;
    return i;
}
static void nobj_create1(int t, int vx, int vy, int px_, int py_, int color, int owner) {
    int d = c_nparam[t][NK_distribution];
    if (d) {
        vx += d - (int)rndm(d*2);   // upstream create1: distribution - rand
        vy += d - (int)rndm(d*2);
    }
    nobj_core(t, vx, vy, px_, py_, color, owner);
}
static void nobj_create2(int t, int angle, int bvx, int bvy, int px_, int py_, int color, int owner) {
    const int32_t* p = c_nparam[t];
    int rs = p[NK_speed] - (int)rndm(p[NK_speedV]);
    int vx = bvx + tdiv(cs_x[angle & 127] * rs, 100);
    int vy = bvy + tdiv(cs_y[angle & 127] * rs, 100);
    int d = p[NK_distribution];
    if (d) {
        vx += (int)rndm(d*2) - d;
        vy += (int)rndm(d*2) - d;
    }
    int i = nobj_core(t, vx, vy, px_, py_, color, owner);
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
    for (int vi = 0; vi < N_WORMS; ++vi) {
        Worm* v = &worms[vi];
        if (x > v->vp_x && x < v->vp_x + VW && y > v->vp_y && y < v->vp_y + VH) {
            if (shk > v->shake) v->shake = shk;
        }
    }
    int damage = p[SK_damage];
    int dr = p[SK_detectRange];
    if (damage > 0) {
      for (int wi = 0; wi < N_WORMS; ++wi) {
        Worm* w = &worms[wi];
        int wix = w->pos_x >> 16, wiy = w->pos_y >> 16;
        if (wix < x + dr && wix > x - dr && wiy < y + dr && wiy > y - dr) {
            int delta = wix - x;
            int power = dr - (delta < 0 ? -delta : delta);
            int power_sum = power;
            if (w->vel_x > -131072 && w->vel_x < 131072) {
                if (delta > 0) w->vel_x += p[SK_blowAway] * power;
                else           w->vel_x -= p[SK_blowAway] * power;
            }
            delta = wiy - y;
            power = dr - (delta < 0 ? -delta : delta);
            power_sum = (power_sum + power) / 2;
            if (w->vel_y > -131072 && w->vel_y < 131072) {
                if (delta > 0) w->vel_y += p[SK_blowAway] * power;
                else           w->vel_y -= p[SK_blowAway] * power;
            }
            int z = damage * power_sum;
            if (dr) z = tdiv(z, dr);
            if (w->health > 0) {
                do_damage(w, z, owner_idx);
                int blood_amount = power_sum;   // settings->blood = 100
                for (int k = 0; k < blood_amount; ++k) {
                    int angle = (int)rndm(128);
                    nobj_create2(IDX_BLOOD, angle, tdiv(w->vel_x,3), tdiv(w->vel_y,3), w->pos_x, w->pos_y, 0, wi);
                }
                if (rndm(3) == 0) rndm(3);   // pain sound pick
            }
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
                    nobj_create2(IDX_PART2, angle, 0, 0, xx << 16, yy << 16, pix, owner_idx);
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
                nobj_create2(p[WK_splinterType], angle, 0, 0, px_ << 16, py_ << 16, p[WK_splinterColour] - colorSub, cause_idx);
            }
        } else {
            for (int k = 0; k < splinters; ++k) {
                int colorSub = (int)rndm(2);
                nobj_create1(p[WK_splinterType], velX, velY, px_ << 16, py_ << 16, p[WK_splinterColour] - colorSub, cause_idx);
            }
        }
    }
    if (p[WK_dirtEffect] >= 0) {
        dirt_effect(p[WK_dirtEffect], px_ - 7, py_ - 7);
        correct_shadow(px_ - 10, py_ - 10, px_ + 11, py_ + 11);
    }
}

// checkForSpecWormHit: mirrored to match draw_worm
static int check_spec_worm_hit(Worm* w, int x, int y, int dist) {
    if (!w->visible) return 0;
    int f = 16 + w->current_frame;
    if (f >= LARGE_FRAMES) f = 16;
    const uint8_t* ws = large_stamps + (size_t)f * 256;
    int dX = x - (w->pos_x >> 16) + 7;
    int dY = y - (w->pos_y >> 16) + 5;
    int x1 = dX - dist < 0 ? 0 : dX - dist;
    int y1 = dY - dist < 0 ? 0 : dY - dist;
    int x2 = dX + dist + 1 > 16 ? 16 : dX + dist + 1;
    int y2 = dY + dist + 1 > 16 ? 16 : dY + dist + 1;
    for (int cy = y1; cy < y2; ++cy)
        for (int cx = x1; cx < x2; ++cx) {
            int pi = w->direction == 1 ? cx : 15 - cx;
            if (mat_flags[ws[cy*16 + pi]] & M_WORM) return 1;
        }
    return 0;
}

static void do_damage(Worm* w, int amount, int by_idx) {
    if (amount > 0) {
        w->health -= amount;
#ifdef AUTOTEST
        debugf("DMG f=%d w=%d a=%d hp=%d by=%d\n", g_frame, w->index, amount, w->health, by_idx);
#endif
        if (w->health <= 0) w->last_killed_by = by_idx;
    }
}

// Weapon::fire STNormal subset + recoil + shells rand (before parts loop)
static void fire_wobj(Worm* w) {
    const int32_t* p = c_wparam[w->weapon];
    int aa = (int)(w->aim_angle >> 16) & 127;
    int fx = tdiv(cs_x[aa] * (p[WK_detectDistance] + 5), 1) + w->pos_x;
    int fy = tdiv(cs_y[aa] * (p[WK_detectDistance] + 5), 1) + w->pos_y - (1 << 16);
    if (p[WK_leaveShells] > 0 && rndm(p[WK_leaveShells]) == 0)
        w->leave_shell_timer = p[WK_leaveShellDelay];
    int speed = p[WK_speed];
    int fvx = 0, fvy = 0;
    if (p[WK_affectByWorm]) {
        if (speed < 100) speed = 100;
        fvx = tdiv(w->vel_x * 100, speed);
        fvy = tdiv(w->vel_y * 100, speed);
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
        wobj[i].cur_frame = cur; wobj[i].time_left = tl; wobj[i].owner = w->index;
        wobj[i].type = w->weapon; wobj[i].has_hit = 0;
#ifdef WEAPONS_AUTOTEST_DUMP
        if (1)
            debugf("WC f=%d wi=%d w=%d x=%ld y=%ld vx=%ld vy=%ld t=%d ty=%d\n",
                g_frame, i, w->index, (long)fx, (long)fy, (long)vx, (long)vy, tl, w->weapon);
#endif
    }
    int recoil = p[WK_recoil];
    w->vel_x -= tdiv(cs_x[aa] * recoil, 100);
    w->vel_y -= tdiv(cs_y[aa] * recoil, 100);
}

static void worm_try_fire(Worm* w, uint8_t fire_held, uint8_t change_held) {
    // upstream: level model — fires whenever held, loaded, delay expired, no Change
#ifdef WEAPONS_AUTOTEST_DUMP
    if (g_frame >= 336 && g_frame <= 352)
        debugf("FS f=%d w=%d slot=%d wpn=%d am=%d dly=%d ld=%d fi=%d chg=%d\n",
            g_frame, w->index, w->wslot, w->weapon, w->wwp[w->wslot].ammo,
            w->wwp[w->wslot].delay_left, w->wwp[w->wslot].loading_left, fire_held, change_held);
#endif
    if (fire_held && !change_held && w->wwp[w->wslot].loading_left == 0 && w->wwp[w->wslot].delay_left <= 0) {
        --w->wwp[w->wslot].ammo;
        w->wwp[w->wslot].delay_left = c_wparam[w->weapon][WK_delay];
        fire_wobj(w);
    }
    w->prev_fire = fire_held;
}

// processWeaponChange: keyChangePressed release-once, pressedOnce(L/R) via prev-edge tracking
static void worm_change_weapon(Worm* w, uint8_t change_held, uint8_t left_held, uint8_t right_held) {
    if (change_held) {
        if (!w->key_change_pressed) {
            w->prev_left = 0; w->prev_right = 0;   // release(Left/Right)
            w->key_change_pressed = 1;
        }
        w->fire_cone = 0; w->animate = 0;
        if (w->wwp[w->wslot].loading_left == 0 || 1) {   // available() || loadChange(true)
            if (left_held && !w->prev_left) {
                if (--w->wslot < 0) w->wslot = WEAPONS_LIST_N - 1;
                w->weapon = weapons_list[w->wslot];
                w->wwp[w->wslot].ammo = c_wparam[w->weapon][WK_ammo];
                w->wwp[w->wslot].loading_left = 0;
                w->leave_shell_timer = 0;
            }
            if (right_held && !w->prev_right) {
                if (++w->wslot >= WEAPONS_LIST_N) w->wslot = 0;
                w->weapon = weapons_list[w->wslot];
                w->wwp[w->wslot].ammo = c_wparam[w->weapon][WK_ammo];
                w->wwp[w->wslot].loading_left = 0;
                w->leave_shell_timer = 0;
            }
        }
    } else {
        w->key_change_pressed = 0;
    }
    w->prev_left = change_held ? left_held : 0;
    w->prev_right = change_held ? right_held : 0;
}

static void worm_weapons(Worm* w) {
    for (int i = 0; i < WEAPONS_LIST_N; ++i)
        if (w->wwp[i].delay_left >= 0) --w->wwp[i].delay_left;
    if (w->wwp[w->wslot].ammo <= 0) {
        int lt = (100 * c_wparam[w->weapon][WK_loadingTime]) / 100; if (!lt) lt = 1;
        w->wwp[w->wslot].loading_left = lt;
        w->wwp[w->wslot].ammo = c_wparam[w->weapon][WK_ammo];
    }
    if (w->wwp[w->wslot].loading_left > 0) --w->wwp[w->wslot].loading_left;
    if (w->fire_cone > 0) --w->fire_cone;
    if (w->leave_shell_timer > 0) {
        if (--w->leave_shell_timer <= 0) {
            int vy = -(int)rndm(20000);
            int vx = (int)rndm(16000) - 8000;
            nobj_create1(IDX_SHELLS, vx, vy, w->pos_x, w->pos_y, 0, w->index);
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
#ifdef WEAPONS_AUTOTEST_DUMP
        if (g_frame <= 190) debugf("WS f=%d wi=%d x=%ld y=%ld t=%d\n", g_frame, i, (long)o->x, (long)o->y, o->time_left);
#endif
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
        for (int wi = 0; wi < N_WORMS; ++wi) {
        Worm* w = &worms[wi];
        if ((p[WK_hitDamage] || p[WK_blowAway] || p[WK_bloodOnHit] || p[WK_wormCollide])
            && check_spec_worm_hit(w, o->x >> 16, o->y >> 16, p[WK_detectDistance])) {
            w->vel_x += tdiv(o->vx * p[WK_blowAway], 100);
            w->vel_y += tdiv(o->vy * p[WK_blowAway], 100);
            do_damage(w, p[WK_hitDamage], o->owner);
            o->has_hit = 1;
            int blood = p[WK_bloodOnHit];   // *settings->blood(100)/100
            for (int k = 0; k < blood; ++k) {
                int angle = (int)rndm(128);
                nobj_create2(IDX_BLOOD, angle, tdiv(o->vx,3), tdiv(o->vy,3), o->x, o->y, 0, wi);
            }
            if (p[WK_hitDamage] > 0 && w->health > 0 && rndm(3) == 0) rndm(3);
            if (p[WK_wormCollide] && rndm(p[WK_wormCollide]) == 0) {
                if (p[WK_wormExplode]) do_explode = 1;
                do_remove = 1;
            }
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
        nobj_create2(p[NK_splinterType], angle, 0, 0, px_ << 16, py_ << 16, p[NK_splinterColour] - cs, 0);
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
        for (int wi = 0; wi < N_WORMS; ++wi) {
        Worm* w = &worms[wi];
        if (!do_explode && p[NK_hitDamage] > 0
            && check_spec_worm_hit(w, o->x >> 16, o->y >> 16, p[NK_detectDistance])) {
            w->vel_x += tdiv(o->vx * p[NK_blowAway], 100);
            w->vel_y += tdiv(o->vy * p[NK_blowAway], 100);
            do_damage(w, p[NK_hitDamage], o->owner);
            if (rndm(3) == 0) rndm(3);   // pain pick BEFORE blood (nobject order)
            int blood = p[NK_bloodOnHit];
            for (int k = 0; k < blood; ++k) {
                int angle = (int)rndm(128);
                nobj_create2(IDX_BLOOD, angle, tdiv(o->vx,3), tdiv(o->vy,3), o->x, o->y, 0, wi);
            }
            if (p[NK_wormExplode]) do_explode = 1;
            else if (p[NK_wormDestroy] && !freed) { nobj_free(i); freed = 1; }
        }
        }
        if (!freed && do_explode) nobj_explode(i, o->owner);
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

// per-viewport process: follow worm + shake (burns two rand calls while active)
static void process_viewport(Worm* w) {
    if (w->killed_timer <= 0) {
        if (w->visible) {
            w->vp_x = (w->pos_x >> 16) - (VW/2);
            w->vp_y = (w->pos_y >> 16) - (VH/2);
        } else {
            int dx = (w->pos_x >> 16) - (VW/2), dy = (w->pos_y >> 16) - (VH/2);
            for (int k = 0; k < 4; ++k) {
                if (w->vp_x < dx) ++w->vp_x; else if (w->vp_x > dx) --w->vp_x;
                if (w->vp_y < dy) ++w->vp_y; else if (w->vp_y > dy) --w->vp_y;
            }
        }
    } else if (w->health <= 0) {
        w->vp_x = (w->pos_x >> 16) - (VW/2);
        w->vp_y = (w->pos_y >> 16) - (VH/2);
    }
    int rs = w->shake >> 16;
    if (rs > 0) { w->vp_x += (int)rndm(rs*2) - rs; w->vp_y += (int)rndm(rs*2) - rs; }
    if (w->vp_x < 0) w->vp_x = 0;
    if (w->vp_x > LW - VW) w->vp_x = LW - VW;
    if (w->vp_y < 0) w->vp_y = 0;
    if (w->vp_y > LH - VH) w->vp_y = LH - VH;
}

#ifdef WEAPONS
static void init_weapons_worm(Worm* w) {
    for (int i = 0; i < WEAPONS_LIST_N; ++i) {
        w->wwp[i].ammo = c_wparam[weapons_list[i]][WK_ammo];
        w->wwp[i].delay_left = 0; w->wwp[i].loading_left = 0;
    }
    w->leave_shell_timer = 0; w->fire_cone = 0;
    w->wslot = 0; w->weapon = weapons_list[0];
}
#endif  // WEAPONS (init_weapons_worm)

// ---- death / respawn ----
static void worm_die(Worm* w) {
    w->leave_shell_timer = 0;
    rndm(3);   // death snd pick
    int old_lk = g_last_killed;
    g_last_killed = w->index;
    if (w->last_killed_by >= 0 && w->last_killed_by != w->index) worms[w->last_killed_by].kills++;
#ifdef AUTOTEST
    debugf("DIE f=%d w=%d lives=%d by=%d kl=%d,%d\n", g_frame, w->index, w->lives - 1, w->last_killed_by, old_lk, g_last_killed);
#endif
    w->visible = 0;
    w->killed_timer = 150;
    --w->lives;
    for (int k = 0; k < 120; ++k) {
        int angle = (int)rndm(128);
        nobj_create2(IDX_BLOOD, angle, tdiv(w->vel_x,3), tdiv(w->vel_y,3), w->pos_x, w->pos_y, 0, w->index);
    }
    for (int a = 7; a <= 105; a += 14)
        nobj_create2(0, a + (int)rndm(14), tdiv(w->vel_x,3), tdiv(w->vel_y,3), w->pos_x, w->pos_y, 0, w->index);
    w->prev_fire = 0;
}

static int check_respawn_pos(int ex, int ey, int oldx, int oldy, int x, int y) {
    int dy = oldy - y;
    if ((oldx < 0 ? -oldx : oldx) <= C_SPAWN_LAST && (dy < 0 ? -dy : dy) <= C_SPAWN_LAST) return 0;
    int edx = ex - x, edy = ey - y;
    if ((edx < 0 ? -edx : edx) <= C_SPAWN_ENEMY && (edy < 0 ? -edy : edy) <= C_SPAWN_ENEMY) return 0;
    int x1 = x - 3 < 0 ? 0 : x - 3;
    int y1 = y - 4 < 0 ? 0 : y - 4;
    int x2 = x + 3 > LW-1 ? LW-1 : x + 3;
    int y2 = y + 4 > LH-1 ? LH-1 : y + 4;
    for (int j = y1; j < y2; ++j)
        for (int k = x1; k < x2; ++k)
            if (rock_at(k, j)) return 0;
    return 1;
}

static void worm_begin_respawn(Worm* w) {
    int temp_x = w->pos_x >> 16, temp_y = w->pos_y >> 16;
    Worm* e = &worms[w->index ^ 1];
    int ex = e->pos_x >> 16, ey = e->pos_y >> 16;
    int sx2 = temp_x, sy2 = temp_y, trials = 0;
    while (1) {
        sx2 = C_SPAWN_X + (int)rndm(C_SPAWN_W);
        sy2 = C_SPAWN_Y + (int)rndm(C_SPAWN_H);
        while (sy2 + 4 < LH && back_at(sx2, sy2 + 4)) ++sy2;
        if (++trials >= 50000) break;
        if (check_respawn_pos(ex, ey, temp_x, temp_y, sx2, sy2)) break;
    }
    w->pos_x = sx2 << 16; w->pos_y = sy2 << 16;
    w->lr_x = temp_x - 80; w->lr_y = temp_y - 80;
    w->killed_timer = -1;
}

static void worm_dead_process(Worm* w, uint8_t fire_held) {
    if (fire_held) w->ready = 1;
    if (w->killed_timer > 0) --w->killed_timer;
    if (w->killed_timer == 0) worm_begin_respawn(w);
    if (w->killed_timer < 0) {
        int dest_x = (w->pos_x >> 16) - 80;
        int dest_y = (w->pos_y >> 16) - 80;
        for (int k = 0; k < 4; ++k) {
            if (w->lr_x < dest_x) ++w->lr_x;
            else if (w->lr_x > dest_x) --w->lr_x;
            if (w->lr_y < dest_y) ++w->lr_y;
            else if (w->lr_y > dest_y) --w->lr_y;
        }
        if (w->lr_x < 0) w->lr_x = 0; else if (w->lr_x > LW-VW) w->lr_x = LW-VW;
        if (w->lr_y < 0) w->lr_y = 0; else if (w->lr_y > LH-VH) w->lr_y = LH-VH;
        if (dest_x < 0) dest_x = 0; else if (dest_x > LW-VW) dest_x = LW-VW;
        if (dest_y < 0) dest_y = 0; else if (dest_y > LH-VH) dest_y = LH-VH;
        if (dest_x-5 < w->lr_x && w->lr_x < dest_x+5 && dest_y-5 < w->lr_y && w->lr_y < dest_y+5 && w->ready) {
            int ipx = w->pos_x >> 16, ipy = w->pos_y >> 16;
            dirt_effect(0, ipx - 7, ipy - 7);
            correct_shadow(ipx - 10, ipy - 10, ipx + 11, ipy + 11);
            w->ready = 0; w->visible = 1;
            w->vel_x = 0; w->vel_y = 0;
            w->health = 100;
#ifdef AUTOTEST
            debugf("RSP f=%d w=%d x=%d y=%d\n", g_frame, w->index, ipx, ipy);
#endif
            if (rng_next() & 1) { w->aim_angle = 32 << 16; w->direction = 0; }
            else { w->aim_angle = 96 << 16; w->direction = 1; }
        }
    }
}

// generic wobject/nobject sprite draw (upstream viewport rule: small sprite if startFrame>0, else pixel)
#ifndef NORENDER
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
#endif  // !NORENDER (draw_wobjects)
#endif  // WEAPONS (combat pools + processing)
// wobj/nobj state hash (debug channel)
#ifdef WEAPONS
static uint32_t g_obj_acc = 2166136261u;
// full pools hash: matches host mirror byte layout (le int32 packed)
static void worm_hash_weap(uint32_t *oh_out, int *wc, int *nc, int *sc, int *bc) {
    uint32_t oh = 2166136261u;
    int w=0,n2=0,s2=0;
    uint8_t ab[40];
    for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used) {
        w++;
        int32_t v[10] = {wobj[j].x, wobj[j].y, wobj[j].vx, wobj[j].vy,
                        wobj[j].cur_frame, wobj[j].time_left, wobj[j].owner, wobj[j].type,
                        wobj[j].has_hit, 0};
        for (int k = 0; k < 10; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 40);
    }
    for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) {
        n2++;
        int32_t v[8] = {nobj[j].x, nobj[j].y, nobj[j].vx, nobj[j].vy,
                        nobj[j].cur_frame, nobj[j].time_left, nobj[j].type, nobj[j].owner};
        for (int k = 0; k < 8; ++k) {
            ab[k*4+0]=(uint8_t)(v[k]); ab[k*4+1]=(uint8_t)(v[k]>>8);
            ab[k*4+2]=(uint8_t)(v[k]>>16); ab[k*4+3]=(uint8_t)(v[k]>>24);
        }
        oh = fnv2(oh, ab, 32);
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
#endif

    // spawn: two worms on chamber floors with ground contact
    {
        int sx = 115, sy = 175;
        for (int y = 156; y < 180; ++y)
            if (reaction_count(sx, y, RF_UP) >= 1) { sy = y; break; }
        worm_init(&worms[0], 0, sx, sy, 0);
#ifdef WEAPONS
        init_weapons_worm(&worms[0]);
#endif
        debugf("SPAWN i=0 x=%d y=%d\n", sx, sy);
        int sx2 = 150, sy2 = 175;
        for (int y = 156; y < 180; ++y)
            if (reaction_count(sx2, y, RF_UP) >= 1) { sy2 = y; break; }
        worm_init(&worms[1], 1, sx2, sy2, 1);
#ifdef WEAPONS
        init_weapons_worm(&worms[1]);
#endif
        debugf("SPAWN i=1 x=%d y=%d\n", sx2, sy2);
    }

    int camx = 0;
    uint64_t frame = 0;
    while (1) {
        joypad_poll();
        joypad_inputs_t jin = joypad_get_inputs(JOYPAD_PORT_1);
        (void)jin;
        uint64_t cycle = frame + 1;
#ifdef WEAPONS
        g_frame = (int)frame;
        // AI phase (upstream localController: order alternates by cycles%2)
        {
            Worm* order[N_WORMS];
            if ((int)(frame % 2) == 0) { order[0] = &worms[0]; order[1] = &worms[1]; }
            else { order[0] = &worms[1]; order[1] = &worms[0]; }
            for (int i = 0; i < N_WORMS; ++i)
                if (worms_ai_on[order[i]->index]) ai_process(order[i]);
        }
        // viewport shake decay (upstream processFrame top)
        for (int vi = 0; vi < N_WORMS; ++vi)
            if (worms[vi].shake > 0) worms[vi].shake -= 4000;
        process_sobjects(cycle);
        process_wobjects(cycle);
        process_nobjects(cycle);
        process_bobjects();
#endif
#ifndef WEAPONS
        (void)cycle;
#endif
#ifdef WORM
        WormInput win = {0,0,0,0,0,0,0};
#ifdef AUTOTEST
        int ph = (int)(frame % 420);
        if (ph < 120) win.right = 1;
        else if (ph == 120) win.jump = 1;
        else if (ph >= 210 && ph < 330) win.left = 1;
        else if (ph >= 330) win.up = 1;
#ifdef WEAPONS
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
#ifdef WEAPONS
        // per-worm inputs: AI worms use ai_state
        WormInput in0 = win, in1 = win;
        if (worms_ai_on[0]) in0 = worms[0].ai;
        if (worms_ai_on[1]) in1 = worms[1].ai;
        worm_process(&worms[0], in0, frame);
        worm_process(&worms[1], in1, frame);
        // viewport follow + shake rand (upstream: after worms)
        for (int vi = 0; vi < N_WORMS; ++vi) process_viewport(&worms[vi]);
#ifdef WEAPONS_AUTOTEST_DUMP
        if ((frame % 5) == 2 || (frame >= 175 && frame <= 190)) {
            for (int j = 0; j < MAX_WOBJ; ++j) if (wobj[j].used)
                debugf("WT f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld t=%d ty=%d ow=%d\n",
                    (int)frame, j,
                    (long)wobj[j].x, (long)wobj[j].y,
                    (long)wobj[j].vx, (long)wobj[j].vy, wobj[j].time_left, wobj[j].type, wobj[j].owner);
        }
        if ((frame % 5) == 1) {
            int cnt = 0;
            for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) ++cnt;
            debugf("NN f=%d n=%d\n", (int)frame, cnt);
            cnt = 0;
            for (int j = 0; j < MAX_NOBJ; ++j) if (nobj[j].used) {
                if (cnt++ >= 16) break;
                debugf("NT f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld t=%d ty=%d ow=%d\n",
                    (int)frame, j,
                    (long)nobj[j].x, (long)nobj[j].y,
                    (long)nobj[j].vx, (long)nobj[j].vy, nobj[j].time_left, nobj[j].type, nobj[j].owner);
            }
        }
        if ((frame % 5) == 3) {
            for (int j = 0; j < MAX_SOBJ; ++j) if (sobj[j].used)
                debugf("ST f=%d i=%d x=%d y=%d cur=%d ad=%d ty=%d\n",
                    (int)frame, j, sobj[j].x, sobj[j].y, sobj[j].cur, sobj[j].ad, sobj[j].type);
        }
        if ((frame % 5) == 4) {
            for (int j = 0; j < bobj_count && j < 12; ++j)
                debugf("BO f=%d i=%d x=%ld y=%ld vx=%ld vy=%ld co=%d\n",
                    (int)frame, j,
                    (long)bobj[j].x, (long)bobj[j].y,
                    (long)bobj[j].vx, (long)bobj[j].vy, bobj[j].color);
        }
        // AI control trace
        debugf("AI f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d aim=%d w=%d\n",
            (int)frame, worms[1].ai.fire, worms[1].ai.jump, worms[1].ai.change,
            worms[1].ai.left, worms[1].ai.right, worms[1].ai.up, worms[1].ai.down,
            (int)(worms[1].aim_angle >> 16), worms[1].weapon);
        if (worms_ai_on[0])
            debugf("A0 f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d\n",
                (int)frame, worms[0].ai.fire, worms[0].ai.jump, worms[0].ai.change,
                worms[0].ai.left, worms[0].ai.right, worms[0].ai.up, worms[0].ai.down);
        else
            debugf("A0 f=%d fi=%d j=%d c=%d l=%d r=%d u=%d d=%d\n",
                (int)frame, win.fire, win.jump, win.change, win.left, win.right, win.up, win.down);
#endif
#else
        worm_process(&worms[0], win, frame);
#endif
#endif  // WORM

        surface_t* fb = display_get();
        rdpq_attach(fb, NULL);
#ifndef NORENDER
#ifdef WKILLHOLD
        if ((frame % 8) == 7) {
#endif
#ifdef WORM
            render_view(worms[0].vp_x, worms[0].vp_y);
            draw_worm(&worms[0], worms[0].vp_x, worms[0].vp_y);
            draw_worm(&worms[1], worms[0].vp_x, worms[0].vp_y);
#else
            render_view(camx, 0);
#endif
#ifdef WEAPONS
            draw_wobjects(worms[0].vp_x, worms[0].vp_y);
#endif
#ifdef WKILLHOLD
        }
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

#ifdef WEAPONS_AUTOTEST_DUMP
        debugf("RG f=%d %08lx,%08lx\n", (int)frame, (unsigned long)rng_x, (unsigned long)rng_c);
        {
            Worm* m = &worms[1];
            debugf("CM f=%d p=%ld,%ld v=%ld,%ld a=%ld as=%ld dir=%d rv=%d,%d,%d,%d\n",
                (int)frame, (long)(m->pos_x >> 16), (long)(m->pos_y >> 16),
                (long)m->vel_x, (long)m->vel_y,
                (long)(m->aim_angle >> 16), (long)(m->aim_speed >> 16), m->direction,
                m->reacts[0], m->reacts[1], m->reacts[2], m->reacts[3]);
        }
#endif
        // full-state checkpoints: terrain + rng + both worms + object pools
        {
            int ck = 0;
#ifdef AUTOTEST
            int ph2 = (int)(frame % 420);
            if (ph2 == 112 || ph2 == 137 || ph2 == 162 || ph2 == 180 ||
                ph2 == 262 || ph2 == 287 || ph2 == 312 || ph2 == 337 ||
                ph2 == 362 || ph2 == 387 || ph2 == 412 ||
                ph2 == 37 || ph2 == 62 || ph2 == 87 ||
                ph2 == 612 || ph2 == 637 || ph2 == 662 || ph2 == 712 ||
                ph2 == 762 || ph2 == 812 || ph2 == 862 || ph2 == 912 ||
                ph2 == 962 || ph2 == 1012 || ph2 == 1062 || ph2 == 1112 ||
                ph2 == 1162 || ph2 == 1212) ck = 1;
#else
            if ((frame % 60) == 5) ck = 1;
#endif
            if ((frame % 60) == 59 || frame == FRAMES_AUTO - 1) ck = 1;
            if (ck) {
                uint32_t th = fnv(level_data, LW*LH);
#ifdef WEAPONS
                debugf("CK frame=%d terr=%08lx rng=%08lx,%08lx wc=%d nc=%d sc=%d bc=%d obj=%08lx kl=%d vp=%d,%d|%d,%d shk=%d,%d%s\n",
                    (int)frame, (unsigned long)th,
                    (unsigned long)rng_x, (unsigned long)rng_c, wc, nc, sc, bc,
                    (unsigned long)oh_out, g_last_killed,
                    worms[0].vp_x, worms[0].vp_y, worms[1].vp_x, worms[1].vp_y,
                    worms[0].shake, worms[1].shake, "");
                for (int wi = 0; wi < N_WORMS; ++wi) {
                    Worm* w = &worms[wi];
                    debugf("CW %d p=%ld,%ld v=%ld,%ld hp=%d vis=%d kt=%d lr=%d,%d aim=%ld dir=%d cur=%d w=%d wslot=%d am=%d dly=%d ld=%d lv=%d k=%d sf=%d\n",
                        wi, (long)(w->pos_x >> 16), (long)(w->pos_y >> 16),
                        (long)w->vel_x, (long)w->vel_y, w->health, w->visible,
                        w->killed_timer, w->lr_x, w->lr_y,
                        (long)(w->aim_angle >> 16), w->direction, w->current_frame,
                        w->weapon, w->wslot, w->wwp[w->wslot].ammo,
                        w->wwp[w->wslot].delay_left, w->wwp[w->wslot].loading_left,
                        w->lives, w->kills, w->leave_shell_timer);
                }
#else
                debugf("CK frame=%d terr=%08lx rng=%08lx,%08lx wc=0 nc=0 sc=0 bc=0 obj=0 kl=-1 vp=0,0|0,0 shk=0,0\n",
                    (int)frame, (unsigned long)th,
                    (unsigned long)rng_x, (unsigned long)rng_c);
#endif
            }
        }
        if (worms[0].lives <= 0 || worms[1].lives <= 0) {
            debugf("OVR f=%d winner=%d lives=%d,%d\n", (int)frame,
                worms[0].lives <= 0 ? 1 : 0, worms[0].lives, worms[1].lives);
            break;
        }
        ++frame;
    }
}
