/* Software OpenGL 1.1 subset for the headless harness: enough of the fixed-function pipeline
 * (matrices, textures, blending, depth, scissor, alpha test) to render previews and make picking real. */
#include "GL/gl.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

int g_sw_enabled = 0;
int g_fb_w = 1440, g_fb_h = 900;
unsigned char *g_fb;
static float *g_depth;

typedef struct { int w, h; unsigned char *px; int wrap_repeat; } STex;
static STex g_texs[8192];
static GLuint g_next_tex = 1, g_bound;
static double g_mat[3][32][16];
static int g_sp[3], g_mode;
static float g_col[4] = { 1, 1, 1, 1 }, g_tc[2];
static int g_en_tex, g_en_blend, g_en_depth, g_en_scissor, g_en_alpha;
static float g_alpha_ref;
static GLenum g_depth_func = GL_LESS;
static int g_depth_mask = 1, g_color_mask = 1, g_poly_line;
static int g_sc[4], g_vp[4];
static GLenum g_env = GL_MODULATE;
static float g_clear[4];
static GLenum g_prim;
typedef struct { double x, y, z, w; float u, v; float c[4]; } Vtx;
static Vtx g_v[65536];
static int g_nv;

static void ensure_fb(void) {
    if (!g_fb) { g_fb = calloc((size_t)g_fb_w * g_fb_h * 4, 1); g_depth = malloc(sizeof(float) * (size_t)g_fb_w * g_fb_h); }
}
static double *M(void) { return g_mat[g_mode][g_sp[g_mode]]; }
static void ident(double *m) { memset(m, 0, 16 * sizeof(double)); m[0] = m[5] = m[10] = m[15] = 1; }
static void mulm(double *m, const double *r) { /* m = m * r, column-major */
    double o[16];
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
        double s = 0;
        for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * r[c * 4 + k];
        o[c * 4 + rr] = s;
    }
    memcpy(m, o, sizeof o);
}
static int mode_idx(GLenum m) { return m == GL_PROJECTION ? 1 : m == GL_TEXTURE ? 2 : 0; }

void glMatrixMode(GLenum m) { g_mode = mode_idx(m); }
void glLoadIdentity(void) { ident(M()); }
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
    double o[16] = { 0 };
    o[0] = 2 / (r - l); o[5] = 2 / (t - b); o[10] = -2 / (f - n);
    o[12] = -(r + l) / (r - l); o[13] = -(t + b) / (t - b); o[14] = -(f + n) / (f - n); o[15] = 1;
    mulm(M(), o);
}
void glTranslatef(GLfloat x, GLfloat y, GLfloat z) { double o[16]; ident(o); o[12] = x; o[13] = y; o[14] = z; mulm(M(), o); }
void gluPerspective(GLdouble fovy, GLdouble a, GLdouble n, GLdouble f) {
    double t = 1.0 / tan(fovy * 3.14159265358979 / 360.0), o[16] = { 0 };
    o[0] = t / a; o[5] = t; o[10] = (f + n) / (n - f); o[11] = -1; o[14] = 2 * f * n / (n - f);
    mulm(M(), o);
}
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz) {
    double f[3] = { cx - ex, cy - ey, cz - ez }, l = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; i++) f[i] /= l;
    double s[3] = { f[1] * uz - f[2] * uy, f[2] * ux - f[0] * uz, f[0] * uy - f[1] * ux };
    l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (int i = 0; i < 3; i++) s[i] /= l;
    double u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    double o[16];
    ident(o);
    o[0] = s[0]; o[4] = s[1]; o[8] = s[2];
    o[1] = u[0]; o[5] = u[1]; o[9] = u[2];
    o[2] = -f[0]; o[6] = -f[1]; o[10] = -f[2];
    mulm(M(), o);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}
void glGetDoublev(GLenum e, GLdouble *d) { memcpy(d, g_mat[e == GL_PROJECTION_MATRIX ? 1 : 0][g_sp[e == GL_PROJECTION_MATRIX ? 1 : 0]], 16 * sizeof(double)); }
void glGetFloatv(GLenum e, GLfloat *d) { (void)e; d[0] = 1; }
void glGetIntegerv(GLenum e, GLint *d) { (void)e; memcpy(d, g_vp, sizeof g_vp); }
GLint gluProject(GLdouble ox, GLdouble oy, GLdouble oz, const GLdouble *mv, const GLdouble *pr, const GLint *vp, GLdouble *wx, GLdouble *wy, GLdouble *wz) {
    double in[4] = { ox, oy, oz, 1 }, t[4], c[4];
    for (int r = 0; r < 4; r++) t[r] = mv[r] * in[0] + mv[4 + r] * in[1] + mv[8 + r] * in[2] + mv[12 + r];
    for (int r = 0; r < 4; r++) c[r] = pr[r] * t[0] + pr[4 + r] * t[1] + pr[8 + r] * t[2] + pr[12 + r] * t[3];
    if (c[3] == 0) return 0;
    *wx = vp[0] + (c[0] / c[3] * 0.5 + 0.5) * vp[2];
    *wy = vp[1] + (c[1] / c[3] * 0.5 + 0.5) * vp[3];
    *wz = c[2] / c[3] * 0.5 + 0.5;
    return 1;
}
const GLubyte *glGetString(GLenum e) { (void)e; return (const GLubyte *)""; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { g_vp[0] = x; g_vp[1] = y; g_vp[2] = w; g_vp[3] = h; }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { g_sc[0] = x; g_sc[1] = y; g_sc[2] = w; g_sc[3] = h; }
static int *flag(GLenum e) {
    static int dummy;
    switch (e) { case GL_TEXTURE_2D: return &g_en_tex; case GL_BLEND: return &g_en_blend; case GL_DEPTH_TEST: return &g_en_depth;
    case GL_SCISSOR_TEST: return &g_en_scissor; case GL_ALPHA_TEST: return &g_en_alpha; default: return &dummy; }
}
void glEnable(GLenum e) { *flag(e) = 1; }
void glDisable(GLenum e) { *flag(e) = 0; }
void glAlphaFunc(GLenum f, GLclampf r) { (void)f; g_alpha_ref = r; }
void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
void glDepthFunc(GLenum f) { g_depth_func = f; }
void glDepthMask(GLboolean m) { g_depth_mask = m; }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)g; (void)b; (void)a; g_color_mask = r; }
void glPolygonMode(GLenum f, GLenum m) { (void)f; g_poly_line = m == GL_LINE; }
void glTexEnvi(GLenum t, GLenum p, GLint v) { (void)t; (void)p; g_env = (GLenum)v; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { g_clear[0] = r; g_clear[1] = g; g_clear[2] = b; g_clear[3] = a; }
void glClearDepth(GLclampd d) { (void)d; }
void glClear(GLbitfield m) {
    if (!g_sw_enabled) return;
    ensure_fb();
    int x0 = 0, y0 = 0, x1 = g_fb_w, y1 = g_fb_h;
    if (g_en_scissor) { x0 = g_sc[0]; y0 = g_sc[1]; x1 = g_sc[0] + g_sc[2]; y1 = g_sc[1] + g_sc[3]; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_fb_w) x1 = g_fb_w;
    if (y1 > g_fb_h) y1 = g_fb_h;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        size_t i = (size_t)y * g_fb_w + x;
        if (m & GL_COLOR_BUFFER_BIT) for (int k = 0; k < 4; k++) g_fb[i * 4 + k] = (unsigned char)(g_clear[k] * 255);
        if (m & GL_DEPTH_BUFFER_BIT) g_depth[i] = 1.0f;
    }
}
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p) {
    (void)f; (void)t;
    unsigned char *o = p;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int X = x + i, Y = y + j;
        unsigned char *d = o + ((size_t)j * w + i) * 4;
        if (!g_fb || X < 0 || Y < 0 || X >= g_fb_w || Y >= g_fb_h) { memset(d, 0, 4); continue; }
        memcpy(d, g_fb + ((size_t)Y * g_fb_w + X) * 4, 4);
    }
}
void glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = g_next_tex++; }
void glDeleteTextures(GLsizei n, const GLuint *t) { for (int i = 0; i < n; i++) if (t[i] < 8192) { free(g_texs[t[i]].px); g_texs[t[i]].px = NULL; } }
void glBindTexture(GLenum t, GLuint id) { (void)t; g_bound = id; }
void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; if (p == GL_TEXTURE_WRAP_S && g_bound < 8192) g_texs[g_bound].wrap_repeat = v == GL_REPEAT; }
void glTexParameterf(GLenum t, GLenum p, GLfloat v) { (void)t; (void)p; (void)v; }
void glPixelStorei(GLenum p, GLint v) { (void)p; (void)v; }
void glTexImage2D(GLenum tg, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum ty, const void *p) {
    (void)tg; (void)ifmt; (void)b; (void)fmt; (void)ty;
    if (lvl != 0 || g_bound >= 8192) return;
    STex *t = &g_texs[g_bound];
    free(t->px);
    t->w = w; t->h = h;
    t->px = malloc((size_t)w * h * 4);
    memcpy(t->px, p, (size_t)w * h * 4);
}
void glHint(GLenum a, GLenum b) { (void)a; (void)b; }
void glFogf(GLenum a, GLfloat b) { (void)a; (void)b; }
void glFogi(GLenum a, GLint b) { (void)a; (void)b; }
void glFogfv(GLenum a, const GLfloat *b) { (void)a; (void)b; }
void glLineWidth(GLfloat w) { (void)w; }
void glPolygonOffset(GLfloat a, GLfloat b) { (void)a; (void)b; }
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { g_col[0] = r; g_col[1] = g; g_col[2] = b; g_col[3] = 1; }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { g_col[0] = r; g_col[1] = g; g_col[2] = b; g_col[3] = a; }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor4f(r / 255.f, g / 255.f, b / 255.f, 1); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.f, g / 255.f, b / 255.f, a / 255.f); }
void glTexCoord2f(GLfloat u, GLfloat v) { g_tc[0] = u; g_tc[1] = v; }
void glBegin(GLenum p) { g_prim = p; g_nv = 0; }
static void vtx(double x, double y, double z) {
    if (g_nv >= 65536) return;
    const double *mv = g_mat[0][g_sp[0]], *pr = g_mat[1][g_sp[1]];
    double t[4], c[4];
    for (int r = 0; r < 4; r++) t[r] = mv[r] * x + mv[4 + r] * y + mv[8 + r] * z + mv[12 + r];
    for (int r = 0; r < 4; r++) c[r] = pr[r] * t[0] + pr[4 + r] * t[1] + pr[8 + r] * t[2] + pr[12 + r] * t[3];
    Vtx *v = &g_v[g_nv++];
    v->x = c[0]; v->y = c[1]; v->z = c[2]; v->w = c[3];
    v->u = g_tc[0]; v->v = g_tc[1];
    memcpy(v->c, g_col, sizeof g_col);
}
void glVertex2f(GLfloat x, GLfloat y) { vtx(x, y, 0); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { vtx(x, y, z); }

static void sample(float u, float v, float *o) {
    STex *t = g_bound < 8192 ? &g_texs[g_bound] : NULL;
    if (!t || !t->px) { o[0] = o[1] = o[2] = o[3] = 1; return; }
    float fu = u * t->w - 0.5f, fv = v * t->h - 0.5f;
    int x0 = (int)floorf(fu), y0 = (int)floorf(fv);
    float ax = fu - x0, ay = fv - y0;
    float acc[4] = { 0 };
    for (int k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        if (t->wrap_repeat) { xx = ((xx % t->w) + t->w) % t->w; yy = ((yy % t->h) + t->h) % t->h; }
        else { xx = xx < 0 ? 0 : xx >= t->w ? t->w - 1 : xx; yy = yy < 0 ? 0 : yy >= t->h ? t->h - 1 : yy; }
        float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
        const unsigned char *p = t->px + ((size_t)yy * t->w + xx) * 4;
        for (int c = 0; c < 4; c++) acc[c] += p[c] / 255.f * wgt;
    }
    memcpy(o, acc, sizeof acc);
}

static void raster_tri(const Vtx *a, const Vtx *b, const Vtx *c) {
    const Vtx *V[3] = { a, b, c };
    double sx[3], sy[3], sz[3], iw[3];
    for (int i = 0; i < 3; i++) {
        iw[i] = 1.0 / V[i]->w;
        sx[i] = g_vp[0] + (V[i]->x * iw[i] * 0.5 + 0.5) * g_vp[2];
        sy[i] = g_vp[1] + (V[i]->y * iw[i] * 0.5 + 0.5) * g_vp[3];
        sz[i] = V[i]->z * iw[i] * 0.5 + 0.5;
    }
    double area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (fabs(area) < 1e-12) return;
    int x0 = (int)floor(fmin(sx[0], fmin(sx[1], sx[2]))), x1 = (int)ceil(fmax(sx[0], fmax(sx[1], sx[2])));
    int y0 = (int)floor(fmin(sy[0], fmin(sy[1], sy[2]))), y1 = (int)ceil(fmax(sy[0], fmax(sy[1], sy[2])));
    int cx0 = 0, cy0 = 0, cx1 = g_fb_w, cy1 = g_fb_h;
    if (g_en_scissor) { cx0 = g_sc[0]; cy0 = g_sc[1]; cx1 = g_sc[0] + g_sc[2]; cy1 = g_sc[1] + g_sc[3]; }
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x1 > g_fb_w) x1 = g_fb_w;
    if (y1 > g_fb_h) y1 = g_fb_h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double px = x + 0.5, py = y + 0.5;
            double w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            double w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            double w2 = 1 - w0 - w1;
            if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
            double z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            size_t i = (size_t)y * g_fb_w + x;
            if (g_en_depth) {
                float dz = g_depth[i];
                int pass = g_depth_func == GL_LEQUAL ? z <= dz + 1e-7 : g_depth_func == GL_EQUAL ? fabs(z - dz) < 1e-6 : z < dz;
                if (!pass) continue;
            }
            double pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
            double b0 = w0 * iw[0] / pw, b1 = w1 * iw[1] / pw, b2 = w2 * iw[2] / pw;
            float col[4];
            for (int k = 0; k < 4; k++) col[k] = (float)(a->c[k] * b0 + b->c[k] * b1 + c->c[k] * b2);
            if (g_en_tex) {
                float t[4];
                sample((float)(a->u * b0 + b->u * b1 + c->u * b2), (float)(a->v * b0 + b->v * b1 + c->v * b2), t);
                if (g_env == GL_REPLACE) memcpy(col, t, sizeof t);
                else for (int k = 0; k < 4; k++) col[k] *= t[k];
            }
            if (g_en_alpha && col[3] <= g_alpha_ref) continue;
            if (g_en_depth && g_depth_mask) g_depth[i] = (float)z;
            if (!g_color_mask) continue;
            unsigned char *d = g_fb + i * 4;
            float al = g_en_blend ? col[3] : 1;
            for (int k = 0; k < 3; k++) {
                float v = col[k] * al + d[k] / 255.f * (1 - al);
                d[k] = (unsigned char)(fminf(fmaxf(v, 0), 1) * 255 + 0.5f);
            }
            d[3] = 255;
        }
}

/* clip polygon against the near plane (z > -w) */
static int clip_near(Vtx *in, int n, Vtx *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        Vtx *a = &in[i], *b = &in[(i + 1) % n];
        double da = a->z + a->w, db = b->z + b->w;
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) {
            double t = da / (da - db);
            Vtx v;
            v.x = a->x + (b->x - a->x) * t; v.y = a->y + (b->y - a->y) * t; v.z = a->z + (b->z - a->z) * t; v.w = a->w + (b->w - a->w) * t;
            v.u = (float)(a->u + (b->u - a->u) * t); v.v = (float)(a->v + (b->v - a->v) * t);
            for (int k = 0; k < 4; k++) v.c[k] = (float)(a->c[k] + (b->c[k] - a->c[k]) * t);
            out[m++] = v;
        }
    }
    return m;
}

static void emit(Vtx *a, Vtx *b, Vtx *c) {
    Vtx in[3] = { *a, *b, *c }, out[8];
    int n = clip_near(in, 3, out);
    for (int i = 1; i + 1 < n; i++) raster_tri(&out[0], &out[i], &out[i + 1]);
}

void glEnd(void) {
    if (!g_sw_enabled || g_poly_line) { g_nv = 0; return; }
    ensure_fb();
    Vtx *v = g_v;
    int n = g_nv;
    if (g_prim == GL_TRIANGLES) for (int i = 0; i + 2 < n; i += 3) emit(&v[i], &v[i + 1], &v[i + 2]);
    else if (g_prim == GL_QUADS) for (int i = 0; i + 3 < n; i += 4) { emit(&v[i], &v[i + 1], &v[i + 2]); emit(&v[i], &v[i + 2], &v[i + 3]); }
    else if (g_prim == GL_TRIANGLE_FAN) for (int i = 1; i + 1 < n; i++) emit(&v[0], &v[i], &v[i + 1]);
    else if (g_prim == GL_TRIANGLE_STRIP) for (int i = 0; i + 2 < n; i++) emit(&v[i], &v[i + 1], &v[i + 2]);
    g_nv = 0;
}

/* ---- GLU tessellator: ear-free fallback that fans each contour (good enough for previews of convex rooms) ---- */
typedef struct { void (*vertex)(void *); double *pts[8192]; int n; int start[1024]; int nc; } Tess;
struct GLUtesselator { Tess t; };
GLUtesselator *gluNewTess(void) { return calloc(1, sizeof(GLUtesselator)); }
void gluDeleteTess(GLUtesselator *t) { free(t); }
void gluTessCallback(GLUtesselator *t, GLenum w, void (*fn)()) { if (w == GLU_TESS_VERTEX) t->t.vertex = (void (*)(void *))fn; }
void gluTessProperty(GLUtesselator *t, GLenum w, GLdouble v) { (void)t; (void)w; (void)v; }
void gluTessNormal(GLUtesselator *t, GLdouble x, GLdouble y, GLdouble z) { (void)t; (void)x; (void)y; (void)z; }
void gluTessBeginPolygon(GLUtesselator *t, void *d) { (void)d; t->t.n = 0; t->t.nc = 0; }
void gluTessBeginContour(GLUtesselator *t) { if (t->t.nc < 1023) t->t.start[t->t.nc++] = t->t.n; }
void gluTessVertex(GLUtesselator *t, GLdouble *c, void *d) { (void)c; if (t->t.n < 8192) t->t.pts[t->t.n++] = d; }
void gluTessEndContour(GLUtesselator *t) { (void)t; }
void gluTessEndPolygon(GLUtesselator *t) {
    Tess *s = &t->t;
    /* only the outer (first, largest) contour is fanned; holes are skipped in previews */
    if (!s->nc) return;
    int best = 0;
    double ba = -1;
    for (int c = 0; c < s->nc; c++) {
        int a = s->start[c], b = c + 1 < s->nc ? s->start[c + 1] : s->n;
        double ar = 0;
        for (int i = a; i < b; i++) { double *p = s->pts[i], *q = s->pts[i + 1 < b ? i + 1 : a]; ar += p[0] * q[1] - q[0] * p[1]; }
        if (fabs(ar) > ba) { ba = fabs(ar); best = c; }
    }
    int a = s->start[best], b = best + 1 < s->nc ? s->start[best + 1] : s->n;
    for (int i = a + 1; i + 1 < b; i++) { s->vertex(s->pts[a]); s->vertex(s->pts[i]); s->vertex(s->pts[i + 1]); }
}

void sw_dump(const char *path) {
    if (!g_fb) return;
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", g_fb_w, g_fb_h);
    for (int y = g_fb_h - 1; y >= 0; y--)
        for (int x = 0; x < g_fb_w; x++) fwrite(g_fb + ((size_t)y * g_fb_w + x) * 4, 1, 3, f);
    fclose(f);
}
