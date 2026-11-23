/* Sector Forge - OpenGL 1.x drawing helpers (2D UI layer, textures, fonts) */
#ifndef SF_GFX_H
#define SF_GFX_H
#include "base.h"
#include "plat.h"
#include "icons.h"

typedef struct { unsigned id; int w, h; } Tex;

enum { FONT_UI, FONT_UI_BOLD, FONT_SMALL, FONT_MONO, FONT_MONO_SMALL, FONT_HEAD, FONT_TITLE, FONT_HUD, FONT_COUNT };

typedef struct {
    Tex tex;
    GlyphInfo ascii[96];
    GlyphInfo *extra;
    int nextra;
    float ascent, height;   /* logical px */
    float scale;            /* physical / logical */
    bool loaded;
} Font;

/* theme */
#define TH_BG        0x0E0F12
#define TH_TITLE     0x0A0B0D
#define TH_PANEL     0x14161A
#define TH_PANEL2    0x121418
#define TH_VIEW      0x0B0C0F
#define TH_BORDER    0x1F2228
#define TH_BORDER2   0x2B2F37
#define TH_FIELD     0x0F1114
#define TH_HOVER     0x23262D
#define TH_BTN       0x1B1E24
#define TH_TEXT      0xE7E9EE
#define TH_TEXT2     0xAAB2BF
#define TH_TEXT3     0x8E96A3
#define TH_ACCENT    0xFF7A1A
#define TH_ACCENT_HI 0xFFB27A
#define TH_ACCENT_DK 0x3A2210
#define TH_ACCENT_BD 0x7A4116
#define TH_BLUE      0x4DA3FF
#define TH_GREEN     0x3DDC84
#define TH_RED       0xFF4D4D
#define TH_YELLOW    0xFFD23D

void gfx_init(void);
Tex gfx_tex_create(const uint8_t *rgba, int w, int h, bool mipmap, bool repeat, bool nearest);
void gfx_tex_free(Tex *t);
void gfx_build_mips(const uint8_t *rgba, int w, int h);

void gfx_begin_2d(float w, float h, float scale);
void gfx_clip_push(Rect r);
void gfx_clip_pop(void);
Rect gfx_clip_current(void);

void gfx_rect(Rect r, Col c);
void gfx_rect_line(Rect r, float th, Col c);
void gfx_rrect(Rect r, float rad, Col c);
void gfx_rrect_line(Rect r, float rad, float th, Col c);
void gfx_shadow(Rect r, float rad, float size, Col c);
void gfx_grad_v(Rect r, Col top, Col bottom);
void gfx_line(float x0, float y0, float x1, float y1, float th, Col c);
void gfx_dashed(float x0, float y0, float x1, float y1, float th, float dash, float gap, float phase, Col c);
void gfx_circle(float cx, float cy, float r, Col c);
void gfx_circle_line(float cx, float cy, float r, float th, Col c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, Col c);
void gfx_quad_tex(const Tex *t, Rect r, float u0, float v0, float u1, float v1, Col tint);
void gfx_icon(int icon, float cx, float cy, float size, Col c);
void gfx_set_icon_atlas(Tex t);

bool gfx_font_build(int id, const char *face, float px, int weight, float scale);
float gfx_text(int font, float x, float y, const char *s, Col c);
float gfx_text_n(int font, float x, float y, const char *s, int n, Col c);
float gfx_text_w(int font, const char *s);
float gfx_text_wn(int font, const char *s, int n);
float gfx_font_h(int font);
void gfx_text_fit(int font, float x, float y, float maxw, const char *s, Col c);
void gfx_text_center(int font, Rect r, const char *s, Col c);
void gfx_text_right(int font, float rx, float y, const char *s, Col c);
uint32_t utf8_next(const char **s);

#endif
