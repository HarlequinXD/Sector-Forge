/* Sector Forge - OpenGL drawing helpers */
#include "gfx.h"
#include "glinc.h"

static Font g_fonts[FONT_COUNT];
static Tex g_icons;
static float g_scale = 1, g_w, g_h;
static Rect g_clip[32];
static int g_nclip;
static bool g_aniso;
static float g_aniso_max = 1;

void gfx_init(void) {
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    if (ext && strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
        g_aniso = true;
        glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &g_aniso_max);
        if (g_aniso_max > 8) g_aniso_max = 8;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

void gfx_build_mips(const uint8_t *rgba, int w, int h) {
    uint8_t *cur = (uint8_t *)rgba, *nxt = NULL;
    int level = 0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    while (w > 1 || h > 1) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        nxt = malloc((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                int x0 = SF_MIN(x * 2, w - 1), x1 = SF_MIN(x * 2 + 1, w - 1);
                int y0 = SF_MIN(y * 2, h - 1), y1 = SF_MIN(y * 2 + 1, h - 1);
                const uint8_t *a = cur + (y0 * w + x0) * 4, *b = cur + (y0 * w + x1) * 4;
                const uint8_t *c = cur + (y1 * w + x0) * 4, *d = cur + (y1 * w + x1) * 4;
                uint8_t *o = nxt + (y * nw + x) * 4;
                /* alpha-weighted average keeps sprite/icon edges clean */
                int aw = a[3] + b[3] + c[3] + d[3];
                for (int k = 0; k < 3; k++)
                    o[k] = aw ? (uint8_t)((a[k] * a[3] + b[k] * b[3] + c[k] * c[3] + d[k] * d[3]) / aw)
                              : (uint8_t)((a[k] + b[k] + c[k] + d[k]) / 4);
                o[3] = (uint8_t)(aw / 4);
            }
        level++;
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nxt);
        if (cur != rgba) free(cur);
        cur = nxt;
        w = nw; h = nh;
    }
    if (cur != rgba) free(cur);
}

Tex gfx_tex_create(const uint8_t *rgba, int w, int h, bool mipmap, bool repeat, bool nearest) {
    Tex t = { 0, w, h };
    GLuint id;
    glGenTextures(1, &id);
    t.id = id;
    glBindTexture(GL_TEXTURE_2D, id);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    if (mipmap) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
        if (g_aniso) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_aniso_max);
        gfx_build_mips(rgba, w, h);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    return t;
}

void gfx_tex_free(Tex *t) {
    if (t->id) { GLuint id = t->id; glDeleteTextures(1, &id); }
    t->id = 0;
}

void gfx_set_icon_atlas(Tex t) { g_icons = t; }

/* ------------------------------------------------------------------ 2D state */
static void apply_scissor(void) {
    if (!g_nclip) { glDisable(GL_SCISSOR_TEST); return; }
    Rect r = g_clip[g_nclip - 1];
    glEnable(GL_SCISSOR_TEST);
    int x = (int)floorf(r.x * g_scale), y = (int)floorf((g_h - r.y - r.h) * g_scale);
    int w = (int)ceilf(r.w * g_scale), h = (int)ceilf(r.h * g_scale);
    glScissor(x, y, SF_MAX(w, 0), SF_MAX(h, 0));
}

void gfx_begin_2d(float w, float h, float scale) {
    g_w = w; g_h = h; g_scale = scale;
    g_nclip = 0;
    glViewport(0, 0, (GLsizei)(w * scale + 0.5f), (GLsizei)(h * scale + 0.5f));
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    apply_scissor();
}

static Rect rect_isect(Rect a, Rect b) {
    float x0 = SF_MAX(a.x, b.x), y0 = SF_MAX(a.y, b.y);
    float x1 = SF_MIN(a.x + a.w, b.x + b.w), y1 = SF_MIN(a.y + a.h, b.y + b.h);
    return rect(x0, y0, SF_MAX(0, x1 - x0), SF_MAX(0, y1 - y0));
}

void gfx_clip_push(Rect r) {
    if (g_nclip) r = rect_isect(r, g_clip[g_nclip - 1]);
    if (g_nclip < 32) g_clip[g_nclip++] = r;
    apply_scissor();
}
void gfx_clip_pop(void) { if (g_nclip) g_nclip--; apply_scissor(); }
Rect gfx_clip_current(void) { return g_nclip ? g_clip[g_nclip - 1] : rect(0, 0, g_w, g_h); }

static void setc(Col c) { glColor4ub(c.r, c.g, c.b, c.a); }
static void notex(void) { glDisable(GL_TEXTURE_2D); }

void gfx_rect(Rect r, Col c) {
    if (r.w <= 0 || r.h <= 0 || !c.a) return;
    notex(); setc(c);
    glBegin(GL_QUADS);
    glVertex2f(r.x, r.y); glVertex2f(r.x + r.w, r.y); glVertex2f(r.x + r.w, r.y + r.h); glVertex2f(r.x, r.y + r.h);
    glEnd();
}

void gfx_rect_line(Rect r, float t, Col c) {
    gfx_rect(rect(r.x, r.y, r.w, t), c);
    gfx_rect(rect(r.x, r.y + r.h - t, r.w, t), c);
    gfx_rect(rect(r.x, r.y + t, t, r.h - 2 * t), c);
    gfx_rect(rect(r.x + r.w - t, r.y + t, t, r.h - 2 * t), c);
}

/* builds a CCW-on-screen perimeter: top-right -> top-left -> bottom-left -> bottom-right */
static int rrect_perimeter(Rect r, float rad, float *xs, float *ys) {
    rad = SF_MIN(rad, SF_MIN(r.w, r.h) * 0.5f);
    if (rad < 0.5f) {
        xs[0] = r.x + r.w; ys[0] = r.y; xs[1] = r.x; ys[1] = r.y;
        xs[2] = r.x; ys[2] = r.y + r.h; xs[3] = r.x + r.w; ys[3] = r.y + r.h;
        return 4;
    }
    const int SEG = 5;
    float cx[4] = { r.x + r.w - rad, r.x + rad, r.x + rad, r.x + r.w - rad };
    float cy[4] = { r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad };
    float start[4] = { 0, 270, 180, 90 }; /* going backwards in angle (y-down screen) */
    int k = 0;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= SEG; i++) {
            float ang = (float)((start[c] - 90.0f * i / SEG) * SF_DEG2RAD);
            if (c == 0) ang = (float)((360 - 90.0f * i / SEG) * SF_DEG2RAD);
            xs[k] = cx[c] + rad * cosf(ang);
            ys[k] = cy[c] + rad * sinf(ang);
            k++;
        }
    return k;
}

void gfx_rrect(Rect r, float rad, Col c) {
    if (r.w <= 0 || r.h <= 0 || !c.a) return;
    float xs[32], ys[32];
    int n = rrect_perimeter(r, rad, xs, ys);
    notex(); setc(c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(r.x + r.w * 0.5f, r.y + r.h * 0.5f);
    for (int i = 0; i < n; i++) glVertex2f(xs[i], ys[i]);
    glVertex2f(xs[0], ys[0]);
    glEnd();
}

void gfx_rrect_line(Rect r, float rad, float th, Col c) {
    if (r.w <= 0 || r.h <= 0 || !c.a) return;
    float xo[32], yo[32], xi[32], yi[32];
    int n = rrect_perimeter(r, rad, xo, yo);
    int m = rrect_perimeter(rect_inset(r, th), SF_MAX(0, rad - th), xi, yi);
    if (n != m) { gfx_rect_line(r, th, c); return; }
    notex(); setc(c);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= n; i++) { int k = i % n; glVertex2f(xo[k], yo[k]); glVertex2f(xi[k], yi[k]); }
    glEnd();
}

void gfx_shadow(Rect r, float rad, float size, Col c) {
    int steps = 6;
    for (int i = steps; i >= 1; i--) {
        float t = (float)i / steps;
        Col cc = col_alpha(c, (int)(c.a * (1 - t) * (1 - t) * 0.5f));
        gfx_rrect(rect(r.x - size * t, r.y - size * t + size * 0.35f, r.w + 2 * size * t, r.h + 2 * size * t), rad + size * t, cc);
    }
}

void gfx_grad_v(Rect r, Col top, Col bottom) {
    notex();
    glBegin(GL_QUADS);
    setc(top); glVertex2f(r.x, r.y); glVertex2f(r.x + r.w, r.y);
    setc(bottom); glVertex2f(r.x + r.w, r.y + r.h); glVertex2f(r.x, r.y + r.h);
    glEnd();
}

void gfx_line(float x0, float y0, float x1, float y1, float th, Col c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    notex(); setc(c);
    glBegin(GL_QUADS);
    glVertex2f(x0 + nx, y0 + ny); glVertex2f(x1 + nx, y1 + ny); glVertex2f(x1 - nx, y1 - ny); glVertex2f(x0 - nx, y0 - ny);
    glEnd();
}

void gfx_dashed(float x0, float y0, float x1, float y1, float th, float dash, float gap, float phase, Col c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) return;
    float ux = dx / l, uy = dy / l, per = dash + gap;
    float t = -fmodf(phase, per);
    for (; t < l; t += per) {
        float a = SF_MAX(t, 0), b = SF_MIN(t + dash, l);
        if (b > a) gfx_line(x0 + ux * a, y0 + uy * a, x0 + ux * b, y0 + uy * b, th, c);
    }
}

void gfx_circle(float cx, float cy, float r, Col c) {
    int n = (int)SF_CLAMP(r * 1.5f, 12, 64);
    notex(); setc(c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= n; i++) { float a = (float)(i * 2 * SF_PI / n); glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r); }
    glEnd();
}

void gfx_circle_line(float cx, float cy, float r, float th, Col c) {
    int n = (int)SF_CLAMP(r * 1.5f, 12, 64);
    notex(); setc(c);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= n; i++) {
        float a = (float)(i * 2 * SF_PI / n), ca = cosf(a), sa = sinf(a);
        glVertex2f(cx + ca * (r + th * 0.5f), cy + sa * (r + th * 0.5f));
        glVertex2f(cx + ca * (r - th * 0.5f), cy + sa * (r - th * 0.5f));
    }
    glEnd();
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, Col c) {
    notex(); setc(c);
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0); glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}

void gfx_quad_tex(const Tex *t, Rect r, float u0, float v0, float u1, float v1, Col tint) {
    if (!t || !t->id) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t->id);
    setc(tint);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(r.x, r.y);
    glTexCoord2f(u1, v0); glVertex2f(r.x + r.w, r.y);
    glTexCoord2f(u1, v1); glVertex2f(r.x + r.w, r.y + r.h);
    glTexCoord2f(u0, v1); glVertex2f(r.x, r.y + r.h);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void gfx_icon(int icon, float cx, float cy, float size, Col c) {
    if (!g_icons.id || icon < 0 || icon >= IC_COUNT) return;
    float u0 = (float)((icon % ICON_COLS) * ICON_CELL) / ICON_ATLAS_W, v0 = (float)((icon / ICON_COLS) * ICON_CELL) / ICON_ATLAS_H;
    float u1 = u0 + (float)ICON_CELL / ICON_ATLAS_W, v1 = v0 + (float)ICON_CELL / ICON_ATLAS_H;
    /* snap to physical pixels for crispness */
    float x = floorf((cx - size * 0.5f) * g_scale + 0.5f) / g_scale, y = floorf((cy - size * 0.5f) * g_scale + 0.5f) / g_scale;
    gfx_quad_tex(&g_icons, rect(x, y, size, size), u0, v0, u1, v1, c);
}

/* ------------------------------------------------------------------ fonts */
static const uint32_t g_extra_cps[] = { 0xB7, 0xD7, 0xB0, 0x2014, 0x2013, 0x2026, 0x2191, 0x2193, 0x2195, 0x2192, 0x2190, 0x25CF, 0x2212, 0xA9, 0x2022 };

bool gfx_font_build(int id, const char *face, float px, int weight, float scale) {
    Font *f = &g_fonts[id];
    uint32_t cps[96 + SF_COUNT(g_extra_cps)];
    int n = 0;
    for (uint32_t c = 32; c < 127; c++) cps[n++] = c;
    for (int i = 0; i < SF_COUNT(g_extra_cps); i++) cps[n++] = g_extra_cps[i];
    FontRaster fr;
    memset(&fr, 0, sizeof fr);
    if (!plat_font_raster(face, px * scale, weight, cps, n, &fr)) return false;
    if (f->loaded) { gfx_tex_free(&f->tex); free(f->extra); }
    memset(f, 0, sizeof *f);
    uint8_t *rgba = malloc((size_t)fr.w * fr.h * 4);
    for (int i = 0; i < fr.w * fr.h; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = fr.alpha[i]; }
    f->tex = gfx_tex_create(rgba, fr.w, fr.h, false, false, true);
    free(rgba);
    f->extra = malloc(sizeof(GlyphInfo) * SF_COUNT(g_extra_cps));
    for (int i = 0; i < fr.nglyphs; i++) {
        GlyphInfo g = fr.glyphs[i];
        if (g.cp >= 32 && g.cp < 128) f->ascii[g.cp - 32] = g;
        else if (f->nextra < SF_COUNT(g_extra_cps)) f->extra[f->nextra++] = g;
    }
    f->scale = scale;
    f->ascent = fr.ascent / scale;
    f->height = fr.height / scale;
    f->loaded = true;
    free(fr.alpha);
    free(fr.glyphs);
    return true;
}

uint32_t utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = *s;
    int n = 1;
    if (c >= 0xF0) { c = ((c & 7u) << 18) | ((s[1] & 63u) << 12) | ((s[2] & 63u) << 6) | (s[3] & 63u); n = 4; }
    else if (c >= 0xE0) { c = ((c & 15u) << 12) | ((s[1] & 63u) << 6) | (s[2] & 63u); n = 3; }
    else if (c >= 0xC0) { c = ((c & 31u) << 6) | (s[1] & 63u); n = 2; }
    for (int i = 1; i < n; i++) if (!s[i]) { n = i; break; }
    *ps += n;
    return c;
}

static const GlyphInfo *glyph(const Font *f, uint32_t cp) {
    if (cp >= 32 && cp < 128) return &f->ascii[cp - 32];
    for (int i = 0; i < f->nextra; i++) if (f->extra[i].cp == cp) return &f->extra[i];
    return &f->ascii['?' - 32];
}

float gfx_text_n(int font, float x, float y, const char *s, int n, Col c) {
    Font *f = &g_fonts[font];
    if (!f->loaded || !s) return x;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, f->tex.id);
    setc(c);
    float sc = f->scale;
    float px = floorf(x * sc + 0.5f), py = floorf(y * sc + 0.5f);
    const char *end = n < 0 ? NULL : s + n;
    glBegin(GL_QUADS);
    while (*s && (!end || s < end)) {
        uint32_t cp = utf8_next(&s);
        const GlyphInfo *g = glyph(f, cp);
        if (g->w > 0) {
            float gx = (px + g->xoff) / sc, gy = (py + g->yoff) / sc;
            float gw = g->w / sc, gh = g->h / sc;
            float u0 = (float)g->x / f->tex.w, v0 = (float)g->y / f->tex.h;
            float u1 = (float)(g->x + g->w) / f->tex.w, v1 = (float)(g->y + g->h) / f->tex.h;
            glTexCoord2f(u0, v0); glVertex2f(gx, gy);
            glTexCoord2f(u1, v0); glVertex2f(gx + gw, gy);
            glTexCoord2f(u1, v1); glVertex2f(gx + gw, gy + gh);
            glTexCoord2f(u0, v1); glVertex2f(gx, gy + gh);
        }
        px += g->adv;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    return px / sc;
}

float gfx_text(int font, float x, float y, const char *s, Col c) { return gfx_text_n(font, x, y, s, -1, c); }

float gfx_text_wn(int font, const char *s, int n) {
    Font *f = &g_fonts[font];
    if (!f->loaded || !s) return 0;
    float w = 0;
    const char *end = n < 0 ? NULL : s + n;
    while (*s && (!end || s < end)) w += glyph(f, utf8_next(&s))->adv;
    return w / f->scale;
}
float gfx_text_w(int font, const char *s) { return gfx_text_wn(font, s, -1); }
float gfx_font_h(int font) { return g_fonts[font].height; }

void gfx_text_fit(int font, float x, float y, float maxw, const char *s, Col c) {
    if (gfx_text_w(font, s) <= maxw) { gfx_text(font, x, y, s, c); return; }
    float ell = gfx_text_w(font, "\xE2\x80\xA6");
    const char *p = s;
    float w = 0;
    while (*p) {
        const char *q = p;
        uint32_t cp = utf8_next(&q);
        float gw = glyph(&g_fonts[font], cp)->adv / g_fonts[font].scale;
        if (w + gw + ell > maxw) break;
        w += gw;
        p = q;
    }
    float ex = gfx_text_n(font, x, y, s, (int)(p - s), c);
    gfx_text(font, ex, y, "\xE2\x80\xA6", c);
}

void gfx_text_center(int font, Rect r, const char *s, Col c) {
    float w = gfx_text_w(font, s), h = gfx_font_h(font);
    gfx_text(font, r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, s, c);
}

void gfx_text_right(int font, float rx, float y, const char *s, Col c) { gfx_text(font, rx - gfx_text_w(font, s), y, s, c); }
