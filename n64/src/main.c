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
static void render_view(int camx, int camy) {
    static uint8_t runs_v[VW+1];
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
            (void)runs_v;
        }
    }
}

static uint32_t fnv(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

int main(void) {
    debug_init_emulog();
    rdpq_init();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    joypad_init();

    generate_random();

    uint32_t hash = fnv(level_data, sizeof(level_data));
    debugf("GEN hash=%08lx\n", (unsigned long)hash);

    int camx = 0;
    uint64_t frame = 0;
    while (1) {
        surface_t* fb = display_get();
        rdpq_attach(fb, NULL);
        render_view(camx, 0);
        rdpq_detach();
        display_show(fb);

        joypad_poll();
#ifdef AUTOTEST
        // Position-hold ping-pong, robust to frame-count drift in headless
        // capture: hold each camera x for 200 frames.
        int f = (int)frame;
        int pos = (f / 200) % 8;
        camx = pos * 26;
        if (camx > LW - VW) camx = LW - VW;
#else
        if (jin.btn.d_right) camx += 12;
        if (jin.btn.d_left)  camx -= 12;
        if (camx < 0) camx = 0;
        if (camx > LW - VW) camx = LW - VW;
#endif

        if ((frame % 60) == 0)
            debugf("F frame=%llu cam=%d\n", (unsigned long long)frame, camx);
        ++frame;
    }
}
