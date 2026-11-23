/* Sector Forge - editor core: project/map lifecycle, history, selection commands */
#include "editor.h"
#include "assets.h"
#include "plat.h"

Editor ed;

void ed_toast(const char *msg, int kind) {
    sf_strcpy(ed.toast, sizeof ed.toast, msg);
    ed.toast_time = plat_time();
    ed.toast_kind = kind;
}

static void clear_history(void) {
    for (int i = 0; i < ed.undo.n; i++) free(ed.undo.a[i]);
    for (int i = 0; i < ed.redo.n; i++) free(ed.redo.a[i]);
    arr_clear(ed.undo);
    arr_clear(ed.redo);
}

static void reset_view_state(void) {
    ed.mode = MODE_2D;
    ed.tool = T_SELECT;
    ed.tool3d = T3_SELECT;
    ed.selmode = SM_SECTORS;
    ed.grid = g_settings.default_grid;
    ed.major = 8;
    ed.show_grid = ed.snap = ed.snap_geo = ed.show_lengths = ed.show_things = ed.show_heights = true;
    ed.textured_2d = g_settings.textured_2d;
    ed.poly_sides = 8;
    ed.drawing = false;
    arr_clear(ed.draw);
    ed.drag = DRAG_NONE;
    ed.cam_valid = false;
    arr_clear(ed.sel3d);
    ed.hover3d = 0;
    ed.thing_type = TH_IMP;
    ed.tex_target = TEXT_AUTO;
    ed.panel = PANEL_PROPS;
}

bool ed_load_map(int idx) {
    if (idx < 0 || idx >= ed.proj.nmaps) return false;
    char path[1024];
    project_map_path(&ed.proj, ed.proj.maps[idx].file, path, sizeof path);
    Map m;
    map_init(&m);
    if (!map_load(&m, path)) { map_free(&m); return false; }
    map_free(&ed.map);
    ed.map = m;
    if (!ed.map.name[0]) sf_strcpy(ed.map.name, sizeof ed.map.name, ed.proj.maps[idx].title);
    ed.map_idx = idx;
    ed.dirty = false;
    clear_history();
    ed.drawing = false;
    arr_clear(ed.draw);
    arr_clear(ed.sel3d);
    ed.cam_valid = false;
    if (ed.map.has_cam) {
        ed.cam.x = ed.map.cam_x; ed.cam.y = ed.map.cam_y; ed.cam.z = ed.map.cam_z;
        ed.cam.yaw = ed.map.cam_yaw; ed.cam.pitch = ed.map.cam_pitch;
        ed.cam_valid = true;
    }
    ed.cam.fov = g_settings.fov;
    sf_strcpy(ed.proj.last_map, sizeof ed.proj.last_map, ed.proj.maps[idx].file);
    project_save(&ed.proj);
    r3d_invalidate();
    ed_fit_view();
    ed.last_save_time = plat_time();
    return true;
}

bool ed_open_project(const char *file, char *err, size_t errcap) {
    Project p;
    if (!project_load(&p, file)) { snprintf(err, errcap, "Could not read the project file."); return false; }
    if (p.nmaps == 0) {
        ed.proj = p;
        if (project_add_map(&ed.proj, "E1M1", TPL_STARTER) < 0) { snprintf(err, errcap, "The project has no maps and one could not be created."); return false; }
        p = ed.proj;
    }
    ed.proj = p;
    assets_clear_project();
    char td[700];
    project_tex_dir(&ed.proj, td, sizeof td);
    assets_load_project_textures(td);
    reset_view_state();
    sf_strcpy(ed.cur_flat, TEXNAME, ed.proj.def.floor_tex);
    sf_strcpy(ed.cur_wall, TEXNAME, ed.proj.def.wall_tex);
    int idx = project_find_map(&ed.proj, ed.proj.last_map);
    if (idx < 0) idx = 0;
    if (!ed_load_map(idx)) { snprintf(err, errcap, "Could not load map %s.", ed.proj.maps[idx].file); return false; }
    ed.open = true;
    projects_remember(file);
    return true;
}

void ed_close_project(void) {
    if (ed.mode == MODE_PLAY) play_stop();
    ed.open = false;
    clear_history();
    map_free(&ed.map);
    map_init(&ed.map);
    assets_clear_project();
}

bool ed_save(void) {
    if (!ed.open) return false;
    if (ed.cam_valid) {
        ed.map.has_cam = true;
        ed.map.cam_x = ed.cam.x; ed.map.cam_y = ed.cam.y; ed.map.cam_z = ed.cam.z;
        ed.map.cam_yaw = ed.cam.yaw; ed.map.cam_pitch = ed.cam.pitch;
    }
    char path[1024];
    project_map_path(&ed.proj, ed.proj.maps[ed.map_idx].file, path, sizeof path);
    if (!map_save(&ed.map, path)) { ed_toast("Save failed \xE2\x80\x94 check the project folder is writable", 2); return false; }
    sf_strcpy(ed.proj.maps[ed.map_idx].title, sizeof ed.proj.maps[ed.map_idx].title, ed.map.name);
    project_save(&ed.proj);
    ed.dirty = false;
    ed.last_save_time = plat_time();
    return true;
}

void ed_mark_dirty(void) {
    ed.dirty = true;
    ed.last_edit_time = plat_time();
}

void ed_snapshot(const char *label) {
    Buf b = { 0 };
    map_write(&ed.map, &b);
    if (ed.undo.n >= 200) { free(ed.undo.a[0]); arr_remove(ed.undo, 0); }
    arr_push(ed.undo, b.s);
    for (int i = 0; i < ed.redo.n; i++) free(ed.redo.a[i]);
    arr_clear(ed.redo);
    sf_strcpy(ed.undo_label, sizeof ed.undo_label, label ? label : "Edit");
    ed_mark_dirty();
}

void ed_snapshot_merge(const char *label) {
    static double last;
    double now = plat_time();
    if (ed.undo.n && !strcmp(ed.undo_label, label) && now - last < 1.2) { last = now; ed_mark_dirty(); return; }
    last = now;
    ed_snapshot(label);
}

static void restore_from(char *txt) {
    Map m;
    map_init(&m);
    if (map_read(&m, txt)) {
        m.has_cam = ed.map.has_cam;
        map_free(&ed.map);
        ed.map = m;
        r3d_invalidate();
    } else map_free(&m);
    arr_clear(ed.sel3d);
    ed.drawing = false;
    arr_clear(ed.draw);
}

void ed_undo(void) {
    if (!ed.undo.n) { ed_toast("Nothing to undo", 0); return; }
    Buf b = { 0 };
    map_write(&ed.map, &b);
    arr_push(ed.redo, b.s);
    char *txt = ed.undo.a[--ed.undo.n];
    restore_from(txt);
    free(txt);
    ed_mark_dirty();
    ed_toast("Undo", 0);
}

void ed_redo(void) {
    if (!ed.redo.n) { ed_toast("Nothing to redo", 0); return; }
    Buf b = { 0 };
    map_write(&ed.map, &b);
    arr_push(ed.undo, b.s);
    char *txt = ed.redo.a[--ed.redo.n];
    restore_from(txt);
    free(txt);
    ed_mark_dirty();
    ed_toast("Redo", 0);
}

/* ------------------------------------------------------------------ selection */
void ed_select_none(void) { map_select_none(&ed.map); arr_clear(ed.sel3d); }

void ed_select_all(void) {
    Map *m = &ed.map;
    if (ed.mode == MODE_3D) {
        arr_clear(ed.sel3d);
        for (int i = 0; i < m->sectors.n; i++) arr_push(ed.sel3d, SURF_ID(SURF_FLOOR, i));
        return;
    }
    if (ed.selmode == SM_VERTS) for (int i = 0; i < m->verts.n; i++) m->verts.a[i].sel = 1;
    if (ed.selmode == SM_LINES) for (int i = 0; i < m->lines.n; i++) m->lines.a[i].sel = 1;
    if (ed.selmode == SM_SECTORS) for (int i = 0; i < m->sectors.n; i++) m->sectors.a[i].sel = 1;
    if (ed.selmode == SM_THINGS) for (int i = 0; i < m->things.n; i++) m->things.a[i].sel = 1;
}

void ed_invert_selection(void) {
    Map *m = &ed.map;
    if (ed.selmode == SM_VERTS) for (int i = 0; i < m->verts.n; i++) m->verts.a[i].sel ^= 1;
    if (ed.selmode == SM_LINES) for (int i = 0; i < m->lines.n; i++) m->lines.a[i].sel ^= 1;
    if (ed.selmode == SM_SECTORS) for (int i = 0; i < m->sectors.n; i++) m->sectors.a[i].sel ^= 1;
    if (ed.selmode == SM_THINGS) for (int i = 0; i < m->things.n; i++) m->things.a[i].sel ^= 1;
}

int ed_sel_sector_count(void) { return map_sel_count(&ed.map, SM_SECTORS); }

void ed_set_selmode(int sm) {
    if (sm == ed.selmode) return;
    /* carry the selection over where it makes sense */
    Map *m = &ed.map;
    if (ed.selmode == SM_SECTORS && sm == SM_LINES) {
        for (int i = 0; i < m->lines.n; i++) {
            MLine *l = &m->lines.a[i];
            l->sel = (l->side[0].sector >= 0 && m->sectors.a[l->side[0].sector].sel) || (l->side[1].sector >= 0 && m->sectors.a[l->side[1].sector].sel);
        }
    } else if ((ed.selmode == SM_SECTORS || ed.selmode == SM_LINES) && sm == SM_VERTS) {
        uint8_t *mask = calloc((size_t)(m->verts.n ? m->verts.n : 1), 1);
        map_collect_sel_verts(m, ed.selmode, mask);
        for (int i = 0; i < m->verts.n; i++) m->verts.a[i].sel = mask[i];
        free(mask);
    }
    ed.selmode = sm;
}

void ed_set_tool(int t) {
    if (ed.drawing) { ed.drawing = false; arr_clear(ed.draw); }
    ed.drag = DRAG_NONE;
    ed.tool = t;
    if (t == T_THING) ed.selmode = SM_THINGS;
}

void ed_set_mode(EdMode m) {
    if (m == ed.mode) return;
    if (ed.mode == MODE_PLAY) play_stop();
    if (m == MODE_PLAY) { play_start(ed.mode == MODE_3D); return; }
    if (m == MODE_3D) {
        if (!ed.cam_valid) ed3d_sync_camera_from_2d();
        /* turn the 2D sector selection into floor selections */
        arr_clear(ed.sel3d);
        for (int i = 0; i < ed.map.sectors.n; i++) if (ed.map.sectors.a[i].sel) arr_push(ed.sel3d, SURF_ID(SURF_FLOOR, i));
    }
    if (m == MODE_2D && ed.mode == MODE_3D) {
        bool any = false;
        for (int i = 0; i < ed.sel3d.n; i++) {
            int k = SURF_KIND(ed.sel3d.a[i]);
            if (k == SURF_FLOOR || k == SURF_CEIL) { if (!any) { map_select_none(&ed.map); any = true; ed.selmode = SM_SECTORS; } ed.map.sectors.a[SURF_IDX(ed.sel3d.a[i])].sel = 1; }
        }
        ed.vx = ed.cam.x; ed.vy = ed.cam.y;
    }
    plat_set_relative_mouse(false);
    ed.looking = false;
    ed.mode = m;
}

/* ------------------------------------------------------------------ view helpers */
void ed_fit_view(void) {
    double x0, y0, x1, y1;
    if (!map_bounds(&ed.map, &x0, &y0, &x1, &y1)) { ed.vx = ed.vy = 0; ed.zoom = 0.5; return; }
    ed.vx = (x0 + x1) * 0.5;
    ed.vy = (y0 + y1) * 0.5;
    double w = SF_MAX(x1 - x0, 256), h = SF_MAX(y1 - y0, 256);
    float vw = ed.view.w > 50 ? ed.view.w : 1000, vh = ed.view.h > 50 ? ed.view.h : 700;
    ed.zoom = SF_MIN(vw / (w * 1.25), vh / (h * 1.25));
    ed.zoom = SF_CLAMP(ed.zoom, 0.02, 16.0);
}

static const int g_grid_sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };

void ed_grid_step(int dir) {
    int idx = 0;
    for (int i = 0; i < SF_COUNT(g_grid_sizes); i++) if (g_grid_sizes[i] <= ed.grid) idx = i;
    if (g_grid_sizes[idx] != ed.grid && dir < 0) { ed.grid = g_grid_sizes[idx]; }
    else {
        idx = SF_CLAMP(idx + dir, 0, SF_COUNT(g_grid_sizes) - 1);
        ed.grid = g_grid_sizes[idx];
    }
    char msg[64];
    snprintf(msg, sizeof msg, "Grid size %d", ed.grid);
    ed_toast(msg, 0);
}

double ed_snap_value(double v) {
    if (!ed.snap || ed.grid <= 0) return v;
    return floor(v / ed.grid + 0.5) * ed.grid;
}

/* ------------------------------------------------------------------ commands */
void ed_delete(void) {
    if (ed.mode == MODE_3D) {
        int things = 0;
        for (int i = 0; i < ed.sel3d.n; i++) if (SURF_KIND(ed.sel3d.a[i]) == SURF_THING) things++;
        if (!things) { ed_toast("Select things to delete in 3D, or use the 2D view for geometry", 0); return; }
        ed_snapshot("Delete things");
        map_select_none(&ed.map);
        for (int i = 0; i < ed.sel3d.n; i++) if (SURF_KIND(ed.sel3d.a[i]) == SURF_THING) ed.map.things.a[SURF_IDX(ed.sel3d.a[i])].sel = 1;
        map_delete_selection(&ed.map, SM_THINGS);
        arr_clear(ed.sel3d);
        return;
    }
    if (!map_sel_count(&ed.map, ed.selmode)) { ed_toast("Nothing selected", 0); return; }
    ed_snapshot("Delete");
    map_delete_selection(&ed.map, ed.selmode);
    map_select_none(&ed.map);
    r3d_invalidate();
}

void ed_copy(bool cut) {
    if (!map_sel_count(&ed.map, ed.selmode)) { ed_toast("Nothing selected to copy", 0); return; }
    map_extract_selection(&ed.map, ed.selmode, &ed.clip);
    ed.clip_mode = ed.selmode;
    ed.has_clip = true;
    if (cut) ed_delete();
    else ed_toast("Copied", 0);
}

static void paste_at(double wx, double wy) {
    if (!ed.has_clip) { ed_toast("Clipboard is empty", 0); return; }
    double x0, y0, x1, y1;
    if (!map_bounds(&ed.clip, &x0, &y0, &x1, &y1)) return;
    double cx = ed_snap_value((x0 + x1) * 0.5), cy = ed_snap_value((y0 + y1) * 0.5);
    double dx = ed_snap_value(wx) - cx, dy = ed_snap_value(wy) - cy;
    ed_snapshot("Paste");
    ed.selmode = ed.clip_mode;
    map_paste(&ed.map, &ed.clip, dx, dy, ed.clip_mode);
    r3d_invalidate();
}

void ed_paste(void) { paste_at(ed.mwx, ed.mwy); }

void ed_duplicate(void) {
    if (!map_sel_count(&ed.map, ed.selmode)) { ed_toast("Nothing selected", 0); return; }
    map_extract_selection(&ed.map, ed.selmode, &ed.clip);
    ed.clip_mode = ed.selmode;
    ed.has_clip = true;
    double x0, y0, x1, y1;
    if (!map_bounds(&ed.clip, &x0, &y0, &x1, &y1)) return;
    paste_at((x0 + x1) * 0.5 + ed.grid * 2, (y0 + y1) * 0.5 - ed.grid * 2);
}

void ed_join(bool merge) {
    if (ed.selmode != SM_SECTORS || ed_sel_sector_count() < 2) { ed_toast("Select two or more sectors to join", 0); return; }
    ed_snapshot(merge ? "Merge sectors" : "Join sectors");
    map_join_sectors(&ed.map, merge);
    r3d_invalidate();
    ed_toast(merge ? "Sectors merged" : "Sectors joined", 1);
}

void ed_flip_lines(void) {
    if (ed.selmode != SM_LINES || !map_sel_count(&ed.map, SM_LINES)) { ed_toast("Select lines to flip", 0); return; }
    ed_snapshot("Flip lines");
    map_flip_lines(&ed.map);
    r3d_invalidate();
}

static bool sel_center(uint8_t *vmask, double *cx, double *cy) {
    Map *m = &ed.map;
    double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    bool any = false;
    for (int i = 0; i < m->verts.n; i++) if (vmask[i]) { any = true; x0 = SF_MIN(x0, m->verts.a[i].x); x1 = SF_MAX(x1, m->verts.a[i].x); y0 = SF_MIN(y0, m->verts.a[i].y); y1 = SF_MAX(y1, m->verts.a[i].y); }
    for (int i = 0; i < m->things.n; i++) if (m->things.a[i].sel && ed.selmode == SM_THINGS) { any = true; x0 = SF_MIN(x0, m->things.a[i].x); x1 = SF_MAX(x1, m->things.a[i].x); y0 = SF_MIN(y0, m->things.a[i].y); y1 = SF_MAX(y1, m->things.a[i].y); }
    *cx = ed_snap_value((x0 + x1) * 0.5);
    *cy = ed_snap_value((y0 + y1) * 0.5);
    return any;
}

static void transform_sel(int op, bool flag) {
    Map *m = &ed.map;
    uint8_t *mask = calloc((size_t)(m->verts.n ? m->verts.n : 1), 1);
    map_collect_sel_verts(m, ed.selmode, mask);
    double cx, cy;
    if (!sel_center(mask, &cx, &cy)) { free(mask); ed_toast("Nothing selected", 0); return; }
    ed_snapshot(op == 0 ? "Mirror" : "Rotate");
    for (int i = 0; i < m->verts.n; i++) {
        if (!mask[i]) continue;
        double x = m->verts.a[i].x - cx, y = m->verts.a[i].y - cy;
        if (op == 0) { if (flag) y = -y; else x = -x; }
        else { double t = x; if (flag) { x = y; y = -t; } else { x = -y; y = t; } }
        m->verts.a[i].x = x + cx; m->verts.a[i].y = y + cy;
    }
    for (int i = 0; i < m->things.n; i++) {
        MThing *t = &m->things.a[i];
        bool take = ed.selmode == SM_THINGS ? t->sel : (ed.selmode == SM_SECTORS && map_sector_at(m, t->x, t->y) >= 0 && m->sectors.a[map_sector_at(m, t->x, t->y)].sel);
        if (!take) continue;
        double x = t->x - cx, y = t->y - cy;
        if (op == 0) { if (flag) { y = -y; t->angle = -t->angle; } else { x = -x; t->angle = 180 - t->angle; } }
        else { double q = x; if (flag) { x = y; y = -q; t->angle -= 90; } else { x = -y; y = q; t->angle += 90; } }
        t->angle = fmodf(t->angle + 720, 360);
        t->x = x + cx; t->y = y + cy;
    }
    if (op == 0) map_mirror_fix(m, mask);
    m->geo_rev++;
    map_finalize_move(m, mask);
    free(mask);
    r3d_invalidate();
}

void ed_mirror(bool vertical) { transform_sel(0, vertical); }
void ed_rotate90(bool cw) { transform_sel(1, cw); }

void ed_apply_texture(const char *name) {
    Map *m = &ed.map;
    int cat = tex_find(name) >= 0 ? g_assets.tex.a[tex_find(name)].cat : 0;
    bool flat_like = cat == TEXCAT_FLAT || cat == TEXCAT_LIQUID || cat == TEXCAT_SKY;
    if (ed.mode == MODE_3D) { ed3d_texture_applied(name); return; }
    int target = ed.tex_target;
    if (target == TEXT_AUTO) target = ed.selmode == SM_LINES ? TEXT_WALL : (flat_like ? (tex_is_sky(name) ? TEXT_CEIL : TEXT_FLOOR) : TEXT_WALL);
    if (target == TEXT_WALL) sf_strcpy(ed.cur_wall, TEXNAME, name);
    else sf_strcpy(ed.cur_flat, TEXNAME, name);
    int ns = map_sel_count(m, SM_SECTORS), nl = map_sel_count(m, SM_LINES);
    if (ed.selmode == SM_SECTORS && ns) {
        ed_snapshot("Apply texture");
        for (int i = 0; i < m->sectors.n; i++) {
            if (!m->sectors.a[i].sel) continue;
            if (target == TEXT_FLOOR) sf_strcpy(m->sectors.a[i].floor_tex, TEXNAME, name);
            else if (target == TEXT_CEIL) sf_strcpy(m->sectors.a[i].ceil_tex, TEXNAME, name);
        }
        if (target == TEXT_WALL)
            for (int i = 0; i < m->lines.n; i++)
                for (int k = 0; k < 2; k++) {
                    MSide *sd = &m->lines.a[i].side[k];
                    if (sd->sector >= 0 && m->sectors.a[sd->sector].sel) { sf_strcpy(sd->upper, TEXNAME, name); sf_strcpy(sd->middle, TEXNAME, name); sf_strcpy(sd->lower, TEXNAME, name); }
                }
        char msg[96];
        snprintf(msg, sizeof msg, "%s applied to %d sector%s (%s)", name, ns, ns == 1 ? "" : "s", target == TEXT_FLOOR ? "floor" : target == TEXT_CEIL ? "ceiling" : "walls");
        ed_toast(msg, 1);
    } else if (ed.selmode == SM_LINES && nl) {
        ed_snapshot("Apply texture");
        for (int i = 0; i < m->lines.n; i++) {
            if (!m->lines.a[i].sel) continue;
            for (int k = 0; k < 2; k++) {
                MSide *sd = &m->lines.a[i].side[k];
                sf_strcpy(sd->upper, TEXNAME, name); sf_strcpy(sd->middle, TEXNAME, name); sf_strcpy(sd->lower, TEXNAME, name);
            }
        }
        char msg[96];
        snprintf(msg, sizeof msg, "%s applied to %d line%s", name, nl, nl == 1 ? "" : "s");
        ed_toast(msg, 1);
    }
}

void ed_apply_thing_type(int type) {
    ed.thing_type = type;
    Map *m = &ed.map;
    int n = map_sel_count(m, SM_THINGS);
    if (ed.selmode == SM_THINGS && n) {
        ed_snapshot("Change thing type");
        for (int i = 0; i < m->things.n; i++) if (m->things.a[i].sel) m->things.a[i].type = type;
    }
}

void ed_new_map(const char *title, int tpl) {
    if (ed.dirty) ed_save();
    int idx = project_add_map(&ed.proj, title, tpl);
    if (idx < 0) { ed_toast("Could not create the map", 2); return; }
    ed_load_map(idx);
    ed_toast("Map created", 1);
}
