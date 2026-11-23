/* Sector Forge - 3D renderer */
#include "render3d.h"
#include "assets.h"
#include "glinc.h"
#include "tess.h"

/* ------------------------------------------------------------------ triangulation cache (GLU tessellator) */
static struct {
    const Map *map;
    unsigned rev;
    int nsec;
    int *start, *count;     /* per sector offsets into tris (in floats) */
    FloatArr tris;
    bool valid;
} g_cache;

#ifdef SF_USE_GLU
static FloatArr *g_tess_out;
static ARR(double *) g_tess_alloc;

static void CALLBACK tess_begin(GLenum type) { (void)type; }
static void CALLBACK tess_edge(GLboolean flag) { (void)flag; }
static void CALLBACK tess_vertex(void *data) {
    const double *v = data;
    arr_push(*g_tess_out, (float)v[0]);
    arr_push(*g_tess_out, (float)v[1]);
}
static void CALLBACK tess_combine(GLdouble coords[3], void *vd[4], GLfloat w[4], void **out) {
    (void)vd; (void)w;
    double *v = malloc(sizeof(double) * 3);
    v[0] = coords[0]; v[1] = coords[1]; v[2] = coords[2];
    arr_push(g_tess_alloc, v);
    *out = v;
}
static void CALLBACK tess_error(GLenum e) { (void)e; }

#endif

void r3d_invalidate(void) { g_cache.valid = false; }

static void build_cache(const Map *m) {
    free(g_cache.start);
    free(g_cache.count);
    arr_clear(g_cache.tris);
    g_cache.nsec = m->sectors.n;
    g_cache.start = calloc((size_t)(m->sectors.n + 1), sizeof(int));
    g_cache.count = calloc((size_t)(m->sectors.n + 1), sizeof(int));
#ifdef SF_USE_GLU
    GLUtesselator *t = gluNewTess();
    gluTessCallback(t, GLU_TESS_BEGIN, (void (CALLBACK *)())tess_begin);
    gluTessCallback(t, GLU_TESS_EDGE_FLAG, (void (CALLBACK *)())tess_edge);
    gluTessCallback(t, GLU_TESS_VERTEX, (void (CALLBACK *)())tess_vertex);
    gluTessCallback(t, GLU_TESS_COMBINE, (void (CALLBACK *)())tess_combine);
    gluTessCallback(t, GLU_TESS_ERROR, (void (CALLBACK *)())tess_error);
    gluTessProperty(t, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessNormal(t, 0, 0, 1);
    g_tess_out = &g_cache.tris;
#endif
    IntArr vs = { 0 }, ls = { 0 };
    ARR(double) coords = { 0 };
    for (int s = 0; s < m->sectors.n; s++) {
        map_sector_loops(m, s, &vs, &ls);
        g_cache.start[s] = g_cache.tris.n;
        if (vs.n >= 3) {
            arr_clear(coords);
            arr_reserve(coords, vs.n * 3);
            for (int i = 0; i < vs.n; i++) {
                coords.a[i * 3] = m->verts.a[vs.a[i]].x;
                coords.a[i * 3 + 1] = m->verts.a[vs.a[i]].y;
                coords.a[i * 3 + 2] = 0;
            }
            coords.n = vs.n * 3;
#ifdef SF_USE_GLU
            gluTessBeginPolygon(t, NULL);
            for (int L = 0; L + 1 < ls.n; L++) {
                if (ls.a[L + 1] - ls.a[L] < 3) continue;
                gluTessBeginContour(t);
                for (int i = ls.a[L]; i < ls.a[L + 1]; i++) gluTessVertex(t, &coords.a[i * 3], &coords.a[i * 3]);
                gluTessEndContour(t);
            }
            gluTessEndPolygon(t);
            for (int i = 0; i < g_tess_alloc.n; i++) free(g_tess_alloc.a[i]);
            arr_clear(g_tess_alloc);
#else
            double *xy = malloc(sizeof(double) * 2 * (size_t)vs.n);
            for (int i = 0; i < vs.n; i++) { xy[i * 2] = coords.a[i * 3]; xy[i * 2 + 1] = coords.a[i * 3 + 1]; }
            tess_odd(xy, ls.a, ls.n - 1, &g_cache.tris);
            free(xy);
#endif
        }
        g_cache.count[s] = (g_cache.tris.n - g_cache.start[s]) / 6;
    }
#ifdef SF_USE_GLU
    gluDeleteTess(t);
#endif
    arr_free(vs); arr_free(ls); arr_free(coords);
    g_cache.map = m;
    g_cache.rev = m->geo_rev;
    g_cache.valid = true;
}

static void ensure_cache(const Map *m) {
    if (!g_cache.valid || g_cache.map != m || g_cache.rev != m->geo_rev || g_cache.nsec != m->sectors.n) build_cache(m);
}

const float *r3d_sector_tris(const Map *m, int s, int *ntri) {
    ensure_cache(m);
    if (s < 0 || s >= g_cache.nsec) { *ntri = 0; return NULL; }
    *ntri = g_cache.count[s];
    return g_cache.tris.a + g_cache.start[s];
}

float r3d_sector_floor(const Map *m, const R3Opts *o, int s) { return o && o->floor_h ? o->floor_h[s] : m->sectors.a[s].floor_h; }
float r3d_sector_ceil(const Map *m, const R3Opts *o, int s) { return o && o->ceil_h ? o->ceil_h[s] : m->sectors.a[s].ceil_h; }

/* ------------------------------------------------------------------ camera */
static GLdouble g_mv[16], g_pr[16];
static GLint g_vp[4];
static float g_win_h, g_scale;

void r3d_cam_vectors(const Cam3 *c, V3 *f, V3 *r, V3 *u) {
    float cp = cosf(c->pitch), sp = sinf(c->pitch), cy = cosf(c->yaw), sy = sinf(c->yaw);
    if (f) *f = v3(cy * cp, sy * cp, sp);
    if (r) *r = v3(sy, -cy, 0);
    if (u) *u = v3(-cy * sp, -sy * sp, cp);
}

static void setup_view(const Cam3 *c, Rect vp, float scale, float win_h) {
    g_win_h = win_h; g_scale = scale;
    int x = (int)(vp.x * scale), y = (int)((win_h - vp.y - vp.h) * scale), w = (int)(vp.w * scale), h = (int)(vp.h * scale);
    glViewport(x, y, SF_MAX(w, 1), SF_MAX(h, 1));
    float aspect = vp.w / SF_MAX(vp.h, 1.0f);
    float hfov = (float)(c->fov * SF_DEG2RAD);
    float vfov = 2.0f * atanf(tanf(hfov * 0.5f) / aspect);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(vfov / SF_DEG2RAD, aspect, 2.0, 40000.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    V3 f;
    r3d_cam_vectors(c, &f, NULL, NULL);
    gluLookAt(c->x, c->y, c->z, c->x + f.x, c->y + f.y, c->z + f.z, 0, 0, 1);
    glGetDoublev(GL_MODELVIEW_MATRIX, g_mv);
    glGetDoublev(GL_PROJECTION_MATRIX, g_pr);
    glGetIntegerv(GL_VIEWPORT, g_vp);
}

bool r3d_project(V3 p, float *sx, float *sy) {
    GLdouble wx, wy, wz;
    if (!gluProject(p.x, p.y, p.z, g_mv, g_pr, g_vp, &wx, &wy, &wz)) return false;
    if (wz < 0 || wz > 1) return false;
    *sx = (float)(wx / g_scale);
    *sy = (float)(g_win_h - wy / g_scale);
    return true;
}

/* ------------------------------------------------------------------ surface emission */
typedef enum { EM_TEXTURED, EM_PICK, EM_OVERLAY } EmitMode;

static bool sel_has(const R3Opts *o, uint32_t id) {
    for (int i = 0; i < o->nsel; i++) if (o->sel[i] == id) return true;
    return false;
}

static float sec_light(const Map *m, const R3Opts *o, int s) {
    if (o->fullbright) return 1.0f;
    float l = o->light ? o->light[s] : (float)m->sectors.a[s].light;
    return SF_CLAMP(l / 255.0f, 0.0f, 1.0f);
}

static void pick_color(uint32_t id) {
    /* kind in top bits squeezed: 3 bits kind + 21 bits index */
    uint32_t packed = ((uint32_t)SURF_KIND(id) << 21) | ((uint32_t)SURF_IDX(id) & 0x1FFFFFu);
    glColor3ub((GLubyte)((packed >> 16) & 255), (GLubyte)((packed >> 8) & 255), (GLubyte)(packed & 255));
}

static void bind_tex(const char *name, float *tw, float *th) {
    const Tex *t = tex_get(name);
    glBindTexture(GL_TEXTURE_2D, t->id);
    *tw = (float)t->w; *th = (float)t->h;
    if (*tw <= 0) *tw = 64;
    if (*th <= 0) *th = 64;
}

static void emit_flat(const Map *m, const R3Opts *o, int s, bool ceil, EmitMode mode) {
    const MSector *S = &m->sectors.a[s];
    const char *tx = ceil ? S->ceil_tex : S->floor_tex;
    if (mode == EM_TEXTURED && tex_is_sky(tx)) return;
    int nt;
    const float *tr = r3d_sector_tris(m, s, &nt);
    if (!nt) return;
    float z = ceil ? r3d_sector_ceil(m, o, s) : r3d_sector_floor(m, o, s);
    float tw = 128, th = 128;
    if (mode == EM_TEXTURED) {
        bind_tex(tx, &tw, &th);
        float l = sec_light(m, o, s) * (ceil ? 0.92f : 1.0f);
        glColor3f(l, l, l);
        if (tex_is_liquid(tx)) {
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glTranslatef((float)fmod(o->time * 0.05, 1.0), (float)fmod(o->time * 0.03, 1.0), 0);
            glMatrixMode(GL_MODELVIEW);
        }
    } else if (mode == EM_PICK) pick_color(SURF_ID(ceil ? SURF_CEIL : SURF_FLOOR, s));
    float sc = ceil ? S->cscale : S->fscale, rot = (float)((ceil ? S->crot : S->frot) * SF_DEG2RAD);
    float ox = ceil ? S->cox : S->fox, oy = ceil ? S->coy : S->foy;
    if (sc <= 0) sc = 1;
    float cr = cosf(rot), sr = sinf(rot);
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < nt * 3; i++) {
        float x = tr[i * 2], y = tr[i * 2 + 1];
        if (mode == EM_TEXTURED) {
            float rx = x * cr - y * sr, ry = x * sr + y * cr;
            glTexCoord2f((rx + ox) / (tw * sc), -(ry + oy) / (th * sc));
        }
        glVertex3f(x, y, z);
    }
    glEnd();
    if (mode == EM_TEXTURED && tex_is_liquid(tx)) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
}

static void wall_quad(float ax, float ay, float bx, float by, float z0, float z1, float len, float tw, float th, float offx, float vtop, bool tex) {
    float u0 = offx / tw, u1 = (offx + len) / tw;
    float v0 = (vtop - z1) / th, v1 = (vtop - z0) / th;
    glBegin(GL_QUADS);
    if (tex) glTexCoord2f(u0, v0);
    glVertex3f(ax, ay, z1);
    if (tex) glTexCoord2f(u1, v0);
    glVertex3f(bx, by, z1);
    if (tex) glTexCoord2f(u1, v1);
    glVertex3f(bx, by, z0);
    if (tex) glTexCoord2f(u0, v1);
    glVertex3f(ax, ay, z0);
    glEnd();
}

/* emits one wall part; part = SURF_UPPER/MID/LOWER; returns false if that part is not present */
static bool emit_wall_part(const Map *m, const R3Opts *o, int li, int side, int part, EmitMode mode) {
    const MLine *L = &m->lines.a[li];
    const MSide *sd = &L->side[side];
    int s = sd->sector, os = L->side[1 - side].sector;
    if (s < 0) return false;
    const MVert *va = &m->verts.a[side == 0 ? L->v1 : L->v2], *vb = &m->verts.a[side == 0 ? L->v2 : L->v1];
    float ax = (float)va->x, ay = (float)va->y, bx = (float)vb->x, by = (float)vb->y;
    float len = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    float fl = r3d_sector_floor(m, o, s), cl = r3d_sector_ceil(m, o, s);
    float z0, z1, vtop;
    const char *tx;
    if (os < 0) {
        if (part != SURF_MID) return false;
        z0 = fl; z1 = cl; tx = sd->middle;
        vtop = (L->flags & LF_LOWER_UNPEG) ? fl + (float)tex_get(tx)->h : cl;
    } else {
        float of = r3d_sector_floor(m, o, os), oc = r3d_sector_ceil(m, o, os);
        if (part == SURF_LOWER) {
            if (of <= fl) return false;
            z0 = fl; z1 = of; tx = sd->lower;
            vtop = (L->flags & LF_LOWER_UNPEG) ? cl : of;
        } else if (part == SURF_UPPER) {
            if (oc >= cl) return false;
            if (mode == EM_TEXTURED && tex_is_sky(m->sectors.a[s].ceil_tex) && tex_is_sky(m->sectors.a[os].ceil_tex)) return false;
            z0 = oc; z1 = cl; tx = sd->upper;
            vtop = (L->flags & LF_UPPER_UNPEG) ? cl : oc + (float)tex_get(tx)->h;
        } else return false;
    }
    if (z1 <= z0) return false;
    float tw = 128, th = 128;
    if (mode == EM_TEXTURED) {
        bind_tex(tx, &tw, &th);
        float l = sec_light(m, o, s);
        if (!o->fullbright) {
            float dx = fabsf(bx - ax), dy = fabsf(by - ay);
            if (dy < dx * 0.08f) l *= 0.86f;
            else if (dx < dy * 0.08f) l *= 1.06f;
        }
        l = SF_MIN(l, 1.0f);
        glColor3f(l, l, l);
    } else if (mode == EM_PICK) pick_color(SURF_ID(part, li * 2 + side));
    wall_quad(ax, ay, bx, by, z0, z1, len, tw, th, sd->offx, vtop + sd->offy, mode == EM_TEXTURED);
    return true;
}

static void emit_surface(const Map *m, const R3Opts *o, uint32_t id, EmitMode mode) {
    int k = SURF_KIND(id), i = SURF_IDX(id);
    if (k == SURF_FLOOR || k == SURF_CEIL) { if (i < m->sectors.n) emit_flat(m, o, i, k == SURF_CEIL, mode); }
    else if (k >= SURF_UPPER && k <= SURF_LOWER) { if (i / 2 < m->lines.n) emit_wall_part(m, o, i / 2, i & 1, k, mode); }
}

/* ------------------------------------------------------------------ things */
static int thing_sector(const Map *m, const MThing *t) { return map_sector_at(m, t->x, t->y); }

static void emit_things(const Map *m, const Cam3 *c, const R3Opts *o, EmitMode mode) {
    V3 r;
    r3d_cam_vectors(c, NULL, &r, NULL);
    const float k = 0.75f;
    for (int i = 0; i < m->things.n; i++) {
        const MThing *t = &m->things.a[i];
        if (t->type == TH_PLAYER && !o->show_player_start) continue;
        if (o->thing_hidden && o->thing_hidden[i]) continue;
        int s = thing_sector(m, t);
        float z = s >= 0 ? r3d_sector_floor(m, o, s) : 0;
        float w = SPRITE_W * k, h = SPRITE_H * k;
        bool dead = o->thing_dead && o->thing_dead[i];
        if (dead) { h *= 0.3f; }
        float x0 = (float)t->x - r.x * w * 0.5f, y0 = (float)t->y - r.y * w * 0.5f;
        float x1 = (float)t->x + r.x * w * 0.5f, y1 = (float)t->y + r.y * w * 0.5f;
        if (mode == EM_TEXTURED) {
            glBindTexture(GL_TEXTURE_2D, g_assets.sprites[t->type].id);
            float l = s >= 0 ? sec_light(m, o, s) : 1.0f;
            if (g_thing_info[t->type].category == TC_LIGHT) l = 1.0f;
            if (dead) l *= 0.45f;
            bool sel = o->editor && sel_has(o, SURF_ID(SURF_THING, i));
            bool hov = o->editor && o->hover == SURF_ID(SURF_THING, i);
            if (sel) glColor3f(1.0f, 0.62f * l + 0.2f, 0.3f * l);
            else if (hov) glColor3f(SF_MIN(1, l * 1.2f), SF_MIN(1, l * 1.2f), SF_MIN(1, l * 1.35f));
            else glColor3f(l, l, l);
        } else if (mode == EM_PICK) pick_color(SURF_ID(SURF_THING, i));
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex3f(x0, y0, z + h);
        glTexCoord2f(1, 0); glVertex3f(x1, y1, z + h);
        glTexCoord2f(1, 1); glVertex3f(x1, y1, z);
        glTexCoord2f(0, 1); glVertex3f(x0, y0, z);
        glEnd();
    }
}

/* ------------------------------------------------------------------ passes */
static void draw_sky(const Cam3 *c, Rect vp) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_assets.sky.id);
    glColor3f(1, 1, 1);
    float hfov = (float)(c->fov * SF_DEG2RAD);
    float u0 = -c->yaw / (float)(2 * SF_PI) * 2.0f;
    float du = hfov / (float)(2 * SF_PI) * 2.0f;
    float aspect = vp.w / SF_MAX(vp.h, 1.0f);
    float dv = du / aspect * 1.6f;
    float vc = 0.62f - c->pitch * 0.45f;
    glBegin(GL_QUADS);
    glTexCoord2f(u0 - du * 0.5f, vc - dv * 0.5f); glVertex2f(0, 0);
    glTexCoord2f(u0 + du * 0.5f, vc - dv * 0.5f); glVertex2f(1, 0);
    glTexCoord2f(u0 + du * 0.5f, vc + dv * 0.5f); glVertex2f(1, 1);
    glTexCoord2f(u0 - du * 0.5f, vc + dv * 0.5f); glVertex2f(0, 1);
    glEnd();
}

static void world_pass(const Map *m, const R3Opts *o, EmitMode mode) {
    for (int s = 0; s < m->sectors.n; s++) {
        emit_flat(m, o, s, false, mode);
        emit_flat(m, o, s, true, mode);
    }
    for (int i = 0; i < m->lines.n; i++)
        for (int k = 0; k < 2; k++)
            for (int p = SURF_UPPER; p <= SURF_LOWER; p++) emit_wall_part(m, o, i, k, p, mode);
}

void r3d_draw(const Map *m, const Cam3 *c, Rect vp, float scale, float win_h, const R3Opts *o) {
    ensure_cache(m);
    int sx = (int)(vp.x * scale), sy = (int)((win_h - vp.y - vp.h) * scale);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, (int)(vp.w * scale), (int)(vp.h * scale));
    glViewport(sx, sy, (int)(vp.w * scale), (int)(vp.h * scale));
    glClearColor(0.02f, 0.025f, 0.03f, 1);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    draw_sky(c, vp);
    setup_view(c, vp, scale, win_h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (!o->fullbright) {
        float fc[4] = { 0.02f, 0.025f, 0.03f, 1 };
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_EXP2);
        glFogf(GL_FOG_DENSITY, 0.00032f);
        glFogfv(GL_FOG_COLOR, fc);
        glHint(GL_FOG_HINT, GL_NICEST);
    }
    world_pass(m, o, EM_TEXTURED);
    /* sprites */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    emit_things(m, c, o, EM_TEXTURED);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    /* overlays: wireframe, selection, hover */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1, -2);
    glDepthMask(GL_FALSE);
    if (o->wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glColor4f(0.6f, 0.75f, 0.9f, 0.55f);
        world_pass(m, o, EM_OVERLAY);
        glDisable(GL_POLYGON_OFFSET_LINE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    if (o->editor) {
        for (int i = 0; i < o->nsel; i++) {
            if (SURF_KIND(o->sel[i]) == SURF_THING) continue;
            glColor4f(1.0f, 0.48f, 0.1f, 0.26f);
            emit_surface(m, o, o->sel[i], EM_OVERLAY);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(2.0f * scale);
            glColor4f(1.0f, 0.48f, 0.1f, 1.0f);
            emit_surface(m, o, o->sel[i], EM_OVERLAY);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        if (o->hover && SURF_KIND(o->hover) != SURF_THING && !sel_has(o, o->hover)) {
            glColor4f(0.3f, 0.64f, 1.0f, 0.16f);
            emit_surface(m, o, o->hover, EM_OVERLAY);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.5f * scale);
            glColor4f(0.3f, 0.64f, 1.0f, 0.9f);
            emit_surface(m, o, o->hover, EM_OVERLAY);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        glLineWidth(1.0f);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

uint32_t r3d_pick(const Map *m, const Cam3 *c, Rect vp, float scale, float win_h, float mx, float my, const R3Opts *o) {
    if (!rect_has(vp, mx, my)) return 0;
    ensure_cache(m);
    int px = (int)(mx * scale), py = (int)((win_h - my) * scale);
#ifdef SF_GLES
    sfgl_offscreen(true);   /* the window surface is multisampled; pick on a single-sample target */
#endif
    setup_view(c, vp, scale, win_h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(px, py, 1, 1);
    glDisable(SF_GL_MULTISAMPLE);
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    world_pass(m, o, EM_PICK);
    /* things: a depth-only pass shaped by sprite alpha, then an id colour pass on exactly those pixels */
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    emit_things(m, c, o, EM_TEXTURED);      /* depth only, shaped by alpha */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDepthFunc(GL_EQUAL);
    emit_things(m, c, o, EM_PICK);           /* colour where the sprite depth landed */
    glDepthFunc(GL_LEQUAL);
    GLubyte px3[4] = { 0 };
    glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px3);
#ifdef SF_GLES
    sfgl_offscreen(false);
#endif
    glEnable(GL_DITHER);
    glEnable(SF_GL_MULTISAMPLE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    uint32_t packed = ((uint32_t)px3[0] << 16) | ((uint32_t)px3[1] << 8) | px3[2];
    if (!packed) return 0;
    int kind = (int)(packed >> 21), idx = (int)(packed & 0x1FFFFF);
    if (kind <= SURF_NONE || kind > SURF_THING) return 0;
    return SURF_ID(kind, idx);
}
