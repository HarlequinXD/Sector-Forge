/* Sector Forge - procedural texture library (tileable 128x128) and sprites */
#include "texgen.h"
#include "map.h"

#define N TEXGEN_SIZE

typedef struct { float r, g, b; } C3;
static C3 c3(float r, float g, float b) { C3 c = { r, g, b }; return c; }
static C3 hx(uint32_t h) { return c3(((h >> 16) & 255) / 255.f, ((h >> 8) & 255) / 255.f, (h & 255) / 255.f); }
static C3 mix(C3 a, C3 b, float t) { return c3(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t); }
static C3 mul(C3 a, float s) { return c3(a.r * s, a.g * s, a.b * s); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float smooth(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

static void put(uint8_t *img, int w, int x, int y, C3 c) {
    uint8_t *p = img + ((size_t)y * w + x) * 4;
    p[0] = (uint8_t)(clampf(c.r, 0, 1) * 255 + 0.5f);
    p[1] = (uint8_t)(clampf(c.g, 0, 1) * 255 + 0.5f);
    p[2] = (uint8_t)(clampf(c.b, 0, 1) * 255 + 0.5f);
    p[3] = 255;
}

/* ---- tileable value noise ---- */
static float lat(int x, int y, int period, uint32_t seed) {
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777215.0f;
}
static float vnoise(float x, float y, int period, uint32_t seed) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = lat(xi, yi, period, seed), b = lat(xi + 1, yi, period, seed);
    float c = lat(xi, yi + 1, period, seed), d = lat(xi + 1, yi + 1, period, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
/* u,v in [0,1); base frequency f (integer) for tiling */
static float fbm(float u, float v, int f, int oct, uint32_t seed) {
    float s = 0, amp = 0.5f, tot = 0;
    for (int o = 0; o < oct; o++) {
        s += vnoise(u * f, v * f, f, seed + o * 17) * amp;
        tot += amp;
        f *= 2; amp *= 0.5f;
    }
    return s / tot;
}
/* tileable worley: returns F1 distance and cell id */
static float worley(float u, float v, int cells, uint32_t seed, float *f2out, uint32_t *idout) {
    float x = u * cells, y = v * cells;
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float f1 = 9, f2 = 9;
    uint32_t id = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int cx = xi + i, cy = yi + j;
            float px = cx + lat(cx, cy, cells, seed), py = cy + lat(cx, cy, cells, seed + 99);
            float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
            if (d < f1) { f2 = f1; f1 = d; id = (uint32_t)(((cx % cells) + cells) % cells) * 131u + (uint32_t)(((cy % cells) + cells) % cells); }
            else if (d < f2) f2 = d;
        }
    if (f2out) *f2out = f2;
    if (idout) *idout = id;
    return f1;
}
static float hash01(uint32_t a, uint32_t seed) { return lat((int)a, (int)(a >> 7), 1 << 20, seed); }

/* ---- generators ---- */
typedef struct {
    const char *name;
    int cat;
    int kind;
    uint32_t c1, c2, c3;
    int p1, p2;
    uint32_t seed;
} TexDef;

enum { K_BRICK, K_BLOCKS, K_COBBLE, K_METAL, K_GRATE, K_TECH, K_SCREEN, K_PLANKS, K_WOODPANEL, K_MARBLE, K_NOISE,
       K_TILES, K_BEAM, K_DOOR, K_CRATE, K_FLESH, K_CEILPANEL, K_LIGHTPANEL, K_GRASS, K_LIQUID, K_LAVA, K_SKY };

static const TexDef g_defs[] = {
    { "BRICK_RED",    TEXCAT_WALL, K_BRICK, 0x7A3426, 0x2B160F, 0x96503A, 32, 16, 1 },
    { "BRICK_GREY",   TEXCAT_WALL, K_BRICK, 0x6B6D72, 0x2A2B2E, 0x83858A, 32, 16, 2 },
    { "BRICK_BROWN",  TEXCAT_WALL, K_BRICK, 0x6E5034, 0x2C2015, 0x8A6844, 42, 14, 3 },
    { "BRICK_DARK",   TEXCAT_WALL, K_BRICK, 0x3A2E2C, 0x161112, 0x4E3E3A, 32, 16, 4 },
    { "STONE_BLOCKS", TEXCAT_WALL, K_BLOCKS, 0x6A6A66, 0x302F2D, 0x82817C, 64, 32, 5 },
    { "CASTLE_WALL",  TEXCAT_WALL, K_BLOCKS, 0x7D725F, 0x3A3329, 0x958A74, 43, 32, 6 },
    { "STONE_COBBLE", TEXCAT_WALL, K_COBBLE, 0x6C6A66, 0x2D2C2A, 0x8A8781, 6, 0, 7 },
    { "STONE_MOSSY",  TEXCAT_WALL, K_COBBLE, 0x5F6656, 0x262B22, 0x4F7A3A, 6, 1, 8 },
    { "METAL_PANEL",  TEXCAT_WALL, K_METAL, 0x5B6470, 0x2E343C, 0x8B95A3, 64, 0, 9 },
    { "METAL_PLATE",  TEXCAT_WALL, K_METAL, 0x6F7378, 0x34373B, 0x9DA2A8, 32, 1, 10 },
    { "RUST_PANEL",   TEXCAT_WALL, K_METAL, 0x6A3B20, 0x2E1A0F, 0x8C5A30, 64, 2, 11 },
    { "METAL_GRATE",  TEXCAT_WALL, K_GRATE, 0x5A6068, 0x16181B, 0x7E8792, 16, 0, 12 },
    { "TECH_WALL",    TEXCAT_WALL, K_TECH, 0x2A3038, 0x14181D, 0xFF7A1A, 0, 0, 13 },
    { "TECH_BLUE",    TEXCAT_WALL, K_TECH, 0x26313D, 0x10161C, 0x33B4E8, 1, 0, 14 },
    { "COMP_SCREEN",  TEXCAT_WALL, K_SCREEN, 0x2B3138, 0x0A120D, 0x3DDC84, 0, 0, 15 },
    { "WOOD_PLANKS",  TEXCAT_WALL, K_PLANKS, 0x6B4423, 0x3A220F, 0x8A5C33, 16, 0, 16 },
    { "WOOD_PANEL",   TEXCAT_WALL, K_WOODPANEL, 0x5E3B1E, 0x2E1B0C, 0x7C522C, 0, 0, 17 },
    { "MARBLE_WHITE", TEXCAT_WALL, K_MARBLE, 0xD2CDC4, 0x8F877C, 0xEDE9E2, 0, 0, 18 },
    { "MARBLE_GREEN", TEXCAT_WALL, K_MARBLE, 0x2E5A48, 0x12261D, 0x6FA58C, 0, 0, 19 },
    { "CONCRETE",     TEXCAT_WALL, K_NOISE, 0x6B6E72, 0x4E5155, 0x80838A, 4, 0, 20 },
    { "PLASTER",      TEXCAT_WALL, K_NOISE, 0xB8AE9C, 0x9C927F, 0xC9C0AE, 2, 1, 21 },
    { "TILE_WHITE",   TEXCAT_WALL, K_TILES, 0xC9CED3, 0x6F777F, 0xE2E6EA, 32, 0, 22 },
    { "SUPPORT_BEAM", TEXCAT_WALL, K_BEAM, 0x585E66, 0x24282D, 0xC9A227, 0, 0, 23 },
    { "DOOR_METAL",   TEXCAT_WALL, K_DOOR, 0x5A616B, 0x23272C, 0xFFB02E, 0, 0, 24 },
    { "DOOR_WOOD",    TEXCAT_WALL, K_DOOR, 0x6B4423, 0x2E1B0C, 0x8B8F96, 1, 0, 25 },
    { "CRATE",        TEXCAT_WALL, K_CRATE, 0x8A6236, 0x3E2A14, 0xA47A48, 0, 0, 26 },
    { "HELL_FLESH",   TEXCAT_WALL, K_FLESH, 0x7A1E16, 0x2A0806, 0xB8453A, 0, 0, 27 },
    { "FLOOR_STONE",  TEXCAT_FLAT, K_BLOCKS, 0x5A5E63, 0x2F3236, 0x6E7378, 32, 32, 30 },
    { "FLOOR_TILE",   TEXCAT_FLAT, K_TILES, 0x3D4F5C, 0x1E272E, 0x56707F, 16, 1, 31 },
    { "FLOOR_METAL",  TEXCAT_FLAT, K_GRATE, 0x5E6670, 0x22262B, 0x828C98, 8, 1, 32 },
    { "FLOOR_WOOD",   TEXCAT_FLAT, K_PLANKS, 0x7A5030, 0x3C2614, 0x94663D, 16, 1, 33 },
    { "FLOOR_DIRT",   TEXCAT_FLAT, K_NOISE, 0x5A4330, 0x3A2A1C, 0x725A44, 8, 2, 34 },
    { "FLOOR_SAND",   TEXCAT_FLAT, K_NOISE, 0xB59A6A, 0x8E7650, 0xCDB585, 8, 3, 35 },
    { "FLOOR_GRASS",  TEXCAT_FLAT, K_GRASS, 0x355C25, 0x1D3513, 0x5E8F3A, 0, 0, 36 },
    { "CARPET_RED",   TEXCAT_FLAT, K_NOISE, 0x6E1616, 0x4A0C0C, 0x8A2222, 16, 4, 37 },
    { "CEIL_TECH",    TEXCAT_FLAT, K_CEILPANEL, 0x1D232A, 0x0F1317, 0x33B4E8, 0, 0, 38 },
    { "CEIL_LIGHT",   TEXCAT_FLAT, K_LIGHTPANEL, 0x2A2F35, 0x15181C, 0xFFF4D6, 0, 0, 39 },
    { "CEIL_STONE",   TEXCAT_FLAT, K_COBBLE, 0x4A4846, 0x1E1D1C, 0x5E5C59, 4, 0, 40 },
    { "WATER",        TEXCAT_LIQUID, K_LIQUID, 0x123B5C, 0x0A2238, 0x3C86BA, 0, 0, 41 },
    { "SLIME",        TEXCAT_LIQUID, K_LIQUID, 0x2C6B16, 0x173A0B, 0x8BE03A, 1, 0, 42 },
    { "LAVA",         TEXCAT_LIQUID, K_LAVA, 0x8F1D07, 0x3A0802, 0xFFB02E, 0, 0, 43 },
    { "F_SKY",        TEXCAT_SKY, K_SKY, 0x1B2A44, 0x0B1222, 0xF29A5C, 0, 0, 44 },
};

int texgen_count(void) { return SF_COUNT(g_defs); }
const char *texgen_name(int i) { return g_defs[i].name; }
int texgen_category(int i) { return g_defs[i].cat; }

static void gen_brick(const TexDef *d, uint8_t *img) {
    int bw = d->p1, bh = d->p2;
    C3 base = hx(d->c1), mortar = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int row = y / bh;
        int ox = (row & 1) ? bw / 2 : 0;
        int bx = (x + ox) % N, col = bx / bw;
        int lx = bx % bw, ly = y % bh;
        uint32_t id = (uint32_t)(row * 37 + col);
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, 8, 4, d->seed);
        float edge = SF_MIN(SF_MIN(lx, bw - 1 - lx), SF_MIN(ly, bh - 1 - ly));
        C3 c = mix(base, hi, hash01(id, d->seed) * 0.6f);
        c = mul(c, 0.8f + n * 0.45f);
        if (ly == 2 || lx == 2) c = mul(c, 1.12f);
        if (ly == bh - 3) c = mul(c, 0.85f);
        float m = edge < 1.5f ? 1.f : 0.f;
        c = mix(c, mul(mortar, 0.8f + n * 0.4f), m);
        put(img, N, x, y, c);
    }
}

static void gen_blocks(const TexDef *d, uint8_t *img) {
    int bw = d->p1, bh = d->p2;
    C3 base = hx(d->c1), mortar = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int row = y / bh, ox = (row & 1) ? bw / 2 : 0;
        int bx = (x + ox) % N, col = bx / bw, lx = bx % bw, ly = y % bh;
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, 4, 5, d->seed), n2 = fbm(u, v, 16, 3, d->seed + 5);
        uint32_t id = (uint32_t)(row * 41 + col);
        C3 c = mix(base, hi, hash01(id, d->seed) * 0.7f);
        c = mul(c, 0.7f + n * 0.5f + n2 * 0.15f);
        float e = (float)SF_MIN(SF_MIN(lx, bw - 1 - lx), SF_MIN(ly, bh - 1 - ly));
        if (e < 3) c = mul(c, (lx < 3 || ly < 3) ? 1.15f : 0.72f);
        if (e < 1.2f) c = mul(mortar, 0.8f + n * 0.4f);
        put(img, N, x, y, c);
    }
}

static void gen_cobble(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), mortar = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, f2;
        uint32_t id;
        float f1 = worley(u, v, d->p1, d->seed, &f2, &id);
        float edge = f2 - f1;
        float n = fbm(u, v, 8, 4, d->seed);
        C3 c = mul(mix(base, hi, d->p2 ? 0 : hash01(id, d->seed) * 0.5f), 0.65f + n * 0.5f);
        c = mul(c, 1.15f - f1 * 0.45f);
        if (d->p2) { float moss = smooth(0.45f, 0.7f, fbm(u, v, 4, 4, d->seed + 3)); c = mix(c, mul(hi, 0.7f + n * 0.5f), moss * 0.8f); }
        float m = smooth(0.06f, 0.14f, edge);
        c = mix(mul(mortar, 0.8f + n * 0.4f), c, m);
        put(img, N, x, y, c);
    }
}

static void gen_metal(const TexDef *d, uint8_t *img) {
    int ps = d->p1;
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, 8, 4, d->seed), scratch = fbm(u * 0.25f, v * 4, 32, 2, d->seed + 1);
        int lx = x % ps, ly = y % ps;
        C3 c = mul(base, 0.85f + n * 0.25f + scratch * 0.1f);
        if (d->p2 == 2) { float r = smooth(0.4f, 0.75f, fbm(u, v, 4, 5, d->seed + 7)); c = mix(c, mul(hi, 0.6f + n * 0.6f), r); }
        if (d->p2 == 1) { if (((x / 4) + (y / 4)) % 2 == 0 && lx > 4 && ly > 4 && lx < ps - 4 && ly < ps - 4) c = mul(c, 1.06f); }
        if (lx == 0 || ly == 0) c = mul(dark, 1.0f);
        else if (lx == 1 || ly == 1) c = mul(hi, 0.9f);
        else if (lx == ps - 1 || ly == ps - 1) c = mul(c, 0.7f);
        int rx = lx < ps / 2 ? 5 : ps - 6, ry = ly < ps / 2 ? 5 : ps - 6;
        float dr = sqrtf((float)((lx - rx) * (lx - rx) + (ly - ry) * (ly - ry)));
        if (dr < 2.2f) c = mix(hi, dark, smooth(0.0f, 2.2f, dr) * (lx > rx ? 1 : 0.4f));
        put(img, N, x, y, c);
    }
}

static void gen_grate(const TexDef *d, uint8_t *img) {
    int s = d->p1;
    C3 base = hx(d->c1), hole = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, 8, 3, d->seed);
        int lx = x % s, ly = y % s;
        C3 c = mul(base, 0.8f + n * 0.35f);
        if (d->p2) { /* diamond plate */
            int a = (x + y) % 16, b = (x - y + 128) % 16;
            if ((a < 3 && (y / 8) % 2 == 0) || (b < 3 && (y / 8) % 2 == 1)) c = mul(hi, 0.85f + n * 0.3f);
            if (x % 64 == 0 || y % 64 == 0) c = mul(hole, 1.2f);
        } else {
            bool bar = lx < 3 || ly < 3;
            if (!bar) c = mul(hole, 0.9f + n * 0.2f);
            else if (lx == 0 || ly == 0) c = mul(hi, 0.9f);
        }
        put(img, N, x, y, c);
    }
}

static void gen_tech(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), glow = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, n = fbm(u, v, 8, 3, d->seed);
        C3 c = mul(base, 0.85f + n * 0.3f);
        int lx = x % 64, ly = y % 32;
        if (lx < 2 || ly < 2) c = dark;
        if (lx == 2 || ly == 2) c = mul(base, 1.35f);
        if (ly > 12 && ly < 19 && lx > 10 && lx < 54) {
            c = mul(dark, 0.8f);
            if (ly > 14 && ly < 17 && (d->p1 ? (lx % 8 < 6) : 1)) c = mix(glow, c3(1, 1, 1), (ly == 15) ? 0.35f : 0.f);
        }
        if (((lx == 6 || lx == 57) && (ly == 6 || ly == 26))) c = mul(base, 1.6f);
        put(img, N, x, y, c);
    }
}

static void gen_screen(const TexDef *d, uint8_t *img) {
    C3 frame = hx(d->c1), scr = hx(d->c2), txt = hx(d->c3);
    Rng r = { d->seed * 7919 };
    uint8_t rowlen[16];
    for (int i = 0; i < 16; i++) rowlen[i] = (uint8_t)(20 + rng_u32(&r) % 70);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float n = fbm(x / (float)N, y / (float)N, 8, 3, d->seed);
        C3 c = mul(frame, 0.85f + n * 0.3f);
        bool in = x >= 12 && x < 116 && y >= 16 && y < 100;
        if (in) {
            c = scr;
            int ry = (y - 20) / 6, ly = (y - 20) % 6;
            if (y >= 20 && ry < 13 && ly < 3 && x >= 18 && x < 18 + rowlen[ry] && ((x / 3) % 5 != 4)) c = mix(scr, txt, 0.9f);
            if (y % 2 == 0) c = mul(c, 0.85f);
        }
        if ((x == 11 || x == 116) && y >= 15 && y <= 100) c = mul(frame, 1.5f);
        if ((y == 15 || y == 100) && x >= 11 && x <= 116) c = mul(frame, 1.5f);
        if (y > 106 && y < 118 && x > 16 && x < 112) {
            int k = (x - 16) / 12;
            c = (x - 16) % 12 < 9 ? (k == 2 ? hx(0xFF4D4D) : k == 5 ? hx(0xFFD23D) : mul(frame, 1.25f)) : mul(frame, 0.7f);
        }
        put(img, N, x, y, c);
    }
}

static void gen_planks(const TexDef *d, uint8_t *img) {
    int pw = d->p1;
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int a = d->p2 ? y : x, b = d->p2 ? x : y;
        int plank = a / pw, la = a % pw;
        float u = a / (float)N, v = b / (float)N;
        float off = hash01((uint32_t)plank, d->seed);
        float grain = fbm(u * 1.0f + off, v * 0.125f, 16, 3, d->seed + (uint32_t)plank);
        float rings = 0.5f + 0.5f * sinf((u * 40 + grain * 10) * 3.14159f);
        C3 c = mix(base, hi, off * 0.5f);
        c = mix(c, dark, rings * 0.35f);
        c = mul(c, 0.85f + grain * 0.3f);
        if (la == 0) c = mul(dark, 0.7f);
        if (la == 1) c = mul(c, 1.1f);
        int seg = (int)((b + plank * 37) % N);
        if (seg == 0) c = mul(dark, 0.8f);
        if ((seg == 4 || seg == 66) && (la == 3 || la == pw - 4)) c = hx(0x2A2A2A);
        put(img, N, x, y, c);
    }
}

static void gen_woodpanel(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float grain = fbm(u * 0.25f, v * 2, 16, 3, d->seed);
        C3 c = mul(mix(base, hi, grain), 0.85f + grain * 0.25f);
        int lx = x % 64, ly = y % 64;
        int e = SF_MIN(SF_MIN(lx, 63 - lx), SF_MIN(ly, 63 - ly));
        if (e < 6) c = mul(c, 0.75f);
        if (e == 6) c = mul(hi, 1.1f);
        if (e == 0) c = dark;
        put(img, N, x, y, c);
    }
}

static void gen_marble(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), vein = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float t = fbm(u, v, 4, 6, d->seed);
        float s = fabsf(sinf((u + v) * 2 * 3.14159f + t * 9));
        C3 c = mix(hi, base, smooth(0.0f, 0.6f, s));
        c = mix(vein, c, smooth(0.0f, 0.08f, s));
        if (x % 64 == 0 || y % 64 == 0) c = mul(c, 0.8f);
        put(img, N, x, y, c);
    }
}

static void gen_noise(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), lo = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, d->p1, 6, d->seed);
        float fine = lat(x, y, N, d->seed + 3);
        C3 c = n < 0.5f ? mix(lo, base, n * 2) : mix(base, hi, (n - 0.5f) * 2);
        c = mul(c, 0.92f + fine * 0.16f);
        if (d->p2 == 2 && fine > 0.97f) c = mul(c, 0.6f);        /* pebbles */
        if (d->p2 == 4 && ((x + y) % 8 == 0 || (x - y + 128) % 8 == 0)) c = mul(c, 1.1f); /* carpet weave */
        if (d->p2 == 1 && fine > 0.995f) c = mul(c, 0.7f);
        put(img, N, x, y, c);
    }
}

static void gen_tiles(const TexDef *d, uint8_t *img) {
    int s = d->p1;
    C3 base = hx(d->c1), grout = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, n = fbm(u, v, 8, 3, d->seed);
        int lx = x % s, ly = y % s;
        uint32_t id = (uint32_t)((y / s) * 31 + x / s);
        C3 c = mul(mix(base, hi, hash01(id, d->seed) * 0.35f), 0.9f + n * 0.2f);
        if (d->p2 && ((x / s) + (y / s)) % 2) c = mul(c, 0.82f);
        if (lx == 1 || ly == 1) c = mul(c, 1.12f);
        if (lx == 0 || ly == 0) c = grout;
        put(img, N, x, y, c);
    }
}

static void gen_beam(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), stripe = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float n = fbm(x / (float)N, y / (float)N, 8, 3, d->seed);
        float shade = 0.75f + 0.35f * sinf(x / (float)N * 3.14159f);
        C3 c = mul(base, shade * (0.85f + n * 0.25f));
        if (x < 6 || x > N - 7) c = mul(dark, 1.1f);
        if (x == 6 || x == N - 7) c = mul(base, 1.4f);
        if ((y % 64) > 52) c = ((x + y) / 8) % 2 ? mul(stripe, 0.9f + n * 0.2f) : mul(dark, 0.8f);
        int ly = y % 32;
        if ((x == 16 || x == N - 17) && ly > 12 && ly < 17) c = mul(base, 1.7f);
        put(img, N, x, y, c);
    }
}

static void gen_door(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), acc = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, n = fbm(u, v, 8, 4, d->seed);
        C3 c;
        if (d->p1) { /* wood door with iron bands */
            float grain = fbm(u * 0.25f, v * 2, 16, 3, d->seed);
            c = mul(mix(base, mul(base, 1.3f), grain), 0.8f + n * 0.25f);
            if (x % 16 == 0) c = dark;
            if ((y > 16 && y < 26) || (y > 102 && y < 112)) { c = mul(acc, 0.7f + n * 0.4f); if (x % 16 == 8 && (y == 21 || y == 107)) c = mul(acc, 1.4f); }
        } else {
            c = mul(base, 0.8f + n * 0.3f);
            int e = SF_MIN(SF_MIN(x, N - 1 - x), SF_MIN(y, N - 1 - y));
            if (e < 8) c = mul(dark, 1.1f + n * 0.2f);
            if (e == 8) c = mul(base, 1.35f);
            if (y > 56 && y < 72) c = ((x + y) / 8) % 2 ? acc : mul(dark, 0.9f);
            if (x > 58 && x < 70 && (y < 56 || y > 72) && e > 8) c = mul(base, 0.7f);
        }
        put(img, N, x, y, c);
    }
}

static void gen_crate(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float grain = fbm(u * 2, v * 0.25f, 16, 3, d->seed);
        C3 c = mul(mix(base, hi, grain), 0.8f + grain * 0.3f);
        if (y % 16 == 0) c = mul(dark, 1.2f);
        int e = SF_MIN(SF_MIN(x, N - 1 - x), SF_MIN(y, N - 1 - y));
        bool diag = abs(x - y) < 7 || abs(x + y - (N - 1)) < 7;
        if (diag && e > 10) c = mul(hi, 0.9f + grain * 0.2f);
        if (e < 10) c = mul(hi, 0.85f + grain * 0.3f);
        if (e == 0 || e == 10) c = dark;
        put(img, N, x, y, c);
    }
}

static void gen_flesh(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, f2;
        float f1 = worley(u, v, 5, d->seed, &f2, NULL);
        float n = fbm(u, v, 8, 5, d->seed);
        C3 c = mix(hi, base, smooth(0.0f, 0.6f, f1));
        c = mul(c, 0.7f + n * 0.5f);
        float vein = smooth(0.05f, 0.0f, f2 - f1);
        c = mix(c, dark, vein);
        put(img, N, x, y, c);
    }
}

static void gen_ceilpanel(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), glow = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float n = fbm(x / (float)N, y / (float)N, 8, 3, d->seed);
        C3 c = mul(base, 0.85f + n * 0.3f);
        int lx = x % 64, ly = y % 64;
        if (lx < 3 || ly < 3) c = dark;
        if (lx == 3 || ly == 3) c = mul(base, 1.4f);
        if (lx > 24 && lx < 40 && ly > 28 && ly < 36) c = mix(glow, c3(1, 1, 1), 0.2f);
        if (((lx == 10 || lx == 54) && (ly == 10 || ly == 54))) c = mul(base, 1.8f);
        put(img, N, x, y, c);
    }
}

static void gen_lightpanel(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), glow = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float n = fbm(x / (float)N, y / (float)N, 8, 3, d->seed);
        C3 c = mul(base, 0.85f + n * 0.25f);
        int lx = x % 64, ly = y % 64;
        if (lx < 2 || ly < 2) c = dark;
        int e = SF_MIN(SF_MIN(lx - 12, 52 - lx), SF_MIN(ly - 12, 52 - ly));
        if (e >= 0) c = mix(glow, c3(1, 1, 1), e > 4 ? 0.4f : 0.f);
        else if (e > -3) c = mul(base, 1.5f);
        put(img, N, x, y, c);
    }
}

static void gen_grass(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float n = fbm(u, v, 4, 5, d->seed), b = lat(x, y, N, d->seed), b2 = lat(x, y + 1, N, d->seed);
        C3 c = mix(dark, base, n);
        if (b > 0.6f) c = mix(c, hi, (b - 0.6f) * 2.0f);
        if (b2 > 0.92f) c = mul(c, 0.7f);
        float dirt = smooth(0.62f, 0.7f, fbm(u, v, 4, 4, d->seed + 9));
        c = mix(c, mul(hx(0x5A4330), 0.8f + n * 0.4f), dirt * 0.8f);
        put(img, N, x, y, c);
    }
}

static void gen_liquid(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), dark = hx(d->c2), hi = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N;
        float w = fbm(u + 0.1f * sinf(v * 6.2832f * 2), v, 4, 5, d->seed);
        float r = 0.5f + 0.5f * sinf((w * 8 + u * 2) * 6.2832f);
        C3 c = mix(dark, base, w);
        c = mix(c, hi, smooth(0.8f, 1.0f, r) * (d->p1 ? 0.8f : 0.5f));
        put(img, N, x, y, c);
    }
}

static void gen_lava(const TexDef *d, uint8_t *img) {
    C3 base = hx(d->c1), crust = hx(d->c2), hot = hx(d->c3);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float u = x / (float)N, v = y / (float)N, f2;
        float f1 = worley(u, v, 4, d->seed, &f2, NULL);
        float n = fbm(u, v, 4, 5, d->seed + 3);
        float crack = smooth(0.18f, 0.0f, f2 - f1);
        C3 c = mul(crust, 0.7f + n * 0.6f);
        c = mix(c, base, smooth(0.3f, 0.7f, n));
        c = mix(c, hot, crack);
        c = mix(c, c3(1, 0.95f, 0.7f), crack * crack * 0.6f);
        put(img, N, x, y, c);
    }
}

void texgen_make(int i, uint8_t *img) {
    const TexDef *d = &g_defs[i];
    switch (d->kind) {
    case K_BRICK: gen_brick(d, img); break;
    case K_BLOCKS: gen_blocks(d, img); break;
    case K_COBBLE: gen_cobble(d, img); break;
    case K_METAL: gen_metal(d, img); break;
    case K_GRATE: gen_grate(d, img); break;
    case K_TECH: gen_tech(d, img); break;
    case K_SCREEN: gen_screen(d, img); break;
    case K_PLANKS: gen_planks(d, img); break;
    case K_WOODPANEL: gen_woodpanel(d, img); break;
    case K_MARBLE: gen_marble(d, img); break;
    case K_NOISE: gen_noise(d, img); break;
    case K_TILES: gen_tiles(d, img); break;
    case K_BEAM: gen_beam(d, img); break;
    case K_DOOR: gen_door(d, img); break;
    case K_CRATE: gen_crate(d, img); break;
    case K_FLESH: gen_flesh(d, img); break;
    case K_CEILPANEL: gen_ceilpanel(d, img); break;
    case K_LIGHTPANEL: gen_lightpanel(d, img); break;
    case K_GRASS: gen_grass(d, img); break;
    case K_LIQUID: gen_liquid(d, img); break;
    case K_LAVA: gen_lava(d, img); break;
    default: { /* sky thumbnail */
        uint8_t *sky = malloc(SKY_W * SKY_H * 4);
        texgen_sky(sky);
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) memcpy(img + (y * N + x) * 4, sky + ((y * 2) * SKY_W + x * 4) * 4, 4);
        free(sky);
    } break;
    }
}

void texgen_sky(uint8_t *img) {
    C3 top = hx(0x0B1222), mid = hx(0x2A3A5E), hor = hx(0xF29A5C), glow = hx(0xFFD9A0);
    for (int y = 0; y < SKY_H; y++) for (int x = 0; x < SKY_W; x++) {
        float u = x / (float)SKY_W, v = y / (float)SKY_H;
        C3 c = v < 0.55f ? mix(top, mid, v / 0.55f) : mix(mid, hor, (v - 0.55f) / 0.45f);
        float cl = fbm(u, v * 0.5f, 6, 6, 77);
        float cloud = smooth(0.5f, 0.75f, cl) * smooth(0.95f, 0.3f, v);
        c = mix(c, mix(hx(0x3B4A6B), glow, v), cloud * 0.7f);
        float st = lat(x, y, SKY_W, 5);
        if (v < 0.45f && st > 0.997f) c = mix(c, c3(1, 1, 1), (0.45f - v) * 2);
        /* distant mountains */
        float hgt = 0.80f - 0.12f * fbm(u, 0, 8, 4, 31);
        if (v > hgt) c = mul(mix(hx(0x1A1E2A), hx(0x2A2228), (v - hgt) * 3), 1.0f);
        put(img, SKY_W, x, y, c);
    }
}

/* ------------------------------------------------------------------ sprites */
typedef struct { uint8_t *img; int w, h; } Canvas;

static void blend(Canvas *cv, int x, int y, C3 c, float a) {
    if (x < 0 || y < 0 || x >= cv->w || y >= cv->h || a <= 0) return;
    uint8_t *p = cv->img + ((size_t)y * cv->w + x) * 4;
    float da = p[3] / 255.f;
    float oa = a + da * (1 - a);
    if (oa <= 0) return;
    for (int k = 0; k < 3; k++) {
        float src = (&c.r)[k], dst = p[k] / 255.f;
        float o = (src * a + dst * da * (1 - a)) / oa;
        p[k] = (uint8_t)(clampf(o, 0, 1) * 255);
    }
    p[3] = (uint8_t)(clampf(oa, 0, 1) * 255);
}

/* shaded ellipse (sphere-ish lighting from top-left) */
static void ellipse(Canvas *cv, float cx, float cy, float rx, float ry, C3 c, float shade) {
    for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++)
        for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = sqrtf(dx * dx + dy * dy);
            float a = clampf((1 - d) * SF_MIN(rx, ry), 0, 1);
            if (a <= 0) continue;
            float l = 1 - shade * clampf(0.5f + 0.5f * (dx * 0.6f + dy * 0.8f), 0, 1);
            blend(cv, x, y, mul(c, l), a);
        }
}
static void box(Canvas *cv, float x0, float y0, float x1, float y1, C3 c, float shade) {
    for (int y = (int)y0; y < (int)y1; y++)
        for (int x = (int)x0; x < (int)x1; x++) {
            float t = (x - x0) / SF_MAX(1.f, x1 - x0);
            blend(cv, x, y, mul(c, 1 - shade * t), 1);
        }
}
static void thick(Canvas *cv, float ax, float ay, float bx, float by, float r, C3 c) {
    int x0 = (int)(SF_MIN(ax, bx) - r - 1), x1 = (int)(SF_MAX(ax, bx) + r + 1);
    int y0 = (int)(SF_MIN(ay, by) - r - 1), y1 = (int)(SF_MAX(ay, by) + r + 1);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        double t;
        float d = (float)seg_dist(x + 0.5, y + 0.5, ax, ay, bx, by, &t);
        blend(cv, x, y, c, clampf(r - d + 0.5f, 0, 1));
    }
}
static void glow(Canvas *cv, float cx, float cy, float r, C3 c, float strength) {
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
        float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
        if (d < 1) blend(cv, x, y, c, (1 - d) * (1 - d) * strength);
    }
}

void sprite_make(int type, uint8_t *rgba) {
    memset(rgba, 0, SPRITE_W * SPRITE_H * 4);
    Canvas cv = { rgba, SPRITE_W, SPRITE_H };
    int B = SPRITE_H; /* bottom */
    switch (type) {
    case TH_PLAYER:
        ellipse(&cv, 32, B - 30, 12, 22, hx(0x2E8B57), 0.5f);
        ellipse(&cv, 32, B - 60, 9, 10, hx(0xD9B38C), 0.4f);
        ellipse(&cv, 32, B - 66, 10, 6, hx(0x3DDC84), 0.4f);
        break;
    case TH_LIGHT:
        box(&cv, 29, B - 56, 35, B, hx(0x3A3F46), 0.5f);
        box(&cv, 25, B - 60, 39, B - 54, hx(0x5A616B), 0.5f);
        glow(&cv, 32, B - 72, 26, hx(0xFF9A3D), 0.8f);
        ellipse(&cv, 32, B - 70, 6, 12, hx(0xFF7A1A), 0.0f);
        ellipse(&cv, 32, B - 67, 3.5f, 7, hx(0xFFE08A), 0.0f);
        break;
    case TH_IMP: {
        C3 skin = hx(0x7A3A22), dark = hx(0x4A1E10);
        thick(&cv, 24, B - 34, 20, B - 2, 5, dark); thick(&cv, 40, B - 34, 44, B - 2, 5, dark);
        ellipse(&cv, 32, B - 46, 15, 18, skin, 0.6f);
        thick(&cv, 18, B - 58, 8, B - 36, 4, skin); thick(&cv, 46, B - 58, 56, B - 36, 4, skin);
        thick(&cv, 8, B - 36, 6, B - 30, 3, hx(0xE8D8C0)); thick(&cv, 56, B - 36, 58, B - 30, 3, hx(0xE8D8C0));
        ellipse(&cv, 32, B - 70, 10, 10, skin, 0.5f);
        thick(&cv, 25, B - 76, 20, B - 88, 2.2f, hx(0xD8CCB0)); thick(&cv, 39, B - 76, 44, B - 88, 2.2f, hx(0xD8CCB0));
        glow(&cv, 28, B - 71, 4, hx(0xFFD23D), 1); glow(&cv, 36, B - 71, 4, hx(0xFFD23D), 1);
        thick(&cv, 28, B - 64, 36, B - 64, 1.2f, hx(0x2A0A05));
        for (int k = 0; k < 4; k++) ellipse(&cv, 26.0f + k * 4, B - 50.0f, 1.2f, 1.2f, hx(0x3A1408), 0);
    } break;
    case TH_SOLDIER: {
        C3 arm = hx(0x4A5A3A), dark = hx(0x2A3322);
        thick(&cv, 26, B - 34, 24, B - 2, 5, dark); thick(&cv, 38, B - 34, 40, B - 2, 5, dark);
        box(&cv, 18, B - 64, 46, B - 32, arm, 0.45f);
        box(&cv, 22, B - 56, 42, B - 44, hx(0x3A4630), 0.3f);
        thick(&cv, 18, B - 60, 12, B - 40, 4, arm); thick(&cv, 46, B - 60, 50, B - 44, 4, arm);
        box(&cv, 34, B - 48, 58, B - 42, hx(0x2A2D31), 0.3f);
        ellipse(&cv, 32, B - 72, 8, 9, hx(0xC8A07A), 0.4f);
        ellipse(&cv, 32, B - 77, 10, 6, arm, 0.4f);
        box(&cv, 26, B - 74, 38, B - 70, hx(0x1A1D21), 0);
    } break;
    case TH_HEALTH:
        box(&cv, 14, B - 26, 50, B, hx(0xE8ECEF), 0.35f);
        box(&cv, 28, B - 23, 36, B - 3, hx(0xE53935), 0.2f);
        box(&cv, 22, B - 17, 42, B - 9, hx(0xE53935), 0.2f);
        box(&cv, 26, B - 30, 38, B - 26, hx(0x9AA3B0), 0.2f);
        break;
    case TH_ARMOR:
        ellipse(&cv, 32, B - 16, 18, 16, hx(0x2F6FBF), 0.55f);
        box(&cv, 22, B - 30, 42, B - 20, hx(0x2F6FBF), 0.4f);
        thick(&cv, 32, B - 28, 32, B - 4, 1.5f, hx(0x89C2FF));
        glow(&cv, 32, B - 18, 20, hx(0x4DA3FF), 0.35f);
        break;
    case TH_AMMO:
        box(&cv, 12, B - 18, 52, B, hx(0x4A5A3A), 0.4f);
        for (int k = 0; k < 5; k++) {
            box(&cv, 16.0f + k * 7, B - 30, 21.0f + k * 7, B - 18, hx(0xC9A227), 0.3f);
            ellipse(&cv, 18.5f + k * 7, B - 30, 2.5f, 3, hx(0xB87333), 0.3f);
        }
        box(&cv, 18, B - 12, 46, B - 7, hx(0xFFB27A), 0);
        break;
    case TH_BARREL:
        box(&cv, 16, B - 42, 48, B, hx(0x4F6B3A), 0.6f);
        box(&cv, 16, B - 32, 48, B - 29, hx(0x2E3E22), 0.5f);
        box(&cv, 16, B - 13, 48, B - 10, hx(0x2E3E22), 0.5f);
        ellipse(&cv, 32, B - 42, 16, 4, hx(0x7DDB2E), 0.2f);
        glow(&cv, 32, B - 44, 14, hx(0x9BFF5A), 0.5f);
        break;
    case TH_LAMP:
        box(&cv, 27, B - 92, 37, B, hx(0x6B6D72), 0.55f);
        box(&cv, 22, B - 8, 42, B, hx(0x4A4C50), 0.5f);
        box(&cv, 22, B - 98, 42, B - 92, hx(0x4A4C50), 0.5f);
        glow(&cv, 32, B - 108, 30, hx(0xFFF4D6), 0.75f);
        ellipse(&cv, 32, B - 106, 9, 9, hx(0xFFF4D6), 0.1f);
        break;
    case TH_TREE: {
        C3 bark = hx(0x5A4330);
        thick(&cv, 32, B, 31, B - 70, 5, bark);
        thick(&cv, 31, B - 50, 14, B - 82, 3, bark); thick(&cv, 31, B - 60, 50, B - 96, 3, bark);
        thick(&cv, 31, B - 70, 28, B - 110, 2.5f, bark); thick(&cv, 14, B - 82, 6, B - 92, 1.8f, bark);
        thick(&cv, 50, B - 96, 58, B - 100, 1.8f, bark); thick(&cv, 22, B - 66, 10, B - 64, 1.8f, bark);
    } break;
    default:
        ellipse(&cv, 32, B - 20, 16, 16, hx(0x9AA3B0), 0.5f);
    }
}

void weapon_make(uint8_t *rgba, bool flash) {
    memset(rgba, 0, WEAPON_W * WEAPON_H * 4);
    Canvas cv = { rgba, WEAPON_W, WEAPON_H };
    if (flash) {
        glow(&cv, 64, 26, 34, hx(0xFFB02E), 0.95f);
        glow(&cv, 64, 26, 16, hx(0xFFF4D6), 1.0f);
    }
    box(&cv, 50, 40, 78, 128, hx(0x2C3138), 0.5f);   /* slide */
    box(&cv, 54, 30, 74, 44, hx(0x3A4049), 0.4f);    /* muzzle */
    box(&cv, 60, 28, 68, 32, hx(0x0A0B0D), 0);
    box(&cv, 46, 86, 82, 128, hx(0x3A2A20), 0.45f);  /* grip */
    for (int y = 92; y < 128; y += 6) box(&cv, 48, (float)y, 80, (float)y + 2, hx(0x2A1E16), 0.3f);
    box(&cv, 62, 36, 66, 40, hx(0xFF7A1A), 0);       /* front sight */
    thick(&cv, 30, 128, 50, 96, 9, hx(0xC8A07A));    /* hands */
    thick(&cv, 98, 128, 78, 96, 9, hx(0xB89070));
}
