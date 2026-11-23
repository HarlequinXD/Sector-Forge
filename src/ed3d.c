/* Sector Forge - 3D view: fly camera, surface picking, mouse-wheel heights, texturing */
#include "editor.h"
#include "assets.h"
#include "plat.h"

static Col C(uint32_t h) { return col_hex(h); }

void ed3d_sync_camera_from_2d(void) {
    double cx = ed.vx, cy = ed.vy;
    float yaw = (float)(SF_PI * 0.5);
    int s = -1;
    /* first visit: start where the player starts, looking the way they look */
    if (!ed.cam_valid)
        for (int i = 0; i < ed.map.things.n; i++)
            if (ed.map.things.a[i].type == TH_PLAYER) {
                const MThing *t = &ed.map.things.a[i];
                int ts = map_sector_at(&ed.map, t->x, t->y);
                if (ts >= 0) { cx = t->x; cy = t->y; s = ts; yaw = (float)(t->angle * SF_DEG2RAD); }
                break;
            }
    if (s < 0) s = map_sector_at(&ed.map, cx, cy);
    if (s < 0 && ed.map.sectors.n) {
        double lx, ly;
        map_sector_label_point(&ed.map, 0, &lx, &ly);
        cx = lx; cy = ly; s = 0;
    }
    float fl = s >= 0 ? ed.map.sectors.a[s].floor_h : 0, cl = s >= 0 ? ed.map.sectors.a[s].ceil_h : 128;
    ed.cam.x = (float)cx; ed.cam.y = (float)cy;
    ed.cam.z = SF_MIN(fl + 56, (fl + cl) * 0.5f + 16);
    ed.cam.yaw = yaw;
    ed.cam.pitch = -0.12f;
    ed.cam.fov = g_settings.fov;
    ed.cam_valid = true;
}

static bool sel_has(uint32_t id) {
    for (int i = 0; i < ed.sel3d.n; i++) if (ed.sel3d.a[i] == id) return true;
    return false;
}
static void sel_toggle(uint32_t id) {
    for (int i = 0; i < ed.sel3d.n; i++) if (ed.sel3d.a[i] == id) { arr_remove(ed.sel3d, i); return; }
    arr_push(ed.sel3d, id);
}

const char *ed3d_surface_texture(uint32_t id) {
    int k = SURF_KIND(id), i = SURF_IDX(id);
    Map *m = &ed.map;
    if (k == SURF_FLOOR && i < m->sectors.n) return m->sectors.a[i].floor_tex;
    if (k == SURF_CEIL && i < m->sectors.n) return m->sectors.a[i].ceil_tex;
    if (k >= SURF_UPPER && k <= SURF_LOWER && i / 2 < m->lines.n) {
        MSide *sd = &m->lines.a[i / 2].side[i & 1];
        return k == SURF_UPPER ? sd->upper : k == SURF_MID ? sd->middle : sd->lower;
    }
    return NULL;
}

static void set_surface_texture(uint32_t id, const char *name) {
    int k = SURF_KIND(id), i = SURF_IDX(id);
    Map *m = &ed.map;
    if (k == SURF_FLOOR && i < m->sectors.n) sf_strcpy(m->sectors.a[i].floor_tex, TEXNAME, name);
    else if (k == SURF_CEIL && i < m->sectors.n) sf_strcpy(m->sectors.a[i].ceil_tex, TEXNAME, name);
    else if (k >= SURF_UPPER && k <= SURF_LOWER && i / 2 < m->lines.n) {
        MSide *sd = &m->lines.a[i / 2].side[i & 1];
        sf_strcpy(k == SURF_UPPER ? sd->upper : k == SURF_MID ? sd->middle : sd->lower, TEXNAME, name);
    }
}

void ed3d_texture_applied(const char *name) {
    int n = 0;
    bool any = ed.sel3d.n > 0 || ed.hover3d;
    if (!any) {
        int cat = tex_find(name) >= 0 ? g_assets.tex.a[tex_find(name)].cat : TEXCAT_WALL;
        sf_strcpy(cat == TEXCAT_WALL ? ed.cur_wall : ed.cur_flat, TEXNAME, name);
        return;
    }
    ed_snapshot("Apply texture");
    if (ed.sel3d.n) {
        for (int i = 0; i < ed.sel3d.n; i++) if (SURF_KIND(ed.sel3d.a[i]) != SURF_THING) { set_surface_texture(ed.sel3d.a[i], name); n++; }
    } else if (ed.hover3d && SURF_KIND(ed.hover3d) != SURF_THING) { set_surface_texture(ed.hover3d, name); n++; }
    char msg[96];
    snprintf(msg, sizeof msg, "%s applied to %d surface%s", name, n, n == 1 ? "" : "s");
    ed_toast(msg, 1);
}

static void sector_of_surface(uint32_t id, int *out_s) {
    int k = SURF_KIND(id), i = SURF_IDX(id);
    *out_s = -1;
    if (k == SURF_FLOOR || k == SURF_CEIL) *out_s = i;
    else if (k >= SURF_UPPER && k <= SURF_LOWER && i / 2 < ed.map.lines.n) *out_s = ed.map.lines.a[i / 2].side[i & 1].sector;
}

static void wheel_heights(float dir, bool fine) {
    Map *m = &ed.map;
    int step = fine ? 1 : SF_MAX(ed.hstep, 1);
    double now = plat_time();
    uint32_t list[1];
    const uint32_t *targets = ed.sel3d.a;
    int nt = ed.sel3d.n;
    if (!nt || !sel_has(ed.hover3d)) {
        if (!nt && ed.hover3d) { list[0] = ed.hover3d; targets = list; nt = 1; }
    }
    if (!nt) return;
    if (now - ed.last_wheel_time > 0.7) ed_snapshot("Change height");
    else ed_mark_dirty();
    ed.last_wheel_time = now;
    uint32_t first = 0;
    for (int i = 0; i < nt; i++) {
        uint32_t id = targets[i];
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        if (k == SURF_FLOOR) {
            MSector *s = &m->sectors.a[idx];
            if (!first) { first = id; ed.wheel_label_from = ed.wheel_label_id == id && now - ed.wheel_label_time < 1.5 ? ed.wheel_label_from : s->floor_h; }
            s->floor_h = SF_MIN(s->floor_h + dir * step, s->ceil_h);
        } else if (k == SURF_CEIL) {
            MSector *s = &m->sectors.a[idx];
            if (!first) { first = id; ed.wheel_label_from = ed.wheel_label_id == id && now - ed.wheel_label_time < 1.5 ? ed.wheel_label_from : s->ceil_h; }
            s->ceil_h = SF_MAX(s->ceil_h + dir * step, s->floor_h);
        } else if (k >= SURF_UPPER && k <= SURF_LOWER) {
            MSide *sd = &m->lines.a[idx / 2].side[idx & 1];
            if (!first) { first = id; ed.wheel_label_from = sd->offy; }
            sd->offy -= dir * step;
        }
    }
    ed.wheel_label_id = first;
    ed.wheel_label_time = now;
}

static void nudge_offsets(float dx, float dy) {
    Map *m = &ed.map;
    if (!ed.sel3d.n) return;
    ed_snapshot("Align texture");
    for (int i = 0; i < ed.sel3d.n; i++) {
        uint32_t id = ed.sel3d.a[i];
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        if (k == SURF_FLOOR) { m->sectors.a[idx].fox += dx; m->sectors.a[idx].foy += dy; }
        else if (k == SURF_CEIL) { m->sectors.a[idx].cox += dx; m->sectors.a[idx].coy += dy; }
        else if (k >= SURF_UPPER && k <= SURF_LOWER) { m->lines.a[idx / 2].side[idx & 1].offx += dx; m->lines.a[idx / 2].side[idx & 1].offy += dy; }
    }
}

static void adjust_light(int d) {
    Map *m = &ed.map;
    if (!ed.sel3d.n && !ed.hover3d) return;
    ed_snapshot("Change light");
    uint8_t *done = calloc((size_t)(m->sectors.n ? m->sectors.n : 1), 1);
    int n = ed.sel3d.n ? ed.sel3d.n : 1;
    for (int i = 0; i < n; i++) {
        int s;
        sector_of_surface(ed.sel3d.n ? ed.sel3d.a[i] : ed.hover3d, &s);
        if (s < 0 || done[s]) continue;
        done[s] = 1;
        m->sectors.a[s].light = SF_CLAMP(m->sectors.a[s].light + d, 0, 255);
    }
    free(done);
}

static void draw_minimap(Rect r) {
    gfx_rrect(r, 8, col(10, 11, 13, 230));
    gfx_rrect_line(r, 8, 1, C(TH_BORDER2));
    gfx_text(FONT_SMALL, r.x + 8, r.y + 6, "OVERVIEW", C(0xA3ABB8));
    Rect in = rect(r.x + 6, r.y + 24, r.w - 12, r.h - 30);
    double x0, y0, x1, y1;
    if (!map_bounds(&ed.map, &x0, &y0, &x1, &y1)) return;
    double span = SF_MAX(x1 - x0, y1 - y0) * 1.1;
    if (span < 1) span = 1;
    double sc = SF_MIN(in.w, in.h) / span;
    double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
    gfx_clip_push(in);
#define MX(vv) (float)(in.x + in.w * 0.5 + ((vv)-cx) * sc)
#define MY(vv) (float)(in.y + in.h * 0.5 - ((vv)-cy) * sc)
    for (int i = 0; i < ed.map.lines.n; i++) {
        const MLine *l = &ed.map.lines.a[i];
        const MVert *a = &ed.map.verts.a[l->v1], *b = &ed.map.verts.a[l->v2];
        gfx_line(MX(a->x), MY(a->y), MX(b->x), MY(b->y), 1, l->side[1].sector >= 0 ? col_hex(0x4A505B) : col_hex(0x9AA3B0));
    }
    for (int i = 0; i < ed.sel3d.n; i++) {
        int s;
        sector_of_surface(ed.sel3d.a[i], &s);
        if (s < 0) continue;
        for (int k = 0; k < ed.map.lines.n; k++) {
            const MLine *l = &ed.map.lines.a[k];
            if (l->side[0].sector != s && l->side[1].sector != s) continue;
            const MVert *a = &ed.map.verts.a[l->v1], *b = &ed.map.verts.a[l->v2];
            gfx_line(MX(a->x), MY(a->y), MX(b->x), MY(b->y), 2, C(TH_ACCENT));
        }
    }
    float px = MX(ed.cam.x), py = MY(ed.cam.y);
    float hf = (float)(ed.cam.fov * SF_DEG2RAD * 0.5);
    float L = 26;
    gfx_tri(px, py, px + cosf(ed.cam.yaw + hf) * L, py - sinf(ed.cam.yaw + hf) * L, px + cosf(ed.cam.yaw - hf) * L, py - sinf(ed.cam.yaw - hf) * L, col(77, 163, 255, 70));
    gfx_circle(px, py, 4, C(TH_BLUE));
#undef MX
#undef MY
    gfx_clip_pop();
}

void ed3d_frame(Rect vp, const Input *in) {
    if (!ed.cam_valid) ed3d_sync_camera_from_2d();
    ed.cam.fov = g_settings.fov;
    if (ed.hstep <= 0) ed.hstep = 8;
    bool hov = ui_hover(vp);
    /* camera look */
    if (hov && in->mpressed[1]) { ed.looking = true; plat_set_relative_mouse(true); }
    if (ed.looking && !in->mdown[1]) { ed.looking = false; plat_set_relative_mouse(false); }
    if (ed.looking) {
        float s = 0.0024f * g_settings.mouse_sens;
        ed.cam.yaw -= in->mdx * s;
        ed.cam.pitch -= in->mdy * s * (g_settings.invert_y ? -1 : 1);
        ed.cam.pitch = SF_CLAMP(ed.cam.pitch, -1.55f, 1.55f);
        ui.cursor = CUR_NONE;
    }
    /* fly */
    if ((hov || ed.looking) && !ui.text_focus && !in->ctrl) {
        V3 f, r, u;
        r3d_cam_vectors(&ed.cam, &f, &r, &u);
        float sp = (in->shift ? 1500.0f : 520.0f) * (float)in->dt;
        V3 mv = v3(0, 0, 0);
        if (in->kdown['W']) mv = v3_add(mv, f);
        if (in->kdown['S']) mv = v3_sub(mv, f);
        if (in->kdown['D']) mv = v3_add(mv, r);
        if (in->kdown['A']) mv = v3_sub(mv, r);
        if (in->kdown['E']) mv.z += 1;
        if (in->kdown['Q']) mv.z -= 1;
        float l = v3_len(mv);
        if (l > 0) { mv = v3_scale(mv, sp / l); ed.cam.x += mv.x; ed.cam.y += mv.y; ed.cam.z += mv.z; }
        if (ed.looking && in->wheel != 0) { /* wheel while looking adjusts fly speed feel: dolly */
            ed.cam.x += f.x * in->wheel * 64; ed.cam.y += f.y * in->wheel * 64; ed.cam.z += f.z * in->wheel * 64;
        }
    }
    R3Opts o;
    memset(&o, 0, sizeof o);
    o.editor = true;
    o.fullbright = ed.fullbright;
    o.wireframe = ed.wireframe;
    o.show_player_start = true;
    o.sel = ed.sel3d.a;
    o.nsel = ed.sel3d.n;
    o.time = in->time;
    /* picking */
    if (hov && !ed.looking) ed.hover3d = r3d_pick(&ed.map, &ed.cam, vp, in->scale, in->win_h, in->mx, in->my, &o);
    else if (!ed.looking) ed.hover3d = 0;
    o.hover = ed.hover3d;
    /* interaction */
    if (hov && !ed.looking) {
        if (in->mpressed[0]) {
            uint32_t h = ed.hover3d;
            if (in->alt || ed.tool3d == T3_PICK) {
                const char *t = h ? ed3d_surface_texture(h) : NULL;
                if (t && t[0]) {
                    int k = SURF_KIND(h);
                    sf_strcpy(k == SURF_FLOOR || k == SURF_CEIL ? ed.cur_flat : ed.cur_wall, TEXNAME, t);
                    char msg[64];
                    snprintf(msg, sizeof msg, "Picked %s", t);
                    ed_toast(msg, 0);
                }
            } else if (ed.tool3d == T3_PAINT) {
                if (h && SURF_KIND(h) != SURF_THING) {
                    int k = SURF_KIND(h);
                    const char *t = (k == SURF_FLOOR || k == SURF_CEIL) ? ed.cur_flat : ed.cur_wall;
                    ed_snapshot("Paint surface");
                    set_surface_texture(h, t);
                }
            } else {
                if (in->ctrl) { if (h) sel_toggle(h); }
                else if (in->shift) { if (h && !sel_has(h)) arr_push(ed.sel3d, h); }
                else { arr_clear(ed.sel3d); if (h) arr_push(ed.sel3d, h); }
                if (h && in->mdouble[0]) ed.panel = PANEL_PROPS;
            }
        }
        if (in->wheel != 0) {
            if (ed.tool3d == T3_LIGHT) adjust_light(in->wheel > 0 ? 16 : -16);
            else wheel_heights(in->wheel > 0 ? 1.0f : -1.0f, in->ctrl);
        }
    }
    if (!ui.text_focus && ed.mode == MODE_3D) {
        float st = in->shift ? 8.0f : 1.0f;
        if (in->kpressed[KEY_LEFT]) nudge_offsets(-st, 0);
        if (in->kpressed[KEY_RIGHT]) nudge_offsets(st, 0);
        if (in->kpressed[KEY_UP]) nudge_offsets(0, -st);
        if (in->kpressed[KEY_DOWN]) nudge_offsets(0, st);
        if (in->kpressed[KEY_PLUS] || in->kpressed[KEY_NUMPLUS] || in->kpressed[KEY_PGUP]) adjust_light(16);
        if (in->kpressed[KEY_MINUS] || in->kpressed[KEY_NUMMINUS] || in->kpressed[KEY_PGDN]) adjust_light(-16);
        if (in->kpressed[KEY_ESC]) arr_clear(ed.sel3d);
        if (in->kpressed['P'] && ed.hover3d && SURF_KIND(ed.hover3d) != SURF_THING) {
            int k = SURF_KIND(ed.hover3d);
            ed_snapshot("Paste texture");
            set_surface_texture(ed.hover3d, (k == SURF_FLOOR || k == SURF_CEIL) ? ed.cur_flat : ed.cur_wall);
        }
    }
    /* validate selection against map changes */
    for (int i = 0; i < ed.sel3d.n; i++) {
        uint32_t id = ed.sel3d.a[i];
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        bool ok = (k == SURF_FLOOR || k == SURF_CEIL) ? idx < ed.map.sectors.n : k == SURF_THING ? idx < ed.map.things.n : idx / 2 < ed.map.lines.n;
        if (!ok) { arr_remove(ed.sel3d, i); i--; }
    }
    o.sel = ed.sel3d.a;
    o.nsel = ed.sel3d.n;
    r3d_draw(&ed.map, &ed.cam, vp, in->scale, in->win_h, &o);
    gfx_begin_2d(in->win_w, in->win_h, in->scale);

    /* overlays */
    float x = vp.x + 12, y = vp.y + 12, w;
    static const char *tn[T3_COUNT] = { "3D View \xC2\xB7 Surface edit", "Fly camera", "Raise / Lower", "Apply texture", "Pick texture", "Align texture", "Sector light" };
    ui_chip(x, y, IC_cube, tn[ed.tool3d], true, &w); x += w + 6;
    char t[64];
    int nf = 0, nc = 0, nth = 0;
    for (int i = 0; i < ed.sel3d.n; i++) {
        int k = SURF_KIND(ed.sel3d.a[i]);
        if (k == SURF_FLOOR) nf++; else if (k == SURF_CEIL) nc++; else if (k == SURF_THING) nth++;
    }
    if (ed.sel3d.n) {
        if (ed.sel3d.n == 1) snprintf(t, sizeof t, "1 %s selected", nf ? "floor" : nc ? "ceiling" : nth ? "thing" : "wall");
        else snprintf(t, sizeof t, "%d surfaces selected", ed.sel3d.n);
        ui_chip(x, y, nf ? IC_floor : nc ? IC_ceiling : nth ? IC_thing : IC_wall, t, false, &w); x += w + 6;
    }
    ui_chip(x, y, IC_light, ed.fullbright ? "Fullbright" : "Lit", false, &w);
    /* minimap */
    draw_minimap(rect(vp.x + vp.w - 190, vp.y + 12, 178, 132));
    /* hover tag */
    if (ed.hover3d && !ed.looking && hov) {
        int k = SURF_KIND(ed.hover3d), idx = SURF_IDX(ed.hover3d);
        char lab[96];
        const char *names[] = { "", "Floor", "Ceiling", "Upper wall", "Wall", "Lower wall", "Thing" };
        if (k == SURF_THING) snprintf(lab, sizeof lab, "%s \xC2\xB7 #%d", g_thing_info[ed.map.things.a[idx].type].name, idx);
        else {
            const char *tx = ed3d_surface_texture(ed.hover3d);
            snprintf(lab, sizeof lab, "%s \xC2\xB7 %s", names[k], tx && tx[0] ? tx : "-");
        }
        float cw;
        ui_chip(in->mx + 16, in->my + 16, k == SURF_FLOOR ? IC_floor : k == SURF_CEIL ? IC_ceiling : k == SURF_THING ? IC_thing : IC_wall, lab, false, &cw);
    }
    /* wheel label */
    if (ed.wheel_label_id && in->time - ed.wheel_label_time < 1.6) {
        uint32_t id = ed.wheel_label_id;
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        float sx = in->mx + 18, sy = in->my - 70;
        char l1[64], l2[64];
        float now_v = 0;
        if (k == SURF_FLOOR || k == SURF_CEIL) {
            const MSector *S = &ed.map.sectors.a[idx];
            now_v = k == SURF_FLOOR ? S->floor_h : S->ceil_h;
            double lx, ly;
            map_sector_label_point(&ed.map, idx, &lx, &ly);
            float px, py;
            if (r3d_project(v3((float)lx, (float)ly, now_v), &px, &py) && rect_has(vp, px, py)) { sx = px + 16; sy = py - 30; }
            snprintf(l1, sizeof l1, "%s \xC2\xB7 SECTOR %d", k == SURF_FLOOR ? "FLOOR" : "CEILING", idx);
        } else {
            now_v = ed.map.lines.a[idx / 2].side[idx & 1].offy;
            snprintf(l1, sizeof l1, "WALL \xC2\xB7 TEXTURE OFFSET Y");
        }
        snprintf(l2, sizeof l2, "%g  \xE2\x86\x92  %g", ed.wheel_label_from, now_v);
        float bw = SF_MAX(gfx_text_w(FONT_UI_BOLD, l1), gfx_text_w(FONT_MONO, l2)) + 28;
        Rect b = rect(sx, sy, SF_MAX(bw, 214), 62);
        if (b.x + b.w > vp.x + vp.w - 8) b.x = vp.x + vp.w - 8 - b.w;
        if (b.y < vp.y + 8) b.y = vp.y + 8;
        float a = (float)SF_CLAMP((1.6 - (in->time - ed.wheel_label_time)) * 3, 0, 1);
        gfx_rrect(b, 8, col(18, 10, 4, (int)(240 * a)));
        gfx_rrect_line(b, 8, 1, col(255, 122, 26, (int)(255 * a)));
        gfx_text(FONT_UI_BOLD, b.x + 12, b.y + 8, l1, col(255, 178, 122, (int)(255 * a)));
        gfx_text(FONT_MONO, b.x + 12, b.y + 26, l2, col(241, 243, 246, (int)(255 * a)));
        char l3[64];
        snprintf(l3, sizeof l3, "wheel \xE2\x86\x95 step %d \xC2\xB7 ctrl+wheel step 1", ed.hstep);
        gfx_text(FONT_SMALL, b.x + 12, b.y + 44, l3, col(201, 169, 143, (int)(255 * a)));
    }
    /* hints */
    const char *hints[][2] = { { "Wheel", "raise / lower" }, { "Click", "select" }, { "RMB", "look \xC2\xB7 WASD fly" }, { "Alt", "pick texture" }, { "Arrows", "align" }, { "Tab", "2D" } };
    x = vp.x + 12;
    y = vp.y + vp.h - 34;
    for (int i = 0; i < SF_COUNT(hints); i++) {
        float kw = gfx_text_w(FONT_MONO_SMALL, hints[i][0]) + 10, tw = gfx_text_w(FONT_SMALL, hints[i][1]);
        Rect r = rect(x, y, kw + tw + 22, 24);
        if (r.x + r.w > vp.x + vp.w - 12) break;
        gfx_rrect(r, 6, col(10, 11, 13, 220));
        gfx_rrect_line(r, 6, 1, C(TH_BORDER2));
        float kwo;
        ui_kbd(x + 6, y + 3, hints[i][0], &kwo);
        gfx_text(FONT_SMALL, x + 12 + kwo, y + (24 - gfx_font_h(FONT_SMALL)) * 0.5f, hints[i][1], C(0xCFD5DE));
        x += r.w + 6;
    }
    if (ed.looking) {
        float cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f;
        gfx_line(cx - 8, cy, cx + 8, cy, 1.5f, col(255, 255, 255, 180));
        gfx_line(cx, cy - 8, cx, cy + 8, 1.5f, col(255, 255, 255, 180));
    }
}
