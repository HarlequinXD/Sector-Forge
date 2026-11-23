/* Sector Forge - base types, math and containers (C11) */
#ifndef SF_BASE_H
#define SF_BASE_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SF_VERSION "0.1.0"
#define SF_PI 3.14159265358979323846
#define SF_DEG2RAD (SF_PI / 180.0)

#define SF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define SF_MAX(a, b) ((a) > (b) ? (a) : (b))
#define SF_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
#define SF_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define SF_UNUSED(x) ((void)(x))

typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { double x, y; } D2;
typedef struct { float x, y, w, h; } Rect;
typedef struct { uint8_t r, g, b, a; } Col;

static inline V2 v2(float x, float y) { V2 r = { x, y }; return r; }
static inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static inline D2 d2(double x, double y) { D2 r = { x, y }; return r; }
static inline Rect rect(float x, float y, float w, float h) { Rect r = { x, y, w, h }; return r; }
static inline Col col(int r, int g, int b, int a) { Col c = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a }; return c; }
static inline Col col_hex(uint32_t hex) { return col((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, 255); }
static inline Col col_alpha(Col c, int a) { c.a = (uint8_t)SF_CLAMP(a, 0, 255); return c; }
static inline Col col_lerp(Col a, Col b, float t) {
    return col((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t), (int)(a.a + (b.a - a.a) * t));
}

static inline bool rect_has(Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static inline Rect rect_inset(Rect r, float d) { return rect(r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d); }
static inline Rect rect_cut_left(Rect *r, float w) { Rect o = rect(r->x, r->y, w, r->h); r->x += w; r->w -= w; return o; }
static inline Rect rect_cut_right(Rect *r, float w) { Rect o = rect(r->x + r->w - w, r->y, w, r->h); r->w -= w; return o; }
static inline Rect rect_cut_top(Rect *r, float h) { Rect o = rect(r->x, r->y, r->w, h); r->y += h; r->h -= h; return o; }
static inline Rect rect_cut_bottom(Rect *r, float h) { Rect o = rect(r->x, r->y + r->h - h, r->w, h); r->h -= h; return o; }

static inline V3 v3_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3_scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(V3 a) { return sqrtf(v3_dot(a, a)); }

static inline double d2_cross(D2 a, D2 b) { return a.x * b.y - a.y * b.x; }
static inline double d2_dist(D2 a, D2 b) { double dx = a.x - b.x, dy = a.y - b.y; return sqrt(dx * dx + dy * dy); }

/* -------- dynamic arrays -------- */
void *sf_grow(void *ptr, int *cap, int need, size_t elem);
#define ARR(T) struct { T *a; int n, cap; }
#define arr_reserve(A, k) ((A).a = sf_grow((A).a, &(A).cap, (k), sizeof(*(A).a)))
#define arr_push(A, v) (arr_reserve(A, (A).n + 1), (A).a[(A).n++] = (v))
#define arr_free(A) (free((A).a), (A).a = NULL, (A).n = (A).cap = 0)
#define arr_clear(A) ((A).n = 0)
#define arr_last(A) ((A).a[(A).n - 1])
#define arr_remove(A, i) (memmove(&(A).a[(i)], &(A).a[(i) + 1], sizeof(*(A).a) * (size_t)((A).n - (i)-1)), (A).n--)

typedef ARR(int) IntArr;
typedef ARR(float) FloatArr;

/* -------- growable text buffer -------- */
typedef struct { char *s; int n, cap; } Buf;
void buf_printf(Buf *b, const char *fmt, ...);
void buf_append(Buf *b, const char *s, int len);
void buf_free(Buf *b);

/* -------- strings / misc -------- */
void sf_strcpy(char *dst, size_t cap, const char *src);
char *sf_strdup(const char *s);
int sf_stricmp(const char *a, const char *b);
bool sf_stristr(const char *hay, const char *needle);
void sf_upper(char *s);
uint32_t sf_hash(const char *s, uint32_t seed);
const char *sf_basename(const char *path);
void sf_path_join(char *out, size_t cap, const char *a, const char *b);
void sf_strip_ext(char *s);
void sf_sanitize_name(char *s);  /* make a string safe for a file name */
char *sf_read_file(const char *path, int *len); /* uses plat_fopen */

/* simple xorshift RNG */
typedef struct { uint32_t s; } Rng;
static inline uint32_t rng_u32(Rng *r) { uint32_t x = r->s ? r->s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; r->s = x; return x; }
static inline float rng_f(Rng *r) { return (rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

#endif
