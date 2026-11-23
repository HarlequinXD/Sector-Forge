/* Sector Forge - 2D map view: rendering and all sector-building tools */
#include "editor.h"
#include "assets.h"
#include "glinc.h"

static uint8_t *g_tmask;
static float *g_orig_ta;
static D2 g_drag_ref;
static bool g_drag_armed;
static int g_click_item;
static bool g_measure_show;
static D2 g_measure_a, g_measure_b;
static uint32_t g_ctx_menu_id;
static double *g_lab_x, *g_lab_y;
static unsigned g_lab_rev;
static int g_lab_n = -1;
static bool g_space_pan;

#define PICK_V 9.0f
#define PICK_L 7.0f

D2 ed2d_world_to_screen(double x, double y) {
    return d2(ed.view.x + ed.view.w * 0.5 + (x - ed.vx) * ed.zoom, ed.view.y + ed.view.h * 0.5 - (y - ed.vy) * ed.zoom);
}
D2 ed2d_screen_to_world(float sx, float sy) {
    return d2(ed.vx + (sx - ed.view.x - ed.view.w * 0.5) / ed.zoom, ed.vy - (sy - ed.view.y - ed.view.h * 0.5) / ed.zoom);
}

static Col C(uint32_t h) { return col_hex(h); }

/* ------------------------------------------------------------------ hover / snapping */
static int nearest_vertex(float sx, float sy, float rad) {
    int best = -1;
    float bd = rad;
    for (int i = 0; i < ed.map.verts.n; i++) {
        D2 p = ed2d_world_to_screen(ed.map.verts.a[i].x, ed.map.verts.a[i].y);
        float d = (float)sqrt((p.x - sx) * (p.x - sx) + (p.y - sy) * (p.y - sy));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int nearest_line(double wx, double wy, float rad_px) {
    int best = -1;
    double bd = rad_px / ed.zoom;
    for (int i = 0; i < ed.map.lines.n; i++) {
        const MLine *l = &ed.map.lines.a[i];
        const MVert *a = &ed.map.verts.a[l->v1], *b = &ed.map.verts.a[l->v2];
        double t, d = seg_dist(wx, wy, a->x, a->y, b->x, b->y, &t);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static int thing_at(double wx, double wy) {
    int best = -1;
    double bd = 1e30;
    for (int i = 0; i < ed.map.things.n; i++) {
        const MThing *t = &ed.map.things.a[i];
        double r = SF_MAX(g_thing_info[t->type].radius, 7.0 / ed.zoom);
        double d = sqrt((t->x - wx) * (t->x - wx) + (t->y - wy) * (t->y - wy));
        if (d < r && d < bd) { bd = d; best = i; }
    }
    return best;
}

static D2 compute_snap(float sx, float sy, int *kind) {
    D2 w = ed2d_screen_to_world(sx, sy);
    *kind = 0;
    if (ed.snap_geo) {
        int v = nearest_vertex(sx, sy, PICK_V);
        if (v >= 0) { *kind = 2; return d2(ed.map.verts.a[v].x, ed.map.verts.a[v].y); }
    }
    if (ed.snap) {
        D2 g = d2(ed_snap_value(w.x), ed_snap_value(w.y));
        D2 gs = ed2d_world_to_screen(g.x, g.y);
        *kind = 1;
        /* prefer a grid point on a nearby line */
        if (ed.snap_geo) {
            int l = nearest_line(w.x, w.y, PICK_L);
            if (l >= 0) {
                const MLine *L = &ed.map.lines.a[l];
                const MVert *a = &ed.map.verts.a[L->v1], *b = &ed.map.verts.a[L->v2];
                double t, d = seg_dist(g.x, g.y, a->x, a->y, b->x, b->y, &t);
                if (d * ed.zoom > 1.0 || sqrt((gs.x - sx) * (gs.x - sx) + (gs.y - sy) * (gs.y - sy)) > PICK_L * 2) {
                    /* project onto line, keep the cursor-side grid coordinate where it reads cleanly */
                    seg_dist(w.x, w.y, a->x, a->y, b->x, b->y, &t);
                    t = SF_CLAMP(t, 0.0, 1.0);
                    D2 p = d2(a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t);
                    if (fabs(b->x - a->x) < 1e-9) p.y = ed_snap_value(p.y);
                    else if (fabs(b->y - a->y) < 1e-9) p.x = ed_snap_value(p.x);
                    *kind = 3;
                    return p;
                }
            }
        }
        return g;
    }
    if (ed.snap_geo) {
        int l = nearest_line(w.x, w.y, PICK_L);
        if (l >= 0) {
            const MLine *L = &ed.map.lines.a[l];
            const MVert *a = &ed.map.verts.a[L->v1], *b = &ed.map.verts.a[L->v2];
            double t;
            seg_dist(w.x, w.y, a->x, a->y, b->x, b->y, &t);
            t = SF_CLAMP(t, 0.0, 1.0);
            *kind = 3;
            return d2(a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t);
        }
    }
    return d2(floor(w.x + 0.5), floor(w.y + 0.5));
}

static void update_hover(float sx, float sy) {
    D2 w = ed2d_screen_to_world(sx, sy);
    ed.hv_vert = nearest_vertex(sx, sy, PICK_V);
    ed.hv_line = nearest_line(w.x, w.y, PICK_L);
    ed.hv_sector = map_sector_at(&ed.map, w.x, w.y);
    ed.hv_thing = ed.show_things ? thing_at(w.x, w.y) : -1;
}

static int hovered_item(void) {
    switch (ed.selmode) {
    case SM_VERTS: return ed.hv_vert;
    case SM_LINES: return ed.hv_line;
    case SM_SECTORS: return ed.hv_sector;
    default: return ed.hv_thing;
    }
}

static uint8_t *item_sel(int i) {
    switch (ed.selmode) {
    case SM_VERTS: return &ed.map.verts.a[i].sel;
    case SM_LINES: return &ed.map.lines.a[i].sel;
    case SM_SECTORS: return &ed.map.sectors.a[i].sel;
    default: return &ed.map.things.a[i].sel;
    }
}

/* ------------------------------------------------------------------ transforms */
static void free_transform(void) {
    free(ed.orig_v); ed.orig_v = NULL;
    free(ed.orig_t); ed.orig_t = NULL;
    free(ed.move_mask); ed.move_mask = NULL;
    free(g_tmask); g_tmask = NULL;
    free(g_orig_ta); g_orig_ta = NULL;
}

static bool begin_transform(D2 start) {
    Map *m = &ed.map;
    free_transform();
    int nv = m->verts.n, nt = m->things.n;
    ed.move_mask = calloc((size_t)(nv ? nv : 1), 1);
    map_collect_sel_verts(m, ed.selmode, ed.move_mask);
    g_tmask = calloc((size_t)(nt ? nt : 1), 1);
    for (int i = 0; i < nt; i++) {
        const MThing *t = &m->things.a[i];
        if (ed.selmode == SM_THINGS) g_tmask[i] = t->sel;
        else if (ed.selmode == SM_SECTORS) { int s = map_sector_at(m, t->x, t->y); g_tmask[i] = s >= 0 && m->sectors.a[s].sel; }
    }
    ed.orig_v = malloc(sizeof(D2) * (size_t)(nv ? nv : 1));
    ed.orig_t = malloc(sizeof(D2) * (size_t)(nt ? nt : 1));
    g_orig_ta = malloc(sizeof(float) * (size_t)(nt ? nt : 1));
    bool any = false;
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    double bestd = 1e30;
    g_drag_ref = start;
    for (int i = 0; i < nv; i++) {
        ed.orig_v[i] = d2(m->verts.a[i].x, m->verts.a[i].y);
        if (!ed.move_mask[i]) continue;
        any = true;
        x0 = SF_MIN(x0, m->verts.a[i].x); x1 = SF_MAX(x1, m->verts.a[i].x); y0 = SF_MIN(y0, m->verts.a[i].y); y1 = SF_MAX(y1, m->verts.a[i].y);
        double d = d2_dist(ed.orig_v[i], start);
        if (d < bestd) { bestd = d; g_drag_ref = ed.orig_v[i]; }
    }
    for (int i = 0; i < nt; i++) {
        ed.orig_t[i] = d2(m->things.a[i].x, m->things.a[i].y);
        g_orig_ta[i] = m->things.a[i].angle;
        if (!g_tmask[i]) continue;
        any = true;
        x0 = SF_MIN(x0, m->things.a[i].x); x1 = SF_MAX(x1, m->things.a[i].x); y0 = SF_MIN(y0, m->things.a[i].y); y1 = SF_MAX(y1, m->things.a[i].y);
        double d = d2_dist(ed.orig_t[i], start);
        if (d < bestd) { bestd = d; g_drag_ref = ed.orig_t[i]; }
    }
    ed.norig_v = nv; ed.norig_t = nt;
    ed.drag_center = d2(ed_snap_value((x0 + x1) * 0.5), ed_snap_value((y0 + y1) * 0.5));
    if (!any) free_transform();
    return any;
}

static void apply_transform(int kind, D2 start, D2 cur, bool alt) {
    Map *m = &ed.map;
    if (!ed.orig_v || ed.norig_v != m->verts.n || ed.norig_t != m->things.n) return;
    double dx = 0, dy = 0, ang = 0, sc = 1;
    if (kind == DRAG_MOVE) {
        D2 r = d2(g_drag_ref.x + (cur.x - start.x), g_drag_ref.y + (cur.y - start.y));
        if (ed.snap && !alt) r = d2(ed_snap_value(r.x), ed_snap_value(r.y));
        dx = r.x - g_drag_ref.x; dy = r.y - g_drag_ref.y;
    } else if (kind == DRAG_ROTATE) {
        double a0 = atan2(start.y - ed.drag_center.y, start.x - ed.drag_center.x), a1 = atan2(cur.y - ed.drag_center.y, cur.x - ed.drag_center.x);
        ang = a1 - a0;
        if (!alt) ang = floor(ang / (15 * SF_DEG2RAD) + 0.5) * 15 * SF_DEG2RAD;
    } else if (kind == DRAG_SCALE) {
        double d0 = d2_dist(start, ed.drag_center), d1 = d2_dist(cur, ed.drag_center);
        sc = d0 > 1e-6 ? d1 / d0 : 1;
        if (!alt) sc = floor(sc * 20 + 0.5) / 20.0;
        if (sc < 0.05) sc = 0.05;
    }
    double ca = cos(ang), sa = sin(ang);
    for (int i = 0; i < m->verts.n; i++) {
        if (!ed.move_mask[i]) continue;
        D2 o = ed.orig_v[i];
        if (kind == DRAG_MOVE) { m->verts.a[i].x = o.x + dx; m->verts.a[i].y = o.y + dy; }
        else {
            double x = o.x - ed.drag_center.x, y = o.y - ed.drag_center.y;
            if (kind == DRAG_ROTATE) { double nx = x * ca - y * sa, ny = x * sa + y * ca; x = nx; y = ny; }
            else { x *= sc; y *= sc; }
            m->verts.a[i].x = floor((x + ed.drag_center.x) * 8 + 0.5) / 8.0;
            m->verts.a[i].y = floor((y + ed.drag_center.y) * 8 + 0.5) / 8.0;
        }
    }
    for (int i = 0; i < m->things.n; i++) {
        if (!g_tmask[i]) continue;
        D2 o = ed.orig_t[i];
        MThing *t = &m->things.a[i];
        if (kind == DRAG_MOVE) { t->x = o.x + dx; t->y = o.y + dy; }
        else {
            double x = o.x - ed.drag_center.x, y = o.y - ed.drag_center.y;
            if (kind == DRAG_ROTATE) { double nx = x * ca - y * sa, ny = x * sa + y * ca; x = nx; y = ny; t->angle = fmodf(g_orig_ta[i] + (float)(ang / SF_DEG2RAD) + 720, 360); }
            else { x *= sc; y *= sc; }
            t->x = x + ed.drag_center.x; t->y = y + ed.drag_center.y;
        }
    }
    m->geo_rev++;
}

static void end_transform(void) {
    if (ed.move_mask && ed.norig_v == ed.map.verts.n) map_finalize_move(&ed.map, ed.move_mask);
    free_transform();
    r3d_invalidate();
}

/* ------------------------------------------------------------------ box selection */
static void box_select(D2 a, D2 b, bool add, bool toggle) {
    Map *m = &ed.map;
    double x0 = SF_MIN(a.x, b.x), x1 = SF_MAX(a.x, b.x), y0 = SF_MIN(a.y, b.y), y1 = SF_MAX(a.y, b.y);
#define INSIDE(px, py) ((px) >= x0 && (px) <= x1 && (py) >= y0 && (py) <= y1)
    if (!add && !toggle) map_select_none(m);
    if (ed.selmode == SM_VERTS)
        for (int i = 0; i < m->verts.n; i++) { if (INSIDE(m->verts.a[i].x, m->verts.a[i].y)) m->verts.a[i].sel = toggle ? !m->verts.a[i].sel : 1; }
    if (ed.selmode == SM_LINES)
        for (int i = 0; i < m->lines.n; i++) {
            const MVert *p = &m->verts.a[m->lines.a[i].v1], *q = &m->verts.a[m->lines.a[i].v2];
            if (INSIDE(p->x, p->y) && INSIDE(q->x, q->y)) m->lines.a[i].sel = toggle ? !m->lines.a[i].sel : 1;
        }
    if (ed.selmode == SM_SECTORS) {
        uint8_t *ok = malloc((size_t)(m->sectors.n ? m->sectors.n : 1));
        memset(ok, 1, (size_t)(m->sectors.n ? m->sectors.n : 1));
        uint8_t *has = calloc((size_t)(m->sectors.n ? m->sectors.n : 1), 1);
        for (int i = 0; i < m->lines.n; i++) {
            const MLine *l = &m->lines.a[i];
            const MVert *p = &m->verts.a[l->v1], *q = &m->verts.a[l->v2];
            bool in = INSIDE(p->x, p->y) && INSIDE(q->x, q->y);
            for (int k = 0; k < 2; k++) if (l->side[k].sector >= 0) { has[l->side[k].sector] = 1; if (!in) ok[l->side[k].sector] = 0; }
        }
        for (int i = 0; i < m->sectors.n; i++) if (ok[i] && has[i]) m->sectors.a[i].sel = toggle ? !m->sectors.a[i].sel : 1;
        free(ok); free(has);
    }
    if (ed.selmode == SM_THINGS)
        for (int i = 0; i < m->things.n; i++) if (INSIDE(m->things.a[i].x, m->things.a[i].y)) m->things.a[i].sel = toggle ? !m->things.a[i].sel : 1;
#undef INSIDE
}

/* ------------------------------------------------------------------ drawing tools */
static void finish_draw(bool closed_request) {
    int n = ed.draw.n;
    if (ed.tool == T_DRAW) {
        if (n >= 3) {
            ed_snapshot("Draw sector");
            MapDefaults d = ed.proj.def;
            sf_strcpy(d.wall_tex, TEXNAME, ed.cur_wall);
            int before = ed.map.sectors.n;
            map_draw_shape(&ed.map, ed.draw.a, n, true, &d);
            map_select_none(&ed.map);
            ed.selmode = SM_SECTORS;
            /* select sectors created by this shape */
            for (int i = 0; i < ed.map.sectors.n; i++) if (i >= before) ed.map.sectors.a[i].sel = 1;
            if (map_sel_count(&ed.map, SM_SECTORS) == 0) {
                double cx = 0, cy = 0;
                for (int i = 0; i < n; i++) { cx += ed.draw.a[i].x; cy += ed.draw.a[i].y; }
                int s = map_sector_at(&ed.map, cx / n, cy / n);
                if (s >= 0) ed.map.sectors.a[s].sel = 1;
            }
            r3d_invalidate();
            ed_toast("Sector created", 1);
        } else if (n > 0) ed_toast("A sector needs at least 3 points", 0);
    } else if (ed.tool == T_LINES) {
        if (n >= 2) {
            ed_snapshot("Draw lines");
            MapDefaults d = ed.proj.def;
            sf_strcpy(d.wall_tex, TEXNAME, ed.cur_wall);
            if (closed_request && n >= 3) {
                D2 *tmp = malloc(sizeof(D2) * (size_t)(n + 1));
                memcpy(tmp, ed.draw.a, sizeof(D2) * (size_t)n);
                tmp[n] = ed.draw.a[0];
                map_draw_shape(&ed.map, tmp, n + 1, false, &d);
                free(tmp);
            } else map_draw_shape(&ed.map, ed.draw.a, n, false, &d);
            r3d_invalidate();
            ed_toast("Lines added", 1);
        }
    }
    ed.drawing = false;
    arr_clear(ed.draw);
}

static void make_rect(D2 a, D2 b) {
    if (fabs(a.x - b.x) < 1e-6 || fabs(a.y - b.y) < 1e-6) return;
    D2 p[4] = { d2(a.x, a.y), d2(a.x, b.y), d2(b.x, b.y), d2(b.x, a.y) };
    ed_snapshot("Draw rectangle");
    MapDefaults d = ed.proj.def;
    sf_strcpy(d.wall_tex, TEXNAME, ed.cur_wall);
    int before = ed.map.sectors.n;
    map_draw_shape(&ed.map, p, 4, true, &d);
    map_select_none(&ed.map);
    ed.selmode = SM_SECTORS;
    for (int i = before; i < ed.map.sectors.n; i++) ed.map.sectors.a[i].sel = 1;
    r3d_invalidate();
}

static int poly_points(D2 c, double r, int sides, D2 *out) {
    for (int k = 0; k < sides; k++) {
        double a = SF_PI * 0.5 + 2 * SF_PI * k / sides + (sides % 2 == 0 ? SF_PI / sides : 0);
        out[k] = d2(floor(c.x + cos(a) * r + 0.5), floor(c.y + sin(a) * r + 0.5));
    }
    return sides;
}

static void make_poly(D2 c, double r) {
    if (r < 2) return;
    D2 p[64];
    int n = poly_points(c, r, ed.poly_sides, p);
    ed_snapshot("Draw polygon");
    MapDefaults d = ed.proj.def;
    sf_strcpy(d.wall_tex, TEXNAME, ed.cur_wall);
    int before = ed.map.sectors.n;
    map_draw_shape(&ed.map, p, n, true, &d);
    map_select_none(&ed.map);
    ed.selmode = SM_SECTORS;
    for (int i = before; i < ed.map.sectors.n; i++) ed.map.sectors.a[i].sel = 1;
    r3d_invalidate();
}

/* ------------------------------------------------------------------ context menu */
static const MenuItem g_ctx_items[] = {
    { "Cut", "Ctrl+X", IC_cut, false, false, false }, { "Copy", "Ctrl+C", IC_copy, false, false, false },
    { "Paste here", "Ctrl+V", IC_paste, false, false, false }, { "Duplicate", "Ctrl+D", IC_copy, false, false, false },
    { "Delete", "Del", IC_trash, false, false, false }, { NULL, NULL, -1, false, false, true },
    { "Join sectors", "J", IC_join, false, false, false }, { "Merge sectors", "Shift+J", IC_join, false, false, false },
    { "Flip lines", "Ctrl+F", IC_flip, false, false, false }, { NULL, NULL, -1, false, false, true },
    { "Mirror horizontally", "H", IC_flip, false, false, false }, { "Mirror vertically", "V", IC_flip_v, false, false, false },
    { "Rotate 90\xC2\xB0 clockwise", "Ctrl+]", IC_rotate, false, false, false }, { NULL, NULL, -1, false, false, true },
    { "Edit in 3D", "Tab", IC_cube, false, false, false },
};

static void handle_ctx_result(void) {
    int r = ui_popup_result(g_ctx_menu_id);
    switch (r) {
    case 0: ed_copy(true); break;
    case 1: ed_copy(false); break;
    case 2: ed_paste(); break;
    case 3: ed_duplicate(); break;
    case 4: ed_delete(); break;
    case 6: ed_join(false); break;
    case 7: ed_join(true); break;
    case 8: ed_flip_lines(); break;
    case 10: ed_mirror(false); break;
    case 11: ed_mirror(true); break;
    case 12: ed_rotate90(true); break;
    case 14: ed_set_mode(MODE_3D); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ input */
static void paint_at(double wx, double wy, bool shift, bool alt) {
    Map *m = &ed.map;
    int l = nearest_line(wx, wy, PICK_L);
    if (l >= 0) {
        MLine *L = &m->lines.a[l];
        const MVert *a = &m->verts.a[L->v1], *b = &m->verts.a[L->v2];
        double cr = (b->x - a->x) * (wy - a->y) - (b->y - a->y) * (wx - a->x);
        int side = cr < 0 ? 0 : 1;
        if (L->side[side].sector < 0) side = 0;
        MSide *sd = &L->side[side];
        if (alt) { sf_strcpy(ed.cur_wall, TEXNAME, sd->middle[0] ? sd->middle : sd->upper); ed_toast("Picked wall texture", 0); return; }
        ed_snapshot("Paint wall");
        sf_strcpy(sd->upper, TEXNAME, ed.cur_wall); sf_strcpy(sd->middle, TEXNAME, ed.cur_wall); sf_strcpy(sd->lower, TEXNAME, ed.cur_wall);
        return;
    }
    int s = map_sector_at(m, wx, wy);
    if (s < 0) return;
    if (alt) { sf_strcpy(ed.cur_flat, TEXNAME, shift ? m->sectors.a[s].ceil_tex : m->sectors.a[s].floor_tex); ed_toast("Picked flat texture", 0); return; }
    ed_snapshot(shift ? "Paint ceiling" : "Paint floor");
    sf_strcpy(shift ? m->sectors.a[s].ceil_tex : m->sectors.a[s].floor_tex, TEXNAME, ed.cur_flat);
}

static void handle_input(Rect vp, const Input *in, bool hov) {
    Map *m = &ed.map;
    float mx = in->mx, my = in->my;
    D2 mw = ed2d_screen_to_world(mx, my);
    ed.mwx = mw.x; ed.mwy = mw.y;
    ed.snap_pt = compute_snap(mx, my, &ed.snap_kind);
    bool busy = ed.drag != DRAG_NONE;
    if (hov || busy) update_hover(mx, my);
    else { ed.hv_vert = ed.hv_line = ed.hv_sector = ed.hv_thing = -1; }

    /* zoom */
    if (hov && in->wheel != 0) {
        if ((ed.drag == DRAG_POLY || ed.tool == T_POLY) && in->shift) {
            ed.poly_sides = SF_CLAMP(ed.poly_sides + (in->wheel > 0 ? 1 : -1), 3, 64);
        } else {
            double f = pow(1.18, in->wheel);
            double nz = SF_CLAMP(ed.zoom * f, 0.01, 32.0);
            ed.vx = mw.x - (mx - vp.x - vp.w * 0.5) / nz;
            ed.vy = mw.y + (my - vp.y - vp.h * 0.5) / nz;
            ed.zoom = nz;
        }
    }
    /* pan: middle mouse, space + left, or pan tool */
    g_space_pan = in->kdown[KEY_SPACE] && !ui.text_focus;
    if (hov && (in->mpressed[2] || ((ed.tool == T_PAN || g_space_pan) && in->mpressed[0]))) {
        ed.drag = DRAG_PAN; ed.pan_sx = mx; ed.pan_sy = my;
    }
    if (ed.drag == DRAG_PAN) {
        ed.vx -= (mx - ed.pan_sx) / ed.zoom;
        ed.vy += (my - ed.pan_sy) / ed.zoom;
        ed.pan_sx = mx; ed.pan_sy = my;
        ui.cursor = CUR_MOVE;
        if (!in->mdown[0] && !in->mdown[2]) ed.drag = DRAG_NONE;
        return;
    }
    if (!hov && !busy) return;

    D2 sp = ed.snap_pt;
    bool shift = in->shift, ctrl = in->ctrl, alt = in->alt;

    switch (ed.tool) {
    case T_DRAW: case T_LINES: {
        ui.cursor = CUR_CROSS;
        if (in->mpressed[0] && hov) {
            if (ed.draw.n >= 3 && d2_dist(sp, ed.draw.a[0]) < 1e-6 * SF_MAX(1, ed.grid)) { finish_draw(true); break; }
            if (in->mdouble[0] && ed.draw.n >= 2) { finish_draw(ed.tool == T_DRAW); break; }
            if (!ed.draw.n || d2_dist(sp, arr_last(ed.draw)) > 1e-6) { arr_push(ed.draw, sp); ed.drawing = true; }
        }
        if (in->mpressed[1] && hov) {
            if (ed.draw.n) { ed.draw.n--; if (!ed.draw.n) ed.drawing = false; }
        }
        if (!ui.text_focus) {
            if (in->kpressed[KEY_ENTER] && ed.drawing) finish_draw(ed.tool == T_DRAW);
            if (in->kpressed[KEY_BACK] && ed.draw.n) { ed.draw.n--; if (!ed.draw.n) ed.drawing = false; }
            if (in->kpressed[KEY_ESC] && ed.drawing) { ed.drawing = false; arr_clear(ed.draw); }
        }
    } break;
    case T_RECT: {
        ui.cursor = CUR_CROSS;
        if (in->mpressed[0] && hov) {
            if (ed.drawing) { make_rect(ed.rect_a, sp); ed.drawing = false; }
            else { ed.rect_a = sp; ed.drag = DRAG_RECT; }
        }
        if (ed.drag == DRAG_RECT && in->mreleased[0]) {
            ed.drag = DRAG_NONE;
            if (d2_dist(ed.rect_a, sp) < 1e-6) ed.drawing = true;
            else make_rect(ed.rect_a, sp);
        }
        if (in->kpressed[KEY_ESC] && !ui.text_focus) { ed.drawing = false; ed.drag = DRAG_NONE; }
    } break;
    case T_POLY: {
        ui.cursor = CUR_CROSS;
        if (!ui.text_focus) {
            if (in->kpressed[KEY_PLUS] || in->kpressed[KEY_NUMPLUS]) ed.poly_sides = SF_MIN(64, ed.poly_sides + 1);
            if (in->kpressed[KEY_MINUS] || in->kpressed[KEY_NUMMINUS]) ed.poly_sides = SF_MAX(3, ed.poly_sides - 1);
        }
        if (in->mpressed[0] && hov) { ed.rect_a = sp; ed.drag = DRAG_POLY; }
        if (ed.drag == DRAG_POLY && in->mreleased[0]) { ed.drag = DRAG_NONE; make_poly(ed.rect_a, d2_dist(ed.rect_a, sp)); }
        if (in->kpressed[KEY_ESC] && !ui.text_focus) ed.drag = DRAG_NONE;
    } break;
    case T_VERTEX: case T_SPLIT: {
        ui.cursor = ed.hv_line >= 0 ? CUR_HAND : CUR_CROSS;
        if (in->mpressed[0] && hov && ed.hv_line >= 0) {
            MLine *L = &m->lines.a[ed.hv_line];
            const MVert *a = &m->verts.a[L->v1], *b = &m->verts.a[L->v2];
            D2 p;
            if (ed.tool == T_SPLIT) p = d2((a->x + b->x) * 0.5, (a->y + b->y) * 0.5);
            else {
                double t;
                seg_dist(sp.x, sp.y, a->x, a->y, b->x, b->y, &t);
                t = SF_CLAMP(t, 0.0, 1.0);
                p = d2(a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t);
            }
            ed_snapshot(ed.tool == T_SPLIT ? "Split line" : "Insert vertex");
            int li = ed.hv_line;
            int v = map_find_vertex(m, p.x, p.y, MAP_EPS);
            if (v < 0) {
                MVert nv = { p.x, p.y, 0, 0 };
                arr_push(m->verts, nv);
                v = m->verts.n - 1;
                map_split_line(m, li, v);
            }
            map_select_none(m);
            m->verts.a[v].sel = 1;
            ed.selmode = SM_VERTS;
            r3d_invalidate();
        }
    } break;
    case T_THING: {
        ui.cursor = ed.hv_thing >= 0 ? CUR_HAND : CUR_CROSS;
        if (in->mpressed[0] && hov) {
            if (ed.hv_thing >= 0) {
                if (!shift && !m->things.a[ed.hv_thing].sel) map_select_none(m);
                m->things.a[ed.hv_thing].sel = 1;
                ed.selmode = SM_THINGS;
                ed.drag = DRAG_MOVE; g_drag_armed = true; ed.drag_moved = false;
                ed.drag_start = mw;
            } else {
                ed_snapshot("Place thing");
                MThing t;
                memset(&t, 0, sizeof t);
                t.x = sp.x; t.y = sp.y; t.type = ed.thing_type;
                t.angle = ed.thing_type == TH_PLAYER ? 90 : 0;
                map_select_none(m);
                t.sel = 1;
                arr_push(m->things, t);
                ed.selmode = SM_THINGS;
                ed.drag = DRAG_THING_ANGLE; ed.drag_start = sp;
            }
        }
    } break;
    case T_MEASURE: {
        ui.cursor = CUR_CROSS;
        if (in->mpressed[0] && hov) { ed.drag = DRAG_MEASURE; g_measure_a = sp; g_measure_show = true; }
        if (ed.drag == DRAG_MEASURE) { g_measure_b = sp; if (in->mreleased[0]) ed.drag = DRAG_NONE; }
    } break;
    case T_PAINT: {
        ui.cursor = CUR_HAND;
        if (in->mpressed[0] && hov) paint_at(mw.x, mw.y, shift, alt);
    } break;
    case T_MOVE: case T_ROTATE: case T_SCALE: {
        ui.cursor = ed.tool == T_MOVE ? CUR_MOVE : CUR_CROSS;
        if (in->mpressed[0] && hov) {
            if (!map_sel_count(m, ed.selmode)) {
                int it = hovered_item();
                if (it >= 0) { map_select_none(m); *item_sel(it) = 1; }
            }
            if (begin_transform(mw)) {
                ed.drag = ed.tool == T_MOVE ? DRAG_MOVE : ed.tool == T_ROTATE ? DRAG_ROTATE : DRAG_SCALE;
                ed.drag_start = mw; ed.drag_moved = false; g_drag_armed = false;
            } else ed_toast("Select something to transform first", 0);
        }
    } break;
    default: { /* select */
        int it = hovered_item();
        if (it >= 0 && ed.drag == DRAG_NONE) ui.cursor = CUR_HAND;
        if (in->mpressed[0] && hov) {
            ed.drag_start = mw;
            ed.drag_cur = mw;
            ed.drag_moved = false;
            g_click_item = it;
            if (it >= 0) {
                uint8_t *s = item_sel(it);
                if (ctrl) *s = !*s;
                else if (shift) *s = 1;
                else if (!*s) { map_select_none(m); *s = 1; }
                if (*s) { ed.drag = DRAG_MOVE; g_drag_armed = true; }
                if (in->mdouble[0]) ed.panel = PANEL_PROPS;
            } else {
                ed.drag = DRAG_BOX;
            }
        }
        if (in->mpressed[1] && hov) {
            int it2 = hovered_item();
            if (it2 >= 0 && !*item_sel(it2)) { map_select_none(m); *item_sel(it2) = 1; }
            g_ctx_menu_id = ui_id("ctx2d", 0);
            ui_open_menu_at(g_ctx_menu_id, mx, my, g_ctx_items, SF_COUNT(g_ctx_items));
        }
    } break;
    }

    /* drag state machine */
    if (ed.drag == DRAG_BOX) {
        ed.drag_cur = mw;
        if (in->mreleased[0]) {
            D2 a = ed2d_world_to_screen(ed.drag_start.x, ed.drag_start.y);
            if (fabs(a.x - mx) < 3 && fabs(a.y - my) < 3) { if (!shift && !ctrl) map_select_none(m); }
            else box_select(ed.drag_start, mw, shift, ctrl);
            ed.drag = DRAG_NONE;
        }
    } else if (ed.drag == DRAG_MOVE || ed.drag == DRAG_ROTATE || ed.drag == DRAG_SCALE) {
        D2 a = ed2d_world_to_screen(ed.drag_start.x, ed.drag_start.y);
        if (!ed.drag_moved && (fabs(a.x - mx) > 3 || fabs(a.y - my) > 3)) {
            if (g_drag_armed) {
                if (!begin_transform(ed.drag_start)) { ed.drag = DRAG_NONE; return; }
                g_drag_armed = false;
            }
            ed_snapshot(ed.drag == DRAG_MOVE ? "Move" : ed.drag == DRAG_ROTATE ? "Rotate" : "Scale");
            ed.drag_moved = true;
        }
        if (ed.drag_moved) apply_transform(ed.drag, ed.drag_start, mw, alt);
        if (in->mreleased[0]) {
            if (ed.drag_moved) end_transform();
            else free_transform();
            ed.drag = DRAG_NONE;
        }
        if (in->kpressed[KEY_ESC] && ed.drag_moved) {
            apply_transform(DRAG_MOVE, ed.drag_start, ed.drag_start, true);
            free_transform();
            ed_undo();
            ed.drag = DRAG_NONE;
        }
    } else if (ed.drag == DRAG_THING_ANGLE) {
        if (m->things.n) {
            MThing *t = &arr_last(m->things);
            D2 a = ed2d_world_to_screen(ed.drag_start.x, ed.drag_start.y);
            if (fabs(a.x - mx) > 6 || fabs(a.y - my) > 6) {
                double ang = atan2(mw.y - t->y, mw.x - t->x) / SF_DEG2RAD;
                t->angle = (float)fmod(floor(ang / 45 + 0.5) * 45 + 360, 360);
            }
        }
        if (in->mreleased[0]) ed.drag = DRAG_NONE;
    }
    if (ed.drag == DRAG_RECT || ed.drag == DRAG_POLY) {
        if (!in->mdown[0] && !in->mreleased[0]) ed.drag = DRAG_NONE;
    }
}

/* ------------------------------------------------------------------ rendering */
static void ensure_labels(void) {
    if (g_lab_n == ed.map.sectors.n && g_lab_rev == ed.map.geo_rev) return;
    free(g_lab_x); free(g_lab_y);
    g_lab_n = ed.map.sectors.n;
    g_lab_x = malloc(sizeof(double) * (size_t)(g_lab_n ? g_lab_n : 1));
    g_lab_y = malloc(sizeof(double) * (size_t)(g_lab_n ? g_lab_n : 1));
    for (int s = 0; s < g_lab_n; s++) map_sector_label_point(&ed.map, s, &g_lab_x[s], &g_lab_y[s]);
    g_lab_rev = ed.map.geo_rev;
}

static void draw_grid(Rect vp) {
    gfx_rect(vp, C(TH_VIEW));
    if (!ed.show_grid) return;
    D2 w0 = ed2d_screen_to_world(vp.x, vp.y + vp.h), w1 = ed2d_screen_to_world(vp.x + vp.w, vp.y);
    double g = ed.grid;
    double gpx = g * ed.zoom;
    int major = SF_MAX(ed.major, 1);
    while (gpx < 6) { g *= 2; gpx = g * ed.zoom; }
    long ix0 = (long)floor(w0.x / g), ix1 = (long)ceil(w1.x / g);
    long iy0 = (long)floor(w0.y / g), iy1 = (long)ceil(w1.y / g);
    Col minor = col_hex(0x181B21), majc = col_hex(0x272B33);
    for (long i = ix0; i <= ix1; i++) {
        double x = i * g;
        D2 s = ed2d_world_to_screen(x, 0);
        long gi = (long)llround(x / ed.grid);
        bool maj = gi % major == 0;
        gfx_rect(rect((float)floor(s.x), vp.y, 1, vp.h), maj ? majc : minor);
    }
    for (long i = iy0; i <= iy1; i++) {
        double y = i * g;
        D2 s = ed2d_world_to_screen(0, y);
        long gi = (long)llround(y / ed.grid);
        bool maj = gi % major == 0;
        gfx_rect(rect(vp.x, (float)floor(s.y), vp.w, 1), maj ? majc : minor);
    }
    D2 o = ed2d_world_to_screen(0, 0);
    if (o.x >= vp.x && o.x <= vp.x + vp.w) gfx_rect(rect((float)floor(o.x), vp.y, 1.5f, vp.h), col_hex(0x3A2A20));
    if (o.y >= vp.y && o.y <= vp.y + vp.h) gfx_rect(rect(vp.x, (float)floor(o.y), vp.w, 1.5f), col_hex(0x3A2A20));
}

static void fill_sector(int s, Col c, bool textured) {
    int nt;
    const float *tr = r3d_sector_tris(&ed.map, s, &nt);
    if (!nt) return;
    if (textured) {
        const MSector *S = &ed.map.sectors.a[s];
        const Tex *t = tex_get(S->floor_tex);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, t->id);
        float tw = (float)SF_MAX(t->w, 1), th = (float)SF_MAX(t->h, 1);
        float sc = S->fscale > 0 ? S->fscale : 1, rot = (float)(S->frot * SF_DEG2RAD), cr = cosf(rot), sr = sinf(rot);
        glColor4ub(c.r, c.g, c.b, c.a);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < nt * 3; i++) {
            float x = tr[i * 2], y = tr[i * 2 + 1];
            float rx = x * cr - y * sr, ry = x * sr + y * cr;
            D2 p = ed2d_world_to_screen(x, y);
            glTexCoord2f((rx + S->fox) / (tw * sc), -(ry + S->foy) / (th * sc));
            glVertex2f((float)p.x, (float)p.y);
        }
        glEnd();
        glDisable(GL_TEXTURE_2D);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor4ub(c.r, c.g, c.b, c.a);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < nt * 3; i++) {
            D2 p = ed2d_world_to_screen(tr[i * 2], tr[i * 2 + 1]);
            glVertex2f((float)p.x, (float)p.y);
        }
        glEnd();
    }
}

static void length_tag(D2 a, D2 b, bool accent) {
    double len = d2_dist(a, b);
    if (len < 1e-6) return;
    D2 sa = ed2d_world_to_screen(a.x, a.y), sb = ed2d_world_to_screen(b.x, b.y);
    float mx = (float)(sa.x + sb.x) * 0.5f, my = (float)(sa.y + sb.y) * 0.5f;
    float dx = (float)(sb.x - sa.x), dy = (float)(sb.y - sa.y), l = sqrtf(dx * dx + dy * dy);
    if (l < 30) return;
    char t[32];
    if (fabs(len - floor(len + 0.5)) < 0.01) snprintf(t, sizeof t, "%d", (int)floor(len + 0.5));
    else snprintf(t, sizeof t, "%.1f", len);
    float nx = -dy / l * 14, ny = dx / l * 14;
    float w = gfx_text_w(FONT_MONO_SMALL, t) + 10, h = 18;
    Rect r = rect(mx + nx - w * 0.5f, my + ny - h * 0.5f, w, h);
    gfx_rrect(r, 4, accent ? col_hex(0x1A0D02) : col(10, 11, 13, 220));
    gfx_rrect_line(r, 4, 1, accent ? col_hex(TH_ACCENT) : col_hex(TH_BORDER2));
    gfx_text_center(FONT_MONO_SMALL, r, t, accent ? col_hex(TH_ACCENT_HI) : col_hex(TH_TEXT2));
}

static Col line_color(const MLine *l) {
    const Map *m = &ed.map;
    int s0 = l->side[0].sector, s1 = l->side[1].sector;
    bool special = (s0 >= 0 && (m->sectors.a[s0].type == ST_DOOR || m->sectors.a[s0].type == ST_LIFT)) ||
                   (s1 >= 0 && (m->sectors.a[s1].type == ST_DOOR || m->sectors.a[s1].type == ST_LIFT));
    if (special) return col_hex(0xB48CFF);
    if (s1 < 0 || s0 < 0) return col_hex(0xDFE3EA);
    if (l->flags & LF_BLOCK) return col_hex(0xE07A6A);
    return col_hex(0x6F7785);
}

static void draw_thing(const MThing *t, bool sel, bool hov) {
    const ThingInfo *ti = &g_thing_info[t->type];
    D2 p = ed2d_world_to_screen(t->x, t->y);
    float r = (float)SF_MAX(ti->radius * ed.zoom, 6);
    Col c = col_hex(ti->color);
    Col dark = col((int)(c.r * 0.18f), (int)(c.g * 0.18f), (int)(c.b * 0.18f), 235);
    gfx_circle((float)p.x, (float)p.y, r, dark);
    gfx_circle_line((float)p.x, (float)p.y, r, sel ? 2.5f : 1.8f, sel ? col_hex(TH_ACCENT) : hov ? col_hex(0xFFFFFF) : c);
    float a = (float)(t->angle * SF_DEG2RAD);
    float ex = (float)p.x + cosf(a) * (r + 5), ey = (float)p.y - sinf(a) * (r + 5);
    gfx_line((float)p.x + cosf(a) * r * 0.6f, (float)p.y - sinf(a) * r * 0.6f, ex, ey, 2, sel ? col_hex(TH_ACCENT) : c);
    if (r >= 9) {
        int icon = t->type == TH_PLAYER ? IC_player : ti->category == TC_ENEMY ? IC_enemy : ti->category == TC_LIGHT ? IC_light : ti->category == TC_PICKUP ? IC_item : IC_thing;
        gfx_icon(icon, (float)p.x, (float)p.y, SF_MIN(r * 1.25f, 22), sel ? col_hex(TH_ACCENT_HI) : c);
    }
}

static void render(Rect vp, const Input *in) {
    Map *m = &ed.map;
    gfx_clip_push(vp);
    draw_grid(vp);
    /* sector fills */
    for (int s = 0; s < m->sectors.n; s++) {
        bool tex = ed.textured_2d;
        Col c = tex ? col(92, 96, 104, 255) : col_hex(0x1A1D24);
        if (tex) {
            float l = 0.25f + 0.45f * (m->sectors.a[s].light / 255.0f);
            c = col((int)(255 * l), (int)(255 * l), (int)(255 * l), 255);
        }
        fill_sector(s, c, tex);
    }
    for (int s = 0; s < m->sectors.n; s++) {
        if (m->sectors.a[s].sel && ed.selmode == SM_SECTORS) fill_sector(s, col(255, 122, 26, 70), false);
        else if (s == ed.hv_sector && ed.selmode == SM_SECTORS && ed.tool == T_SELECT && ed.drag == DRAG_NONE) fill_sector(s, col(255, 255, 255, 22), false);
    }
    /* lines */
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        D2 a = ed2d_world_to_screen(m->verts.a[l->v1].x, m->verts.a[l->v1].y), b = ed2d_world_to_screen(m->verts.a[l->v2].x, m->verts.a[l->v2].y);
        if (SF_MAX(a.x, b.x) < vp.x - 4 || SF_MIN(a.x, b.x) > vp.x + vp.w + 4 || SF_MAX(a.y, b.y) < vp.y - 4 || SF_MIN(a.y, b.y) > vp.y + vp.h + 4) continue;
        bool sel = false;
        if (ed.selmode == SM_LINES) sel = l->sel;
        if (ed.selmode == SM_SECTORS) sel = (l->side[0].sector >= 0 && m->sectors.a[l->side[0].sector].sel) || (l->side[1].sector >= 0 && m->sectors.a[l->side[1].sector].sel);
        bool hov = ed.selmode == SM_LINES && i == ed.hv_line && (ed.tool == T_SELECT || ed.tool == T_PAINT);
        if ((ed.tool == T_VERTEX || ed.tool == T_SPLIT || ed.tool == T_PAINT) && i == ed.hv_line) hov = true;
        Col c = sel ? col_hex(TH_ACCENT) : hov ? col_hex(TH_ACCENT_HI) : line_color(l);
        float th = sel ? 2.4f : 1.6f;
        gfx_line((float)a.x, (float)a.y, (float)b.x, (float)b.y, th, c);
        /* front-side tick */
        float dx = (float)(b.x - a.x), dy = (float)(b.y - a.y), len = sqrtf(dx * dx + dy * dy);
        if (len > 16) {
            float mx = (float)(a.x + b.x) * 0.5f, my = (float)(a.y + b.y) * 0.5f;
            float nx = -dy / len, ny = dx / len; /* screen-space right of v1->v2 in y-up world */
            gfx_line(mx, my, mx + nx * 6, my + ny * 6, 1.3f, c);
        }
        if ((sel && ed.selmode == SM_LINES && ed.show_lengths) || hov) {
            length_tag(d2(m->verts.a[l->v1].x, m->verts.a[l->v1].y), d2(m->verts.a[l->v2].x, m->verts.a[l->v2].y), false);
        }
    }
    /* vertices */
    bool vmode = ed.selmode == SM_VERTS;
    if (ed.zoom > 0.12 || vmode) {
        for (int i = 0; i < m->verts.n; i++) {
            D2 p = ed2d_world_to_screen(m->verts.a[i].x, m->verts.a[i].y);
            if (!rect_has(vp, (float)p.x, (float)p.y)) continue;
            bool sel = vmode && m->verts.a[i].sel, hov = vmode && i == ed.hv_vert;
            float s = vmode ? 6.f : 4.f;
            if (sel) s = 8;
            gfx_rect(rect((float)p.x - s * 0.5f, (float)p.y - s * 0.5f, s, s), sel ? col_hex(TH_ACCENT_HI) : hov ? col_hex(0xFFFFFF) : col_hex(0x9FB0C6));
        }
    }
    /* labels */
    if (ed.show_heights && ed.zoom > 0.22) {
        ensure_labels();
        for (int s = 0; s < m->sectors.n; s++) {
            D2 p = ed2d_world_to_screen(g_lab_x[s], g_lab_y[s]);
            if (!rect_has(vp, (float)p.x, (float)p.y)) continue;
            const MSector *S = &m->sectors.a[s];
            bool sel = S->sel && ed.selmode == SM_SECTORS;
            Col c = sel ? col_hex(TH_ACCENT_HI) : col(200, 206, 216, 200);
            char h[48];
            snprintf(h, sizeof h, "F%g \xC2\xB7 C%g", S->floor_h, S->ceil_h);
            float y = (float)p.y - 8;
            if (S->name[0] && ed.zoom > 0.35) { float w = gfx_text_w(FONT_SMALL, S->name); gfx_text(FONT_SMALL, (float)p.x - w * 0.5f, y - 15, S->name, c); }
            float w = gfx_text_w(FONT_MONO_SMALL, h);
            gfx_rrect(rect((float)p.x - w * 0.5f - 4, y - 1, w + 8, 17), 4, col(8, 9, 11, 150));
            gfx_text(FONT_MONO_SMALL, (float)p.x - w * 0.5f, y, h, c);
            if (S->type != ST_NORMAL && ed.zoom > 0.3) {
                const char *tn = g_sector_type_names[S->type];
                float tw = gfx_text_w(FONT_SMALL, tn);
                gfx_text(FONT_SMALL, (float)p.x - tw * 0.5f, y + 17, tn, col_hex(0xB48CFF));
            }
        }
    }
    /* things */
    if (ed.show_things)
        for (int i = 0; i < m->things.n; i++) {
            const MThing *t = &m->things.a[i];
            D2 p = ed2d_world_to_screen(t->x, t->y);
            if (p.x < vp.x - 40 || p.x > vp.x + vp.w + 40 || p.y < vp.y - 40 || p.y > vp.y + vp.h + 40) continue;
            draw_thing(t, t->sel && ed.selmode == SM_THINGS, i == ed.hv_thing && ed.selmode == SM_THINGS);
        }
    /* box */
    if (ed.drag == DRAG_BOX) {
        D2 a = ed2d_world_to_screen(ed.drag_start.x, ed.drag_start.y), b = ed2d_world_to_screen(ed.drag_cur.x, ed.drag_cur.y);
        Rect r = rect((float)SF_MIN(a.x, b.x), (float)SF_MIN(a.y, b.y), (float)fabs(b.x - a.x), (float)fabs(b.y - a.y));
        gfx_rect(r, col(77, 163, 255, 18));
        gfx_rect_line(r, 1, col(77, 163, 255, 150));
    }
    /* drawing previews */
    D2 sp = ed.snap_pt;
    bool view_hot = rect_has(vp, in->mx, in->my);
    if ((ed.tool == T_DRAW || ed.tool == T_LINES) && ed.draw.n) {
        for (int i = 0; i + 1 < ed.draw.n; i++) {
            D2 a = ed2d_world_to_screen(ed.draw.a[i].x, ed.draw.a[i].y), b = ed2d_world_to_screen(ed.draw.a[i + 1].x, ed.draw.a[i + 1].y);
            gfx_line((float)a.x, (float)a.y, (float)b.x, (float)b.y, 2, col_hex(TH_ACCENT));
            if (ed.show_lengths) length_tag(ed.draw.a[i], ed.draw.a[i + 1], true);
        }
        D2 l = ed2d_world_to_screen(arr_last(ed.draw).x, arr_last(ed.draw).y), c = ed2d_world_to_screen(sp.x, sp.y);
        gfx_dashed((float)l.x, (float)l.y, (float)c.x, (float)c.y, 2, 7, 5, 0, col_hex(TH_ACCENT));
        if (ed.show_lengths) length_tag(arr_last(ed.draw), sp, true);
        if (ed.tool == T_DRAW && ed.draw.n >= 2) {
            D2 f = ed2d_world_to_screen(ed.draw.a[0].x, ed.draw.a[0].y);
            gfx_dashed((float)c.x, (float)c.y, (float)f.x, (float)f.y, 1.2f, 3, 5, 0, col(255, 122, 26, 110));
        }
        for (int i = 0; i < ed.draw.n; i++) {
            D2 p = ed2d_world_to_screen(ed.draw.a[i].x, ed.draw.a[i].y);
            bool first_hot = i == 0 && ed.draw.n >= 3 && d2_dist(sp, ed.draw.a[0]) < 1e-6;
            float s = first_hot ? 12 : 8;
            gfx_rect(rect((float)p.x - s * 0.5f, (float)p.y - s * 0.5f, s, s), first_hot ? col_hex(0xFFFFFF) : col_hex(TH_ACCENT));
        }
    }
    if (ed.tool == T_RECT && (ed.drag == DRAG_RECT || ed.drawing)) {
        D2 a = ed2d_world_to_screen(ed.rect_a.x, ed.rect_a.y), b = ed2d_world_to_screen(sp.x, sp.y);
        Rect r = rect((float)SF_MIN(a.x, b.x), (float)SF_MIN(a.y, b.y), (float)fabs(b.x - a.x), (float)fabs(b.y - a.y));
        gfx_rect(r, col(255, 122, 26, 30));
        gfx_dashed(r.x, r.y, r.x + r.w, r.y, 2, 7, 5, 0, col_hex(TH_ACCENT));
        gfx_dashed(r.x + r.w, r.y, r.x + r.w, r.y + r.h, 2, 7, 5, 0, col_hex(TH_ACCENT));
        gfx_dashed(r.x + r.w, r.y + r.h, r.x, r.y + r.h, 2, 7, 5, 0, col_hex(TH_ACCENT));
        gfx_dashed(r.x, r.y + r.h, r.x, r.y, 2, 7, 5, 0, col_hex(TH_ACCENT));
        length_tag(d2(ed.rect_a.x, SF_MAX(ed.rect_a.y, sp.y)), d2(sp.x, SF_MAX(ed.rect_a.y, sp.y)), true);
        length_tag(d2(SF_MAX(ed.rect_a.x, sp.x), ed.rect_a.y), d2(SF_MAX(ed.rect_a.x, sp.x), sp.y), true);
    }
    if (ed.tool == T_POLY) {
        D2 c = ed.drag == DRAG_POLY ? ed.rect_a : sp;
        double r = ed.drag == DRAG_POLY ? d2_dist(ed.rect_a, sp) : ed.grid * 2;
        if (ed.drag == DRAG_POLY || view_hot) {
            D2 pts[64];
            int n = poly_points(c, r, ed.poly_sides, pts);
            for (int i = 0; i < n; i++) {
                D2 a = ed2d_world_to_screen(pts[i].x, pts[i].y), b = ed2d_world_to_screen(pts[(i + 1) % n].x, pts[(i + 1) % n].y);
                if (ed.drag == DRAG_POLY) gfx_line((float)a.x, (float)a.y, (float)b.x, (float)b.y, 2, col_hex(TH_ACCENT));
                else gfx_dashed((float)a.x, (float)a.y, (float)b.x, (float)b.y, 1.2f, 4, 4, 0, col(255, 122, 26, 120));
            }
            D2 cs = ed2d_world_to_screen(c.x, c.y);
            char t[32];
            snprintf(t, sizeof t, "%d sides", ed.poly_sides);
            float cw;
            ui_chip((float)cs.x + 10, (float)cs.y + 10, IC_polygon, t, true, &cw);
            if (ed.drag == DRAG_POLY) length_tag(c, sp, true);
        }
    }
    if (g_measure_show && ed.tool == T_MEASURE) {
        D2 a = ed2d_world_to_screen(g_measure_a.x, g_measure_a.y), b = ed2d_world_to_screen(g_measure_b.x, g_measure_b.y);
        gfx_line((float)a.x, (float)a.y, (float)b.x, (float)b.y, 2, col_hex(TH_YELLOW));
        gfx_circle((float)a.x, (float)a.y, 4, col_hex(TH_YELLOW));
        gfx_circle((float)b.x, (float)b.y, 4, col_hex(TH_YELLOW));
        double len = d2_dist(g_measure_a, g_measure_b);
        double ang = atan2(g_measure_b.y - g_measure_a.y, g_measure_b.x - g_measure_a.x) / SF_DEG2RAD;
        char t[64];
        snprintf(t, sizeof t, "%.1f units \xC2\xB7 %.0f\xC2\xB0 \xC2\xB7 dx %.0f dy %.0f", len, ang, g_measure_b.x - g_measure_a.x, g_measure_b.y - g_measure_a.y);
        float cw;
        ui_chip((float)b.x + 12, (float)b.y + 8, IC_measure, t, true, &cw);
    }
    /* snap indicator + crosshair */
    if (view_hot && ed.drag != DRAG_PAN && ed.tool != T_SELECT && ed.tool != T_PAN && ed.tool != T_PAINT) {
        D2 s = ed2d_world_to_screen(sp.x, sp.y);
        Col cc = ed.snap_kind == 2 ? col_hex(TH_GREEN) : ed.snap_kind == 3 ? col_hex(TH_BLUE) : col_hex(0xFFFFFF);
        float x = (float)s.x, y = (float)s.y;
        gfx_line(x - 12, y, x - 4, y, 1.3f, cc); gfx_line(x + 4, y, x + 12, y, 1.3f, cc);
        gfx_line(x, y - 12, x, y - 4, 1.3f, cc); gfx_line(x, y + 4, x, y + 12, 1.3f, cc);
        if (ed.snap_kind == 2) gfx_rrect_line(rect(x - 6, y - 6, 12, 12), 2, 1.5f, cc);
    }
    gfx_clip_pop();
}

static void overlays(Rect vp, const Input *in) {
    static const char *tool_names[T_COUNT] = { "Select", "Pan", "Draw Sector", "Rectangle", "Polygon", "Draw Lines", "Insert Vertex", "Split Line",
                                               "Move", "Rotate", "Scale", "Place Thing", "Measure", "Paint Texture" };
    static const int tool_icons[T_COUNT] = { IC_select, IC_hand, IC_draw_sector, IC_rect, IC_polygon, IC_line, IC_vertex, IC_split,
                                             IC_move, IC_rotate, IC_scale, IC_thing, IC_measure, IC_bucket };
    float x = vp.x + 12, y = vp.y + 12, w;
    ui_chip(x, y, tool_icons[ed.tool], tool_names[ed.tool], true, &w); x += w + 6;
    ui_chip(x, y, IC_magnet, ed.snap ? "Snap on" : "Snap off", false, &w); x += w + 6;
    char z[32];
    snprintf(z, sizeof z, "%.0f%%", ed.zoom * 100);
    ui_chip(x, y, IC_zoom_in, z, false, &w); x += w + 6;
    char c[64];
    snprintf(c, sizeof c, "%.0f, %.0f", ed.snap_pt.x, ed.snap_pt.y);
    if (rect_has(vp, in->mx, in->my)) ui_chip(x, y, IC_crosshair, c, false, &w);

    /* hints */
    const char *hints[6][2];
    int nh = 0;
#define HINT(k, t) do { hints[nh][0] = k; hints[nh][1] = t; nh++; } while (0)
    switch (ed.tool) {
    case T_DRAW: HINT("Click", "add vertex"); HINT("Dbl-click", "close sector"); HINT("Bksp", "remove point"); HINT("Esc", "cancel"); break;
    case T_LINES: HINT("Click", "add point"); HINT("Enter", "finish"); HINT("RMB", "undo point"); HINT("Esc", "cancel"); break;
    case T_RECT: HINT("Drag", "draw rectangle"); HINT("Click+Click", "two corners"); break;
    case T_POLY: HINT("Drag", "radius"); HINT("Shift+Wheel", "sides"); HINT("+/\xE2\x88\x92", "sides"); break;
    case T_VERTEX: HINT("Click line", "insert vertex"); break;
    case T_SPLIT: HINT("Click line", "split at midpoint"); break;
    case T_THING: HINT("Click", "place"); HINT("Drag", "set angle"); HINT(",  .", "rotate 45\xC2\xB0"); break;
    case T_PAINT: HINT("Click", "paint floor / wall"); HINT("Shift", "ceiling"); HINT("Alt", "pick"); break;
    case T_MOVE: case T_ROTATE: case T_SCALE: HINT("Drag", "transform selection"); HINT("Alt", "free (no snap)"); break;
    case T_MEASURE: HINT("Drag", "measure"); break;
    default: HINT("Click", "select"); HINT("Drag", "move / box"); HINT("Shift", "add"); HINT("RMB", "menu"); HINT("Space", "pan"); break;
    }
#undef HINT
    x = vp.x + 12;
    y = vp.y + vp.h - 34;
    for (int i = 0; i < nh; i++) {
        float kw = gfx_text_w(FONT_MONO_SMALL, hints[i][0]) + 10, tw = gfx_text_w(FONT_SMALL, hints[i][1]);
        Rect r = rect(x, y, kw + tw + 22, 24);
        if (r.x + r.w > vp.x + vp.w - 170) break;
        gfx_rrect(r, 6, col(10, 11, 13, 220));
        gfx_rrect_line(r, 6, 1, col_hex(TH_BORDER2));
        float kwo;
        ui_kbd(x + 6, y + 3, hints[i][0], &kwo);
        gfx_text(FONT_SMALL, x + 12 + kwo, y + (24 - gfx_font_h(FONT_SMALL)) * 0.5f, hints[i][1], col_hex(0xCFD5DE));
        x += r.w + 6;
    }
    /* zoom controls */
    Rect zc = rect(vp.x + vp.w - 136, vp.y + vp.h - 40, 124, 32);
    gfx_rrect(zc, 8, col(10, 11, 13, 225));
    gfx_rrect_line(zc, 8, 1, col_hex(TH_BORDER2));
    float bx = zc.x + 2;
    if (ui_icon_btn(ui_id("zout", 0), rect(bx, zc.y + 2, 28, 28), IC_zoom_out, "Zoom out", false)) ed.zoom = SF_MAX(0.01, ed.zoom / 1.3);
    bx += 30;
    if (ui_icon_btn(ui_id("zin", 0), rect(bx, zc.y + 2, 28, 28), IC_zoom_in, "Zoom in", false)) ed.zoom = SF_MIN(32, ed.zoom * 1.3);
    bx += 30;
    if (ui_icon_btn(ui_id("zfit", 0), rect(bx, zc.y + 2, 28, 28), IC_fit, "Fit map (Home)", false)) ed_fit_view();
    bx += 30;
    if (ui_icon_btn(ui_id("ztex", 0), rect(bx, zc.y + 2, 28, 28), IC_layers, "Textured floors", ed.textured_2d)) ed.textured_2d = !ed.textured_2d;
}

static void grid_popup(Rect vp, const Input *in) {
    if (!ed.grid_popup) return;
    Rect r = rect(vp.x + vp.w - 272, vp.y + 12, 260, 352);
    int saved = ui.layer;
    ui.layer = LAYER_POPUP;
    ui_push_block(r, LAYER_POPUP);
    gfx_shadow(r, 10, 16, col(0, 0, 0, 150));
    gfx_rrect(r, 10, col_hex(TH_PANEL));
    gfx_rrect_line(r, 10, 1, col_hex(TH_BORDER2));
    float x = r.x + 14, y = r.y + 14;
    gfx_icon(IC_grid, x + 8, y + 9, 16, col_hex(0xFF8A33));
    gfx_text(FONT_UI_BOLD, x + 24, y + 9 - gfx_font_h(FONT_UI_BOLD) * 0.5f, "Grid Tool", col_hex(0xF1F3F6));
    float kw;
    ui_kbd(r.x + r.w - 54, y, "[", &kw);
    ui_kbd(r.x + r.w - 32, y, "]", &kw);
    if (ui_icon_btn(ui_id("gclose", 0), rect(r.x + r.w - 82, y - 4, 26, 26), IC_close, "Close", false)) ed.grid_popup = false;
    y += 32;
    gfx_text(FONT_SMALL, x, y, "Grid size (map units)", col_hex(TH_TEXT2));
    y += 20;
    static const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    float bw = (r.w - 28 - 4 * 4) / 5;
    for (int i = 0; i < 10; i++) {
        Rect b = rect(x + (i % 5) * (bw + 4), y + (i / 5) * 30, bw, 26);
        char t[8];
        snprintf(t, sizeof t, "%d", sizes[i]);
        if (ui_button_on(ui_id("gsz", i), b, -1, t, BTN_SEG, ed.grid == sizes[i])) ed.grid = sizes[i];
        if (ed.grid != sizes[i]) gfx_rrect_line(b, 6, 1, col_hex(TH_BORDER2));
    }
    y += 66;
    float hw = (r.w - 28 - 8) / 2;
    gfx_text(FONT_SMALL, x, y, "Custom", col_hex(TH_TEXT2));
    gfx_text(FONT_SMALL, x + hw + 8, y, "Major every", col_hex(TH_TEXT2));
    y += 18;
    float g = (float)ed.grid, mj = (float)ed.major;
    if (ui_number(ui_id("gcust", 0), rect(x, y, hw, 28), &g, 1, 0, 1, 4096, true)) ed.grid = (int)g;
    if (ui_number(ui_id("gmaj", 0), rect(x + hw + 8, y, hw, 28), &mj, 1, 0, 1, 64, true)) ed.major = (int)mj;
    y += 40;
    ui_toggle(ui_id("gt1", 0), rect(x, y, r.w - 28, 26), "Show grid", &ed.show_grid); y += 30;
    ui_toggle(ui_id("gt2", 0), rect(x, y, r.w - 28, 26), "Snap to grid", &ed.snap); y += 30;
    ui_toggle(ui_id("gt3", 0), rect(x, y, r.w - 28, 26), "Snap to vertices & lines", &ed.snap_geo); y += 30;
    ui_toggle(ui_id("gt4", 0), rect(x, y, r.w - 28, 26), "Show line lengths", &ed.show_lengths); y += 30;
    ui_toggle(ui_id("gt5", 0), rect(x, y, r.w - 28, 26), "Sector heights", &ed.show_heights);
    if (in->mpressed[0] && !rect_has(r, in->mx, in->my) && ui.top_layer <= LAYER_POPUP && !ui.popup_id) {
        /* clicking elsewhere closes the popover, except the toolbar grid button which toggles it */
        if (in->my > vp.y) ed.grid_popup = false;
    }
    ui.layer = saved;
}

void ed2d_frame(Rect vp, const Input *in) {
    ed.view = vp;
    if (ed.zoom <= 0) ed_fit_view();
    handle_ctx_result();
    bool hov = ui_hover(vp) && !(ed.grid_popup && rect_has(rect(vp.x + vp.w - 272, vp.y + 12, 260, 352), in->mx, in->my));
    handle_input(vp, in, hov);
    /* keyboard nudge */
    if (!ui.text_focus && ed.tool == T_SELECT && ed.drag == DRAG_NONE && map_sel_count(&ed.map, ed.selmode) &&
        (in->kpressed[KEY_LEFT] || in->kpressed[KEY_RIGHT] || in->kpressed[KEY_UP] || in->kpressed[KEY_DOWN]) && !in->ctrl) {
        double step = in->shift ? ed.grid * 4 : ed.grid;
        double dx = in->kpressed[KEY_LEFT] ? -step : in->kpressed[KEY_RIGHT] ? step : 0;
        double dy = in->kpressed[KEY_DOWN] ? -step : in->kpressed[KEY_UP] ? step : 0;
        if (begin_transform(d2(0, 0))) {
            ed_snapshot("Nudge");
            g_drag_ref = d2(0, 0);
            bool s = ed.snap;
            ed.snap = false;
            apply_transform(DRAG_MOVE, d2(0, 0), d2(dx, dy), true);
            ed.snap = s;
            end_transform();
        }
    }
    if (!ui.text_focus && ed.selmode == SM_THINGS && (in->kpressed[KEY_COMMA] || in->kpressed[KEY_PERIOD]) && map_sel_count(&ed.map, SM_THINGS)) {
        ed_snapshot("Rotate things");
        float d = in->kpressed[KEY_COMMA] ? 45.f : -45.f;
        for (int i = 0; i < ed.map.things.n; i++) if (ed.map.things.a[i].sel) ed.map.things.a[i].angle = fmodf(ed.map.things.a[i].angle + d + 360, 360);
    }
    render(vp, in);
    if (!in->touch) {   /* the touch shell draws its own chips, hints and grid sheet */
        overlays(vp, in);
        grid_popup(vp, in);
    }
}

/* ------------------------------------------------------------------ hooks for the touch shell */
void ed2d_finish_draw(bool closed) { finish_draw(closed); }

bool ed2d_item_at(float sx, float sy) {
    update_hover(sx, sy);
    return hovered_item() >= 0;
}

void ed2d_select_at(float sx, float sy) {
    update_hover(sx, sy);
    int it = hovered_item();
    if (it >= 0 && !*item_sel(it)) { map_select_none(&ed.map); *item_sel(it) = 1; }
}

void ed2d_zoom_about(float sx, float sy, double factor) {
    D2 w = ed2d_screen_to_world(sx, sy);
    double nz = SF_CLAMP(ed.zoom * factor, 0.01, 32.0);
    ed.vx = w.x - (sx - ed.view.x - ed.view.w * 0.5) / nz;
    ed.vy = w.y + (sy - ed.view.y - ed.view.h * 0.5) / nz;
    ed.zoom = nz;
}

void ed2d_pan_px(float dx, float dy) {
    ed.vx -= dx / ed.zoom;
    ed.vy += dy / ed.zoom;
}

void ed2d_render_view(Rect vp, const Input *in, double cx, double cy, double zoom) {
    Rect sv = ed.view;
    double svx = ed.vx, svy = ed.vy, sz = ed.zoom;
    ed.view = vp; ed.vx = cx; ed.vy = cy; ed.zoom = zoom;
    render(vp, in);
    ed.view = sv; ed.vx = svx; ed.vy = svy; ed.zoom = sz;
}
