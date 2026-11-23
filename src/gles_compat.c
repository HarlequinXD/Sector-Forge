/* Sector Forge - GL 1.1 fixed-function emulation on OpenGL ES 3.0 (see gles_compat.h).
   Geometry from glBegin/glEnd is converted to triangle or line lists and
   accumulated until some state changes, so a frame of UI quads becomes a
   handful of draw calls. */
#define SF_GLES_COMPAT_IMPL
#include "gles_compat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x, y, z, u, v; GLubyte c[4]; } Vtx;

typedef struct { float m[16]; } Mat;

static struct {
    bool ready;
    GLuint prog, vao, vbo;
    GLint u_mvp, u_mvz, u_tex, u_usetex, u_afunc, u_aref, u_fog, u_fogmode, u_fogrange;
    /* batch */
    Vtx *batch; int nbatch, capbatch;
    GLenum batch_kind;               /* GL_TRIANGLES or GL_LINES */
    /* current primitive */
    Vtx *prim; int nprim, capprim;
    GLenum prim_mode;
    bool in_prim;
    Vtx cur;
    /* matrices */
    Mat stack[2][32];
    int sp[2];
    int mode;                        /* 0 modelview, 1 projection, 2 ignored (texture) */
    bool mat_dirty;
    /* fixed-function state */
    bool tex2d, fog, alpha_test, line_mode;
    GLenum afunc; float aref;
    GLenum fog_mode; float fog_density, fog_start, fog_end, fog_col[4];
    bool state_dirty;
    int draws;
    /* offscreen pick target */
    GLuint fbo, rb_color, rb_depth; int fbo_w, fbo_h;
} G;

static void mat_ident(Mat *a) { memset(a, 0, sizeof *a); a->m[0] = a->m[5] = a->m[10] = a->m[15] = 1; }
static void mat_mul(Mat *out, const Mat *a, const Mat *b) {
    Mat r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + rr] * b->m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    *out = r;
}
static Mat *top(void) { int i = G.mode > 1 ? 0 : G.mode; return &G.stack[i][G.sp[i]]; }

/* ------------------------------------------------------------------ shader */
static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec2 a_uv;\n"
    "layout(location=2) in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "uniform vec4 u_mvz;\n"
    "out vec2 v_uv; out vec4 v_col; out float v_z;\n"
    "void main(){ gl_Position = u_mvp * vec4(a_pos, 1.0); v_uv = a_uv; v_col = a_col;\n"
    "  v_z = abs(dot(u_mvz, vec4(a_pos, 1.0))); }\n";
static const char *FS =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv; in vec4 v_col; in float v_z;\n"
    "uniform sampler2D u_tex; uniform int u_usetex; uniform int u_afunc; uniform float u_aref;\n"
    "uniform vec4 u_fog; uniform int u_fogmode; uniform vec2 u_fogrange;\n"
    "out vec4 o_col;\n"
    "void main(){\n"
    "  vec4 c = v_col;\n"
    "  if (u_usetex != 0) c *= texture(u_tex, v_uv);\n"
    "  if (u_afunc != 0) {\n"
    "    bool pass = u_afunc == 1 ? c.a > u_aref : u_afunc == 2 ? c.a >= u_aref : u_afunc == 3 ? c.a < u_aref :\n"
    "                u_afunc == 4 ? c.a <= u_aref : u_afunc == 5 ? c.a == u_aref : u_afunc == 6 ? c.a != u_aref : false;\n"
    "    if (!pass) discard;\n"
    "  }\n"
    "  if (u_fogmode != 0) {\n"
    "    float f;\n"
    "    if (u_fogmode == 1) f = (u_fogrange.y - v_z) / max(u_fogrange.y - u_fogrange.x, 1e-6);\n"
    "    else if (u_fogmode == 2) f = exp(-u_fog.a * v_z);\n"
    "    else { float d = u_fog.a * v_z; f = exp(-d * d); }\n"
    "    c.rgb = mix(u_fog.rgb, c.rgb, clamp(f, 0.0, 1.0));\n"
    "  }\n"
    "  o_col = c;\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "sfgl shader: %s\n", log);
    }
    return s;
}

bool sfgl_init(void) {
    /* called whenever a fresh context is current: GL objects from a lost context are gone */
    G.fbo = G.rb_color = G.rb_depth = 0;
    G.fbo_w = G.fbo_h = 0;
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    G.prog = glCreateProgram();
    glAttachShader(G.prog, vs);
    glAttachShader(G.prog, fs);
    glLinkProgram(G.prog);
    GLint ok = 0;
    glGetProgramiv(G.prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(G.prog, sizeof log, NULL, log);
        fprintf(stderr, "sfgl link: %s\n", log);
        return false;
    }
    G.u_mvp = glGetUniformLocation(G.prog, "u_mvp");
    G.u_mvz = glGetUniformLocation(G.prog, "u_mvz");
    G.u_tex = glGetUniformLocation(G.prog, "u_tex");
    G.u_usetex = glGetUniformLocation(G.prog, "u_usetex");
    G.u_afunc = glGetUniformLocation(G.prog, "u_afunc");
    G.u_aref = glGetUniformLocation(G.prog, "u_aref");
    G.u_fog = glGetUniformLocation(G.prog, "u_fog");
    G.u_fogmode = glGetUniformLocation(G.prog, "u_fogmode");
    G.u_fogrange = glGetUniformLocation(G.prog, "u_fogrange");
    glGenVertexArrays(1, &G.vao);
    glGenBuffers(1, &G.vbo);
    glBindVertexArray(G.vao);
    glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(sizeof(float) * 3));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void *)(sizeof(float) * 5));
    glUseProgram(G.prog);
    glUniform1i(G.u_tex, 0);
    if (!G.ready) {
        for (int i = 0; i < 2; i++) { G.sp[i] = 0; mat_ident(&G.stack[i][0]); }
        G.cur.c[0] = G.cur.c[1] = G.cur.c[2] = G.cur.c[3] = 255;
        G.afunc = GL_ALWAYS;
        G.fog_mode = GL_EXP;
        G.fog_density = 1;
        G.fog_end = 1;
    }
    G.mat_dirty = G.state_dirty = true;
    G.nbatch = 0;
    G.ready = true;
    return true;
}

static int afunc_code(void) {
    if (!G.alpha_test) return 0;
    switch (G.afunc) {
    case GL_GREATER: return 1;
    case GL_GEQUAL: return 2;
    case GL_LESS: return 3;
    case GL_LEQUAL: return 4;
    case GL_EQUAL: return 5;
    case GL_NOTEQUAL: return 6;
    case GL_NEVER: return 7;
    default: return 0;
    }
}

void sfgl_flush(void) {
    if (!G.ready || !G.nbatch) return;
    glUseProgram(G.prog);
    glBindVertexArray(G.vao);
    glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    if (G.mat_dirty) {
        Mat mvp;
        mat_mul(&mvp, &G.stack[1][G.sp[1]], &G.stack[0][G.sp[0]]);
        glUniformMatrix4fv(G.u_mvp, 1, GL_FALSE, mvp.m);
        const float *mv = G.stack[0][G.sp[0]].m;
        glUniform4f(G.u_mvz, mv[2], mv[6], mv[10], mv[14]);
        G.mat_dirty = false;
    }
    if (G.state_dirty) {
        glUniform1i(G.u_usetex, G.tex2d ? 1 : 0);
        glUniform1i(G.u_afunc, afunc_code());
        glUniform1f(G.u_aref, G.aref);
        int fm = 0;
        if (G.fog) fm = G.fog_mode == GL_LINEAR ? 1 : G.fog_mode == GL_EXP ? 2 : 3;
        glUniform1i(G.u_fogmode, fm);
        glUniform4f(G.u_fog, G.fog_col[0], G.fog_col[1], G.fog_col[2], G.fog_density);
        glUniform2f(G.u_fogrange, G.fog_start, G.fog_end);
        G.state_dirty = false;
    }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * (size_t)G.nbatch), G.batch, GL_STREAM_DRAW);
    glDrawArrays(G.batch_kind, 0, G.nbatch);
    G.draws++;
    G.nbatch = 0;
}

int sfgl_draw_calls(void) { int d = G.draws; G.draws = 0; return d; }

/* ------------------------------------------------------------------ immediate mode */
static void push_batch(const Vtx *v) {
    if (G.nbatch == G.capbatch) {
        G.capbatch = G.capbatch ? G.capbatch * 2 : 4096;
        G.batch = realloc(G.batch, sizeof(Vtx) * (size_t)G.capbatch);
    }
    G.batch[G.nbatch++] = *v;
}
static void tri(int a, int b, int c) {
    if (G.line_mode) {
        push_batch(&G.prim[a]); push_batch(&G.prim[b]);
        push_batch(&G.prim[b]); push_batch(&G.prim[c]);
        push_batch(&G.prim[c]); push_batch(&G.prim[a]);
    } else {
        push_batch(&G.prim[a]); push_batch(&G.prim[b]); push_batch(&G.prim[c]);
    }
}
static void seg(int a, int b) { push_batch(&G.prim[a]); push_batch(&G.prim[b]); }

void sfgl_Begin(GLenum mode) { G.prim_mode = mode; G.nprim = 0; G.in_prim = true; }

void sfgl_End(void) {
    G.in_prim = false;
    int n = G.nprim;
    GLenum m = G.prim_mode;
    bool lines_prim = m == GL_LINES || m == GL_LINE_STRIP || m == GL_LINE_LOOP;
    if (m == GL_POINTS || n < 2) return;
    GLenum kind = (lines_prim || G.line_mode) ? GL_LINES : GL_TRIANGLES;
    if (kind != G.batch_kind) { sfgl_flush(); G.batch_kind = kind; }
    switch (m) {
    case GL_LINES: for (int i = 0; i + 1 < n; i += 2) seg(i, i + 1); break;
    case GL_LINE_STRIP: for (int i = 0; i + 1 < n; i++) seg(i, i + 1); break;
    case GL_LINE_LOOP: for (int i = 0; i < n; i++) seg(i, (i + 1) % n); break;
    case GL_TRIANGLES: for (int i = 0; i + 2 < n; i += 3) tri(i, i + 1, i + 2); break;
    case GL_TRIANGLE_FAN: for (int i = 1; i + 1 < n; i++) tri(0, i, i + 1); break;
    case GL_TRIANGLE_STRIP:
        for (int i = 0; i + 2 < n; i++) { if (i & 1) tri(i + 1, i, i + 2); else tri(i, i + 1, i + 2); }
        break;
    case GL_QUADS:
        for (int i = 0; i + 3 < n; i += 4) {
            if (G.line_mode) { seg(i, i + 1); seg(i + 1, i + 2); seg(i + 2, i + 3); seg(i + 3, i); }
            else { tri(i, i + 1, i + 2); tri(i, i + 2, i + 3); }
        }
        break;
    case GL_QUAD_STRIP:
        for (int i = 0; i + 3 < n; i += 2) {
            if (G.line_mode) { seg(i, i + 1); seg(i + 1, i + 3); seg(i + 3, i + 2); seg(i + 2, i); }
            else { tri(i, i + 1, i + 3); tri(i, i + 3, i + 2); }
        }
        break;
    case GL_POLYGON:
        if (G.line_mode) for (int i = 0; i < n; i++) seg(i, (i + 1) % n);
        else for (int i = 1; i + 1 < n; i++) tri(0, i, i + 1);
        break;
    default: break;
    }
}

static void vtx(float x, float y, float z) {
    if (!G.in_prim) return;
    if (G.nprim == G.capprim) {
        G.capprim = G.capprim ? G.capprim * 2 : 1024;
        G.prim = realloc(G.prim, sizeof(Vtx) * (size_t)G.capprim);
    }
    Vtx v = G.cur;
    v.x = x; v.y = y; v.z = z;
    G.prim[G.nprim++] = v;
}
void sfgl_Vertex2f(float x, float y) { vtx(x, y, 0); }
void sfgl_Vertex3f(float x, float y, float z) { vtx(x, y, z); }
void sfgl_TexCoord2f(float u, float v) { G.cur.u = u; G.cur.v = v; }
static GLubyte f2b(float f) { return (GLubyte)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f); }
void sfgl_Color3f(float r, float g, float b) { G.cur.c[0] = f2b(r); G.cur.c[1] = f2b(g); G.cur.c[2] = f2b(b); G.cur.c[3] = 255; }
void sfgl_Color4f(float r, float g, float b, float a) { G.cur.c[0] = f2b(r); G.cur.c[1] = f2b(g); G.cur.c[2] = f2b(b); G.cur.c[3] = f2b(a); }
void sfgl_Color3ub(GLubyte r, GLubyte g, GLubyte b) { G.cur.c[0] = r; G.cur.c[1] = g; G.cur.c[2] = b; G.cur.c[3] = 255; }
void sfgl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { G.cur.c[0] = r; G.cur.c[1] = g; G.cur.c[2] = b; G.cur.c[3] = a; }

/* ------------------------------------------------------------------ matrices */
static void mat_changed(void) { if (G.mode <= 1) { sfgl_flush(); G.mat_dirty = true; } }
static void mult(const Mat *b) { if (G.mode > 1) return; mat_changed(); Mat *t = top(); mat_mul(t, t, b); }

void sfgl_MatrixMode(GLenum m) { G.mode = m == GL_MODELVIEW ? 0 : m == GL_PROJECTION ? 1 : 2; }
void sfgl_LoadIdentity(void) { if (G.mode > 1) return; mat_changed(); mat_ident(top()); }
void sfgl_PushMatrix(void) {
    if (G.mode > 1 || G.sp[G.mode] >= 31) return;
    G.stack[G.mode][G.sp[G.mode] + 1] = G.stack[G.mode][G.sp[G.mode]];
    G.sp[G.mode]++;
}
void sfgl_PopMatrix(void) { if (G.mode > 1 || G.sp[G.mode] <= 0) return; mat_changed(); G.sp[G.mode]--; }
void sfgl_Ortho(double l, double r, double b, double t, double n, double f) {
    Mat m; mat_ident(&m);
    m.m[0] = (float)(2 / (r - l)); m.m[5] = (float)(2 / (t - b)); m.m[10] = (float)(-2 / (f - n));
    m.m[12] = (float)(-(r + l) / (r - l)); m.m[13] = (float)(-(t + b) / (t - b)); m.m[14] = (float)(-(f + n) / (f - n));
    mult(&m);
}
void sfgl_Translatef(float x, float y, float z) { Mat m; mat_ident(&m); m.m[12] = x; m.m[13] = y; m.m[14] = z; mult(&m); }
void sfgl_Scalef(float x, float y, float z) { Mat m; mat_ident(&m); m.m[0] = x; m.m[5] = y; m.m[10] = z; mult(&m); }
void sfgl_Rotatef(float a, float x, float y, float z) {
    float l = sqrtf(x * x + y * y + z * z);
    if (l <= 0) return;
    x /= l; y /= l; z /= l;
    float c = cosf(a * 3.14159265f / 180), s = sinf(a * 3.14159265f / 180), ic = 1 - c;
    Mat m; mat_ident(&m);
    m.m[0] = x * x * ic + c;     m.m[4] = x * y * ic - z * s; m.m[8] = x * z * ic + y * s;
    m.m[1] = y * x * ic + z * s; m.m[5] = y * y * ic + c;     m.m[9] = y * z * ic - x * s;
    m.m[2] = x * z * ic - y * s; m.m[6] = y * z * ic + x * s; m.m[10] = z * z * ic + c;
    mult(&m);
}
void sfgl_Perspective(double fovy, double aspect, double zn, double zf) {
    double f = 1.0 / tan(fovy * 3.14159265358979 / 360.0);
    Mat m; memset(&m, 0, sizeof m);
    m.m[0] = (float)(f / aspect); m.m[5] = (float)f;
    m.m[10] = (float)((zf + zn) / (zn - zf)); m.m[11] = -1;
    m.m[14] = (float)(2 * zf * zn / (zn - zf));
    mult(&m);
}
void sfgl_LookAt(double ex, double ey, double ez, double cx, double cy, double cz, double ux, double uy, double uz) {
    double fx = cx - ex, fy = cy - ey, fz = cz - ez;
    double fl = sqrt(fx * fx + fy * fy + fz * fz); if (fl > 0) { fx /= fl; fy /= fl; fz /= fl; }
    double sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    double sl = sqrt(sx * sx + sy * sy + sz * sz); if (sl > 0) { sx /= sl; sy /= sl; sz /= sl; }
    double vx = sy * fz - sz * fy, vy = sz * fx - sx * fz, vz = sx * fy - sy * fx;
    Mat m; mat_ident(&m);
    m.m[0] = (float)sx; m.m[4] = (float)sy; m.m[8] = (float)sz;
    m.m[1] = (float)vx; m.m[5] = (float)vy; m.m[9] = (float)vz;
    m.m[2] = (float)-fx; m.m[6] = (float)-fy; m.m[10] = (float)-fz;
    mult(&m);
    sfgl_Translatef((float)-ex, (float)-ey, (float)-ez);
}
void sfgl_GetDoublev(GLenum p, GLdouble *v) {
    if (p == GL_MODELVIEW_MATRIX || p == GL_PROJECTION_MATRIX) {
        const float *m = G.stack[p == GL_MODELVIEW_MATRIX ? 0 : 1][G.sp[p == GL_MODELVIEW_MATRIX ? 0 : 1]].m;
        for (int i = 0; i < 16; i++) v[i] = m[i];
        return;
    }
    float f[16] = { 0 };
    glGetFloatv(p, f);
    v[0] = f[0];
}
GLint sfgl_Project(double ox, double oy, double oz, const double mv[16], const double pr[16], const GLint vp[4], double *wx, double *wy, double *wz) {
    double in[4] = { ox, oy, oz, 1 }, e[4], c[4];
    for (int r = 0; r < 4; r++) e[r] = mv[r] * in[0] + mv[4 + r] * in[1] + mv[8 + r] * in[2] + mv[12 + r] * in[3];
    for (int r = 0; r < 4; r++) c[r] = pr[r] * e[0] + pr[4 + r] * e[1] + pr[8 + r] * e[2] + pr[12 + r] * e[3];
    if (c[3] == 0) return GL_FALSE;
    c[0] /= c[3]; c[1] /= c[3]; c[2] /= c[3];
    *wx = vp[0] + (1 + c[0]) * vp[2] / 2;
    *wy = vp[1] + (1 + c[1]) * vp[3] / 2;
    *wz = (1 + c[2]) / 2;
    return GL_TRUE;
}

/* ------------------------------------------------------------------ state */
static void set_flag(bool *f, bool v) { if (*f != v) { sfgl_flush(); *f = v; G.state_dirty = true; } }
static void cap(GLenum c, bool on) {
    switch (c) {
    case GL_TEXTURE_2D: set_flag(&G.tex2d, on); return;
    case GL_FOG: set_flag(&G.fog, on); return;
    case GL_ALPHA_TEST: set_flag(&G.alpha_test, on); return;
    case GL_POLYGON_OFFSET_LINE: case GL_LINE_SMOOTH: case 0x809D /* GL_MULTISAMPLE */: return;
    default:
        sfgl_flush();
        if (on) glEnable(c); else glDisable(c);
    }
}
void sfgl_Enable(GLenum c) { cap(c, true); }
void sfgl_Disable(GLenum c) { cap(c, false); }
void sfgl_TexEnvi(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; /* always GL_MODULATE */ }
void sfgl_AlphaFunc(GLenum f, float ref) {
    if (G.afunc == f && G.aref == ref) return;
    sfgl_flush(); G.afunc = f; G.aref = ref; G.state_dirty = true;
}
void sfgl_Fogi(GLenum p, GLint v) { if (p == GL_FOG_MODE) { sfgl_flush(); G.fog_mode = (GLenum)v; G.state_dirty = true; } }
void sfgl_Fogf(GLenum p, float v) {
    sfgl_flush();
    if (p == GL_FOG_DENSITY) G.fog_density = v;
    else if (p == GL_FOG_START) G.fog_start = v;
    else if (p == GL_FOG_END) G.fog_end = v;
    else if (p == GL_FOG_MODE) G.fog_mode = (GLenum)v;
    G.state_dirty = true;
}
void sfgl_Fogfv(GLenum p, const float *v) {
    if (p == GL_FOG_COLOR) { sfgl_flush(); memcpy(G.fog_col, v, sizeof G.fog_col); G.state_dirty = true; }
    else sfgl_Fogf(p, v[0]);
}
void sfgl_PolygonMode(GLenum face, GLenum mode) { (void)face; if ((mode == GL_LINE) != G.line_mode) { sfgl_flush(); G.line_mode = mode == GL_LINE; } }
void sfgl_Hint(GLenum t, GLenum m) { if (t != GL_FOG_HINT) glHint(t, m); }
void sfgl_ClearDepth(double d) { glClearDepthf((float)d); }

void sfgl_BindTexture(GLenum t, GLuint id) { sfgl_flush(); glBindTexture(t, id); }
void sfgl_BlendFunc(GLenum s, GLenum d) { sfgl_flush(); glBlendFunc(s, d); }
void sfgl_DepthFunc(GLenum f) { sfgl_flush(); glDepthFunc(f); }
void sfgl_DepthMask(GLboolean m) { sfgl_flush(); glDepthMask(m); }
void sfgl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { sfgl_flush(); glColorMask(r, g, b, a); }
void sfgl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { sfgl_flush(); glScissor(x, y, w, h); }
void sfgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { sfgl_flush(); glViewport(x, y, w, h); }
void sfgl_Clear(GLbitfield m) { sfgl_flush(); glClear(m); }
void sfgl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p) { sfgl_flush(); glReadPixels(x, y, w, h, f, t, p); }
void sfgl_PolygonOffset(float f, float u) { sfgl_flush(); glPolygonOffset(f, u); }
void sfgl_LineWidth(float w) { sfgl_flush(); glLineWidth(w); }
void sfgl_TexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p) {
    sfgl_flush(); glTexImage2D(t, l, ifmt, w, h, b, f, ty, p);
}
void sfgl_TexParameteri(GLenum t, GLenum p, GLint v) { sfgl_flush(); glTexParameteri(t, p, v); }
void sfgl_TexParameterf(GLenum t, GLenum p, float v) { sfgl_flush(); glTexParameterf(t, p, v); }
void sfgl_DeleteTextures(GLsizei n, const GLuint *ids) { sfgl_flush(); glDeleteTextures(n, ids); }

static int g_surf_w = 1, g_surf_h = 1;
static GLuint g_default_fbo;
void sfgl_set_default_fbo(GLuint fbo) { g_default_fbo = fbo; }
void sfgl_set_surface_size(int w, int h) { g_surf_w = w > 0 ? w : 1; g_surf_h = h > 0 ? h : 1; }

void sfgl_offscreen(bool on) {
    int w = g_surf_w, h = g_surf_h;
    sfgl_flush();
    if (!on) { glBindFramebuffer(GL_FRAMEBUFFER, g_default_fbo); return; }
    if (!G.fbo || G.fbo_w != w || G.fbo_h != h) {
        if (!G.fbo) {
            glGenFramebuffers(1, &G.fbo);
            glGenRenderbuffers(1, &G.rb_color);
            glGenRenderbuffers(1, &G.rb_depth);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, G.rb_color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, G.rb_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, G.rb_color);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, G.rb_depth);
        G.fbo_w = w; G.fbo_h = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, G.fbo);
}
