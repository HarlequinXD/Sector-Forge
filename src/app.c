/* Sector Forge - application shell: splash, project manager, loading screen, editor layout, menus, dialogs */
#include "editor.h"
#include "assets.h"
#include "plat.h"
#include "glinc.h"
#include <time.h>

enum { APP_SPLASH, APP_PM, APP_LOADING, APP_EDITOR };
enum { PM_RECENT, PM_ALL, PM_TEMPLATES, PM_LEARN, PM_SETTINGS };
enum { DLG_NONE, DLG_NEW_PROJECT, DLG_DELETE_PROJECT, DLG_NEW_MAP, DLG_DELETE_MAP, DLG_UNSAVED, DLG_SHORTCUTS, DLG_ABOUT, DLG_MESSAGE };

static struct {
    int state;
    float scale;
    double t0, state_time;
    int asset_stage;
    float progress, shown_progress;
    char status[128];
    /* project manager */
    ProjectList projects;
    int pm_tab, pm_sel;
    char pm_search[64];
    float pm_scroll;
    float *tpl_seg[TPL_COUNT];
    int tpl_nseg[TPL_COUNT];
    /* loading */
    char load_file[600];
    char load_name[64];
    int load_step;
    /* dialogs */
    int dlg;
    char dlg_name[64], dlg_loc[512], dlg_err[200], dlg_msg[300], dlg_title[80];
    int dlg_tpl, dlg_idx, dlg_after;
    bool dlg_first;
    double fps_t;
    int fps_frames;
    double last_autosave;
    float win_w;
} A;

static Col C(uint32_t h) { return col_hex(h); }
#define ID(s) ui_id(s, __LINE__)

/* ------------------------------------------------------------------ fonts */
static void build_fonts(float scale) {
    gfx_font_build(FONT_UI, "Segoe UI", 13.5f, 400, scale);
    gfx_font_build(FONT_UI_BOLD, "Segoe UI", 13.5f, 600, scale);
    gfx_font_build(FONT_SMALL, "Segoe UI", 12.0f, 400, scale);
    gfx_font_build(FONT_MONO, "Consolas", 13.5f, 400, scale);
    gfx_font_build(FONT_MONO_SMALL, "Consolas", 12.0f, 400, scale);
    gfx_font_build(FONT_HEAD, "Segoe UI", 22.0f, 700, scale);
    gfx_font_build(FONT_TITLE, "Segoe UI", 44.0f, 800, scale);
    gfx_font_build(FONT_HUD, "Consolas", 28.0f, 700, scale);
    A.scale = scale;
}

/* ------------------------------------------------------------------ helpers */
static void draw_segments(Rect r, const float *seg, int n, Col c, float th) {
    for (int i = 0; i < n; i++) {
        const float *s = seg + i * 4;
        gfx_line(r.x + s[0] * r.w, r.y + s[1] * r.h, r.x + s[2] * r.w, r.y + s[3] * r.h, th, c);
    }
}

static void thumb(Rect r, const float *seg, int n, bool sel) {
    gfx_rect(r, C(TH_VIEW));
    gfx_clip_push(r);
    for (float x = r.x; x < r.x + r.w; x += 15) gfx_rect(rect(x, r.y, 1, r.h), C(0x1A1D22));
    for (float y = r.y; y < r.y + r.h; y += 15) gfx_rect(rect(r.x, y, r.w, 1), C(0x1A1D22));
    float s = SF_MIN(r.w, r.h) * 0.86f;
    Rect q = rect(r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s);
    if (n) draw_segments(q, seg, n, sel ? C(TH_ACCENT_HI) : C(0xC9CFD8), 1.5f);
    else gfx_text_center(FONT_SMALL, r, "Empty map", C(TH_TEXT3));
    gfx_clip_pop();
}

static void make_tpl_thumbs(void) {
    MapDefaults d;
    map_defaults_init(&d);
    for (int t = 0; t < TPL_COUNT; t++) {
        Map m;
        map_init(&m);
        map_make_template(&m, t, &d);
        double x0, y0, x1, y1;
        A.tpl_nseg[t] = 0;
        if (m.lines.n && map_bounds(&m, &x0, &y0, &x1, &y1)) {
            double span = SF_MAX(x1 - x0, y1 - y0), cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            A.tpl_seg[t] = malloc(sizeof(float) * 4 * (size_t)m.lines.n);
            for (int i = 0; i < m.lines.n; i++) {
                const MVert *a = &m.verts.a[m.lines.a[i].v1], *b = &m.verts.a[m.lines.a[i].v2];
                A.tpl_seg[t][i * 4] = (float)((a->x - cx) / span + 0.5); A.tpl_seg[t][i * 4 + 1] = (float)(0.5 - (a->y - cy) / span);
                A.tpl_seg[t][i * 4 + 2] = (float)((b->x - cx) / span + 0.5); A.tpl_seg[t][i * 4 + 3] = (float)(0.5 - (b->y - cy) / span);
            }
            A.tpl_nseg[t] = m.lines.n;
        }
        map_free(&m);
    }
}

static void fmt_age(int64_t t, char *out, size_t cap) {
    int64_t d = (int64_t)time(NULL) - t;
    if (t <= 0) { snprintf(out, cap, "Unknown"); return; }
    if (d < 60) snprintf(out, cap, "Edited just now");
    else if (d < 3600) snprintf(out, cap, "Edited %d min ago", (int)(d / 60));
    else if (d < 86400) snprintf(out, cap, "Edited %d hour%s ago", (int)(d / 3600), d / 3600 == 1 ? "" : "s");
    else if (d < 86400 * 2) snprintf(out, cap, "Edited yesterday");
    else if (d < 86400 * 14) snprintf(out, cap, "Edited %d days ago", (int)(d / 86400));
    else snprintf(out, cap, "Edited %d weeks ago", (int)(d / (86400 * 7)));
}

static void set_state(int s) { A.state = s; A.state_time = plat_time(); }

static void open_dialog(int kind) {
    A.dlg = kind;
    A.dlg_err[0] = 0;
    A.dlg_first = true;
    ui_close_popup();
}

static void message(const char *title, const char *msg) {
    sf_strcpy(A.dlg_title, sizeof A.dlg_title, title);
    sf_strcpy(A.dlg_msg, sizeof A.dlg_msg, msg);
    open_dialog(DLG_MESSAGE);
}

static void begin_load(const char *file, const char *name) {
    sf_strcpy(A.load_file, sizeof A.load_file, file);
    sf_strcpy(A.load_name, sizeof A.load_name, name);
    A.load_step = 0;
    A.progress = A.shown_progress = 0;
    set_state(APP_LOADING);
}

/* ------------------------------------------------------------------ title bar */
static MenuItem mi_file[8], mi_edit[13], mi_view[16], mi_map[14], mi_tools[14], mi_play[4], mi_help[3];

static void MI(MenuItem *m, const char *label, const char *sc, int icon, bool checked, bool disabled) {
    m->label = label; m->shortcut = sc; m->icon = icon; m->checked = checked; m->disabled = disabled; m->separator = false;
}
static void SEP(MenuItem *m) { memset(m, 0, sizeof *m); m->separator = true; m->icon = -1; }

static void build_menus(void) {
    bool has_sel = ed.open && map_sel_count(&ed.map, ed.selmode) > 0;
    MI(&mi_file[0], "New map\xE2\x80\xA6", "Ctrl+N", IC_new, false, false);
    MI(&mi_file[1], "Save", "Ctrl+S", IC_save, false, false);
    MI(&mi_file[2], "Import texture\xE2\x80\xA6", NULL, IC_import, false, false);
    MI(&mi_file[3], "Open project folder", NULL, IC_open, false, false);
    SEP(&mi_file[4]);
    MI(&mi_file[5], "Close project", NULL, IC_close, false, false);
    MI(&mi_file[6], "Exit", "Alt+F4", -1, false, false);

    MI(&mi_edit[0], "Undo", "Ctrl+Z", IC_undo, false, ed.undo.n == 0);
    MI(&mi_edit[1], "Redo", "Ctrl+Y", IC_redo, false, ed.redo.n == 0);
    SEP(&mi_edit[2]);
    MI(&mi_edit[3], "Cut", "Ctrl+X", IC_cut, false, !has_sel);
    MI(&mi_edit[4], "Copy", "Ctrl+C", IC_copy, false, !has_sel);
    MI(&mi_edit[5], "Paste", "Ctrl+V", IC_paste, false, !ed.has_clip);
    MI(&mi_edit[6], "Duplicate", "Ctrl+D", IC_copy, false, !has_sel);
    MI(&mi_edit[7], "Delete", "Del", IC_trash, false, false);
    SEP(&mi_edit[8]);
    MI(&mi_edit[9], "Select all", "Ctrl+A", -1, false, false);
    MI(&mi_edit[10], "Select none", "Esc", -1, false, false);
    MI(&mi_edit[11], "Invert selection", "Ctrl+I", -1, false, false);

    MI(&mi_view[0], "2D map", "Tab", IC_mode2d, ed.mode == MODE_2D, false);
    MI(&mi_view[1], "3D view", "Tab", IC_cube, ed.mode == MODE_3D, false);
    SEP(&mi_view[2]);
    MI(&mi_view[3], "Show grid", NULL, -1, ed.show_grid, false);
    MI(&mi_view[4], "Show things", NULL, -1, ed.show_things, false);
    MI(&mi_view[5], "Sector heights", NULL, -1, ed.show_heights, false);
    MI(&mi_view[6], "Line lengths", NULL, -1, ed.show_lengths, false);
    MI(&mi_view[7], "Textured floors (2D)", NULL, -1, ed.textured_2d, false);
    SEP(&mi_view[8]);
    MI(&mi_view[9], "Fullbright (3D)", "F", -1, ed.fullbright, false);
    MI(&mi_view[10], "Wireframe overlay (3D)", "G", -1, ed.wireframe, false);
    SEP(&mi_view[11]);
    MI(&mi_view[12], "Zoom to fit", "Home", IC_fit, false, false);

    MI(&mi_map[0], "Join sectors", "J", IC_join, false, false);
    MI(&mi_map[1], "Merge sectors", "Shift+J", IC_join, false, false);
    MI(&mi_map[2], "Flip lines", "Ctrl+F", IC_flip, false, false);
    MI(&mi_map[3], "Mirror horizontally", "H", IC_flip, false, false);
    MI(&mi_map[4], "Mirror vertically", "V", IC_flip_v, false, false);
    MI(&mi_map[5], "Rotate 90\xC2\xB0 clockwise", "Ctrl+]", IC_rotate, false, false);
    MI(&mi_map[6], "Rotate 90\xC2\xB0 counter-clockwise", "Ctrl+[", IC_rotate, false, false);
    SEP(&mi_map[7]);
    MI(&mi_map[8], "Grid tool\xE2\x80\xA6", "G", IC_grid, ed.grid_popup, false);
    MI(&mi_map[9], "Smaller grid", "[", IC_minus, false, false);
    MI(&mi_map[10], "Larger grid", "]", IC_plus, false, false);
    MI(&mi_map[11], "Snap to grid", NULL, IC_magnet, ed.snap, false);
    MI(&mi_map[12], "Snap to vertices & lines", NULL, -1, ed.snap_geo, false);

    static const char *tn[] = { "Select", "Pan", "Draw sector", "Rectangle", "Polygon / circle", "Draw lines", "Insert vertex", "Split line", "Move", "Rotate", "Scale", "Place thing", "Measure", "Paint texture" };
    static const char *tk[] = { "S", "Space", "D", "R", "O", "L", "I", "X", "M", NULL, NULL, "T", "U", "B" };
    static const int ti[] = { IC_select, IC_hand, IC_draw_sector, IC_rect, IC_polygon, IC_line, IC_vertex, IC_split, IC_move, IC_rotate, IC_scale, IC_thing, IC_measure, IC_bucket };
    for (int i = 0; i < T_COUNT; i++) MI(&mi_tools[i], tn[i], tk[i], ti[i], ed.mode == MODE_2D && ed.tool == i, false);

    MI(&mi_play[0], "Play from player start", "F5", IC_play, false, ed.mode == MODE_PLAY);
    MI(&mi_play[1], "Play from 3D camera", "Shift+F5", IC_cube, false, ed.mode == MODE_PLAY);
    MI(&mi_play[2], "Stop", "Esc", IC_stop, false, ed.mode != MODE_PLAY);

    MI(&mi_help[0], "Keyboard shortcuts", "F1", IC_keyboard, false, false);
    MI(&mi_help[1], "About Sector Forge", NULL, IC_info, false, false);
}

static void handle_menus(void) {
    int r;
    if ((r = ui_popup_result(ui_id("m_file", 0))) >= 0) {
        if (r == 0) app_dialog_new_map();
        if (r == 1 && ed_save()) ed_toast("Saved", 1);
        if (r == 2) app_import_texture();
        if (r == 3) plat_open_folder(ed.proj.dir);
        if (r == 5) app_request_close_project();
        if (r == 6) plat_request_close();
    }
    if ((r = ui_popup_result(ui_id("m_edit", 0))) >= 0) {
        if (r == 0) ed_undo();
        if (r == 1) ed_redo();
        if (r == 3) ed_copy(true);
        if (r == 4) ed_copy(false);
        if (r == 5) ed_paste();
        if (r == 6) ed_duplicate();
        if (r == 7) ed_delete();
        if (r == 9) ed_select_all();
        if (r == 10) ed_select_none();
        if (r == 11) ed_invert_selection();
    }
    if ((r = ui_popup_result(ui_id("m_view", 0))) >= 0) {
        if (r == 0) ed_set_mode(MODE_2D);
        if (r == 1) ed_set_mode(MODE_3D);
        if (r == 3) ed.show_grid = !ed.show_grid;
        if (r == 4) ed.show_things = !ed.show_things;
        if (r == 5) ed.show_heights = !ed.show_heights;
        if (r == 6) ed.show_lengths = !ed.show_lengths;
        if (r == 7) ed.textured_2d = !ed.textured_2d;
        if (r == 9) ed.fullbright = !ed.fullbright;
        if (r == 10) ed.wireframe = !ed.wireframe;
        if (r == 12) ed_fit_view();
    }
    if ((r = ui_popup_result(ui_id("m_map", 0))) >= 0) {
        if (r == 0) ed_join(false);
        if (r == 1) ed_join(true);
        if (r == 2) ed_flip_lines();
        if (r == 3) ed_mirror(false);
        if (r == 4) ed_mirror(true);
        if (r == 5) ed_rotate90(true);
        if (r == 6) ed_rotate90(false);
        if (r == 8) { ed.grid_popup = !ed.grid_popup; ed_set_mode(MODE_2D); }
        if (r == 9) ed_grid_step(-1);
        if (r == 10) ed_grid_step(1);
        if (r == 11) ed.snap = !ed.snap;
        if (r == 12) ed.snap_geo = !ed.snap_geo;
    }
    if ((r = ui_popup_result(ui_id("m_tools", 0))) >= 0) { ed_set_mode(MODE_2D); ed_set_tool(r); }
    if ((r = ui_popup_result(ui_id("m_play", 0))) >= 0) {
        if (r == 0) ed_set_mode(MODE_PLAY);
        if (r == 1) { if (!ed.cam_valid) ed3d_sync_camera_from_2d(); play_start(true); }
        if (r == 2) play_stop();
    }
    if ((r = ui_popup_result(ui_id("m_help", 0))) >= 0) {
        if (r == 0) app_dialog_shortcuts();
        if (r == 1) app_dialog_about();
    }
}

static void titlebar(Rect r, const char *subtitle, bool menus, const Input *in) {
    gfx_rect(r, C(TH_TITLE));
    gfx_rect(rect(r.x, r.y + r.h - 1, r.w, 1), C(TH_BORDER));
    float x = r.x + 12;
    gfx_icon(IC_logo, x + 9, r.y + r.h * 0.5f, 18, C(TH_ACCENT));
    x += 26;
    x = gfx_text(FONT_UI_BOLD, x, r.y + (r.h - gfx_font_h(FONT_UI_BOLD)) * 0.5f, "Sector Forge", C(0xF1F3F6)) + 10;
    if (menus) {
        build_menus();
        static const char *names[] = { "File", "Edit", "View", "Map", "Tools", "Play", "Help" };
        static const char *ids[] = { "m_file", "m_edit", "m_view", "m_map", "m_tools", "m_play", "m_help" };
        MenuItem *items[] = { mi_file, mi_edit, mi_view, mi_map, mi_tools, mi_play, mi_help };
        int counts[] = { 7, 12, 13, 13, T_COUNT, 3, 2 };
        for (int i = 0; i < 7; i++) {
            float w = gfx_text_w(FONT_UI, names[i]) + 18;
            ui_menu_button(ui_id(ids[i], 0), rect(x, r.y + 4, w, r.h - 8), names[i], items[i], counts[i]);
            x += w + 1;
        }
    }
    float bw = 46;
    float right = r.x + r.w - bw * 3;
    if (subtitle) {
        float tw = gfx_text_w(FONT_SMALL, subtitle);
        float cx = r.x + (r.w - tw) * 0.5f;
        if (cx < x + 12) cx = x + 12;
        if (cx + tw < right - 12) gfx_text(FONT_SMALL, cx, r.y + (r.h - gfx_font_h(FONT_SMALL)) * 0.5f, subtitle, C(TH_TEXT3));
    }
    if (ui_win_btn(ID("wmin"), rect(right, r.y, bw, r.h), IC_minimize, false)) plat_minimize();
    if (ui_win_btn(ID("wmax"), rect(right + bw, r.y, bw, r.h), in->maximized ? IC_restore : IC_maximize, false)) plat_toggle_maximize();
    if (ui_win_btn(ID("wclose"), rect(right + bw * 2, r.y, bw, r.h), IC_close, true)) plat_request_close();
}

/* ------------------------------------------------------------------ splash */
static void splash_bg(Rect r, double t) {
    gfx_rect(r, C(TH_VIEW));
    for (float x = r.x; x < r.x + r.w; x += 24) gfx_rect(rect(x, r.y, 1, r.h), C(0x15181D));
    for (float y = r.y; y < r.y + r.h; y += 24) gfx_rect(rect(r.x, y, r.w, 1), C(0x15181D));
    /* wireframe sector plan on the right */
    float ox = r.x + r.w * 0.53f, oy = r.y + r.h * 0.2f, s = r.h / 450.0f;
    Col lc = col_hex(0x2A2E36);
    Rect rooms[] = { { 400, 90, 110, 80 }, { 510, 120, 60, 24 }, { 430, 170, 50, 60 }, { 380, 230, 150, 70 } };
    for (int i = 0; i < 4; i++) {
        Rect q = rect(ox + (rooms[i].x - 400) * s, oy + (rooms[i].y - 90) * s, rooms[i].w * s, rooms[i].h * s);
        gfx_rect_line(q, 1.5f, lc);
    }
    float cx = ox + (620 - 400) * s, cy = oy + (132 - 90) * s, rr = 52 * s;
    float pulse = 0.5f + 0.5f * (float)sin(t * 2.2);
    for (int k = 0; k < 8; k++) {
        float a0 = (float)((22.5 + 45 * k) * SF_DEG2RAD), a1 = (float)((22.5 + 45 * (k + 1)) * SF_DEG2RAD);
        gfx_line(cx + cosf(a0) * rr, cy + sinf(a0) * rr, cx + cosf(a1) * rr, cy + sinf(a1) * rr, 1.5f, col(255, 122, 26, (int)(110 + 80 * pulse)));
    }
    gfx_circle(cx, cy, rr * 0.92f, col(255, 122, 26, 18));
}

static void splash(const Input *in) {
    double now = plat_time();
    if (!g_assets.ready) {
        double start = now;
        do { A.progress = assets_load_step(&A.asset_stage, A.status, sizeof A.status); } while (!g_assets.ready && plat_time() - start < 0.012);
        if (g_assets.ready) { make_tpl_thumbs(); sf_strcpy(A.status, sizeof A.status, "Ready"); }
    }
    A.shown_progress += (A.progress - A.shown_progress) * SF_MIN(1.0f, (float)in->dt * 10);
    Rect r = rect(0, 0, in->win_w, in->win_h);
    splash_bg(r, now);
    if (g_assets.ready) {
        Rect lg = rect(44, 56, 64, 64);
        gfx_rrect(lg, 14, C(0x1D140D));
        gfx_rrect_line(lg, 14, 1, C(TH_ACCENT_BD));
        gfx_icon(IC_logo, lg.x + 32, lg.y + 32, 40, C(TH_ACCENT));
    }
    gfx_text(FONT_TITLE, 42, 136, "SECTOR", C(0xF1F3F6));
    gfx_text(FONT_TITLE, 42, 136 + gfx_font_h(FONT_TITLE) * 0.9f, "FORGE", C(TH_ACCENT));
    gfx_text(FONT_UI, 44, 148 + gfx_font_h(FONT_TITLE) * 1.9f, "Sector map editor & game engine", C(TH_TEXT2));
    float by = in->win_h - 36;
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(A.shown_progress * 100 + 0.5f));
    gfx_text(FONT_UI, 44, by - 34, A.status, C(TH_TEXT));
    gfx_text_right(FONT_MONO, in->win_w - 44, by - 34, pct, C(TH_ACCENT_HI));
    gfx_rrect(rect(44, by - 12, in->win_w - 88, 4), 2, C(TH_BORDER));
    gfx_rrect(rect(44, by - 12, (in->win_w - 88) * SF_CLAMP(A.shown_progress, 0, 1), 4), 2, C(TH_ACCENT));
    gfx_text(FONT_MONO_SMALL, 44, by + 2, "v" SF_VERSION " \xC2\xB7 C \xC2\xB7 OpenGL", C(TH_TEXT3));
    if (ui_win_btn(ID("splash_close"), rect(in->win_w - 46, 0, 46, 36), IC_close, true)) plat_quit();
    if (g_assets.ready && now - A.state_time > 1.3 && A.shown_progress > 0.98f) {
        plat_set_window_mode(WINMODE_MAIN);
        projects_scan(&A.projects);
        A.pm_sel = A.projects.n ? 0 : -1;
        set_state(APP_PM);
    }
}

/* ------------------------------------------------------------------ project manager */
static void pm_open_selected(void) {
    if (A.pm_sel < 0 || A.pm_sel >= A.projects.n) return;
    ProjectInfo *p = &A.projects.a[A.pm_sel];
    if (p->missing) { message("Project not found", "The project file could not be read. It may have been moved or deleted."); return; }
    begin_load(p->file, p->name);
}

static void pm_project_card(Rect r, int i) {
    ProjectInfo *p = &A.projects.a[i];
    bool sel = A.pm_sel == i;
    bool hov = ui_hover(r);
    uint32_t id = ui_id("pcard", i);
    if (hov) { ui.hot = id; ui.cursor = CUR_HAND; }
    if (hov && ui.in->mpressed[0]) { A.pm_sel = i; if (ui.in->mdouble[0]) pm_open_selected(); }
    gfx_rrect(r, 12, sel ? C(0x1D140D) : hov ? C(0x181B20) : C(TH_PANEL));
    float th = r.h - 76;
    Rect t = rect(r.x + 1, r.y + 1, r.w - 2, th);
    gfx_clip_push(rect(r.x, r.y, r.w, th + 1));
    thumb(t, p->seg, p->nseg, sel);
    gfx_clip_pop();
    gfx_rrect_line(r, 12, sel ? 2.f : 1.f, sel ? C(TH_ACCENT) : C(0x23262D));
    float y = r.y + th + 12;
    gfx_text_fit(FONT_UI_BOLD, r.x + 14, y, r.w - 70, p->name, C(TH_TEXT));
    if (i == 0 && !p->missing && A.pm_tab == PM_RECENT) {
        Rect b = rect(r.x + r.w - 50, y, 38, 18);
        gfx_rrect(b, 4, C(TH_ACCENT));
        gfx_text_center(FONT_SMALL, b, "LAST", C(0x1A0D02));
    }
    char s[96];
    if (p->missing) snprintf(s, sizeof s, "Missing \xE2\x80\x94 moved or deleted");
    else snprintf(s, sizeof s, "%d map%s \xC2\xB7 %d texture%s", p->nmaps, p->nmaps == 1 ? "" : "s", p->ntex, p->ntex == 1 ? "" : "s");
    gfx_text(FONT_SMALL, r.x + 14, y + 22, s, p->missing ? C(TH_RED) : C(TH_TEXT2));
    fmt_age(p->mtime, s, sizeof s);
    gfx_icon(IC_clock, r.x + 20, y + 47, 12, C(TH_TEXT3));
    gfx_text(FONT_SMALL, r.x + 30, y + 40, s, C(TH_TEXT3));
}

static void pm_template_card(Rect r, int t) {
    bool hov = ui_hover(r);
    uint32_t id = ui_id("tcard", t);
    if (hov) { ui.hot = id; ui.cursor = CUR_HAND; }
    gfx_rrect(r, 10, hov ? C(0x181B20) : C(TH_PANEL));
    float th = r.h - 54;
    gfx_clip_push(rect(r.x, r.y, r.w, th));
    thumb(rect(r.x + 1, r.y + 1, r.w - 2, th), A.tpl_seg[t], A.tpl_nseg[t], false);
    gfx_clip_pop();
    gfx_rrect_line(r, 10, 1, hov ? C(0x3A3F49) : C(0x23262D));
    gfx_text(FONT_UI_BOLD, r.x + 12, r.y + th + 8, g_template_names[t], C(TH_TEXT));
    gfx_text_fit(FONT_SMALL, r.x + 12, r.y + th + 28, r.w - 24, g_template_desc[t], C(TH_TEXT2));
    if (hov && ui.in->mreleased[0]) {
        A.dlg_tpl = t;
        snprintf(A.dlg_name, sizeof A.dlg_name, "%s", t == TPL_EMPTY ? "New Project" : g_template_names[t]);
        sf_strcpy(A.dlg_loc, sizeof A.dlg_loc, g_settings.projects_dir);
        open_dialog(DLG_NEW_PROJECT);
    }
}

static void pm_settings(Rect r) {
    float x = r.x, y = r.y, w = SF_MIN(560, r.w);
    gfx_text(FONT_HEAD, x, y, "Settings", C(TH_TEXT));
    y += 46;
    Settings *s = &g_settings;
    gfx_text(FONT_SMALL, x, y, "MOUSE & CAMERA", C(0xA3ABB8));
    y += 24;
    gfx_text(FONT_UI, x, y + 4, "Mouse sensitivity", C(TH_TEXT2));
    ui_slider(ID("set_sens"), rect(x + 200, y + 4, w - 260, 20), &s->mouse_sens, 0.2f, 3.0f, 2);
    char t[32];
    snprintf(t, sizeof t, "%.2f", s->mouse_sens);
    gfx_text_right(FONT_MONO, x + w, y + 4, t, C(TH_TEXT));
    y += 36;
    gfx_text(FONT_UI, x, y + 4, "Field of view", C(TH_TEXT2));
    ui_slider(ID("set_fov"), rect(x + 200, y + 4, w - 260, 20), &s->fov, 60, 120, 0);
    snprintf(t, sizeof t, "%.0f\xC2\xB0", s->fov);
    gfx_text_right(FONT_MONO, x + w, y + 4, t, C(TH_TEXT));
    y += 36;
    ui_toggle(ID("set_inv"), rect(x, y, w, 28), "Invert mouse Y", &s->invert_y); y += 36;
    gfx_text(FONT_SMALL, x, y + 8, "EDITOR", C(0xA3ABB8));
    y += 32;
    ui_toggle(ID("set_tex"), rect(x, y, w, 28), "Textured floors in the 2D view", &s->textured_2d); y += 36;
    ui_toggle(ID("set_auto"), rect(x, y, w, 28), "Autosave every 2 minutes", &s->autosave); y += 36;
    ui_toggle(ID("set_fps"), rect(x, y, w, 28), "Show FPS in play mode", &s->show_fps); y += 36;
    bool vs = s->vsync;
    if (ui_toggle(ID("set_vs"), rect(x, y, w, 28), "Vertical sync", &vs)) { s->vsync = vs; plat_set_vsync(vs); }
    y += 36;
    gfx_text(FONT_UI, x, y + 6, "Default grid size", C(TH_TEXT2));
    float g = (float)s->default_grid;
    if (ui_number(ID("set_grid"), rect(x + w - 140, y, 140, 30), &g, 8, 0, 1, 512, true)) s->default_grid = (int)g;
    y += 46;
    gfx_text(FONT_SMALL, x, y, "PROJECTS FOLDER", C(0xA3ABB8));
    y += 22;
    Rect pf = rect(x, y, w - 100, 32);
    gfx_rrect(pf, 6, C(TH_FIELD));
    gfx_rrect_line(pf, 6, 1, C(TH_BORDER2));
    gfx_text_fit(FONT_MONO_SMALL, pf.x + 10, pf.y + 9, pf.w - 20, s->projects_dir, C(TH_TEXT));
    if (ui_button(ID("set_browse"), rect(x + w - 92, y, 92, 32), IC_open, "Browse", BTN_DEFAULT)) {
        char dir[512];
        if (plat_pick_folder(dir, sizeof dir, "Choose the folder for new projects")) sf_strcpy(s->projects_dir, sizeof s->projects_dir, dir);
    }
    y += 48;
    if (ui_button(ID("set_save"), rect(x, y, 160, 34), IC_check, "Save settings", BTN_PRIMARY)) { settings_save(); projects_scan(&A.projects); ed_toast("Settings saved", 1); }
}

static void pm_learn(Rect r) {
    float x = r.x, y = r.y;
    gfx_text(FONT_HEAD, x, y, "Getting started", C(TH_TEXT));
    y += 44;
    static const char *steps[][2] = {
        { "1. Draw sectors", "Press D, click corners of a room and double-click to close it. Draw rooms touching each other to connect them." },
        { "2. Set heights", "Select a sector (3) and set floor and ceiling heights in Properties, or press Tab for 3D and scroll the wheel over a floor." },
        { "3. Texture it", "In 3D click a wall, floor or ceiling and click a texture in the Textures tab. Arrow keys align, Alt+click picks." },
        { "4. Add things", "Press T and click to place the player start, lights, enemies and pickups. Drag while placing to aim them." },
        { "5. Play", "Press F5 to walk your map. E opens doors and lifts, left click shoots, Esc returns to the editor." },
    };
    for (int i = 0; i < SF_COUNT(steps); i++) {
        Rect c = rect(x, y, SF_MIN(720, r.w), 64);
        gfx_rrect(c, 10, C(TH_PANEL));
        gfx_rrect_line(c, 10, 1, C(0x23262D));
        gfx_text(FONT_UI_BOLD, c.x + 16, c.y + 12, steps[i][0], C(TH_ACCENT_HI));
        gfx_text_fit(FONT_SMALL, c.x + 16, c.y + 34, c.w - 32, steps[i][1], C(TH_TEXT2));
        y += 72;
    }
    y += 8;
    if (ui_button(ID("learn_keys"), rect(x, y, 220, 34), IC_keyboard, "Keyboard shortcuts", BTN_DEFAULT)) app_dialog_shortcuts();
}

static void project_manager(const Input *in) {
    Rect full = rect(0, 0, in->win_w, in->win_h);
    gfx_rect(full, C(TH_BG));
    titlebar(rect(0, 0, in->win_w, 36), "Project Manager", false, in);
    Rect body = rect(0, 36, in->win_w, in->win_h - 36);
    /* sidebar */
    Rect side = rect_cut_left(&body, 232);
    gfx_rect(side, C(0x121418));
    gfx_rect(rect(side.x + side.w - 1, side.y, 1, side.h), C(TH_BORDER));
    static const char *nav[] = { "Recent", "All projects", "Templates", "Learn", "Settings" };
    static const int navi[] = { IC_clock, IC_open, IC_template, IC_book, IC_settings };
    float y = side.y + 16;
    for (int i = 0; i < 5; i++) {
        Rect nr = rect(side.x + 10, y, side.w - 20, 36);
        bool on = A.pm_tab == i;
        bool hov = ui_hover(nr);
        if (hov) { ui.hot = ui_id("pmnav", i); ui.cursor = CUR_HAND; }
        if (hov && in->mpressed[0]) { A.pm_tab = i; A.pm_scroll = 0; }
        if (on) gfx_rrect(nr, 8, C(0x2A1709));
        else if (hov) gfx_rrect(nr, 8, C(TH_HOVER));
        gfx_icon(navi[i], nr.x + 20, nr.y + 18, 17, on ? C(TH_ACCENT_HI) : C(0xC4CAD4));
        gfx_text(on ? FONT_UI_BOLD : FONT_UI, nr.x + 38, nr.y + (36 - gfx_font_h(FONT_UI)) * 0.5f, nav[i], on ? C(TH_ACCENT_HI) : C(0xC4CAD4));
        y += 38;
    }
    Rect ver = rect(side.x + 10, side.y + side.h - 72, side.w - 20, 60);
    gfx_rrect_line(ver, 10, 1, C(0x23262D));
    gfx_icon(IC_logo, ver.x + 22, ver.y + 20, 18, C(TH_ACCENT));
    gfx_text(FONT_UI_BOLD, ver.x + 38, ver.y + 11, "Sector Forge", C(0xF1F3F6));
    gfx_text(FONT_MONO_SMALL, ver.x + 12, ver.y + 36, "v" SF_VERSION " \xC2\xB7 C \xC2\xB7 OpenGL", C(TH_TEXT3));

    /* detail panel */
    bool show_detail = (A.pm_tab == PM_RECENT || A.pm_tab == PM_ALL) && A.pm_sel >= 0 && A.pm_sel < A.projects.n && body.w > 900;
    Rect detail = show_detail ? rect_cut_right(&body, 340) : rect(0, 0, 0, 0);
    /* main */
    Rect main = rect_inset(body, 0);
    float sy = ui_scroll_begin(ID("pmscroll"), main, &A.pm_scroll);
    float x = main.x + 32, w = main.w - 64;
    y = sy + 28;
    if (A.pm_tab == PM_RECENT || A.pm_tab == PM_ALL || A.pm_tab == PM_TEMPLATES) {
        const char *h1 = A.pm_tab == PM_TEMPLATES ? "Templates" : A.pm_tab == PM_ALL ? "All projects" : "Projects";
        gfx_text(FONT_HEAD, x, y, h1, C(TH_TEXT));
        gfx_text(FONT_UI, x, y + 34, A.pm_tab == PM_TEMPLATES ? "Start a new project from a ready-made layout." : "Pick up where you left off, or forge something new.", C(TH_TEXT2));
        float bx = x + w;
        bx -= 140;
        if (ui_button(ID("pm_new"), rect(bx, y + 8, 140, 36), IC_plus, "New Project", BTN_PRIMARY)) {
            A.dlg_tpl = TPL_STARTER;
            sf_strcpy(A.dlg_name, sizeof A.dlg_name, "New Project");
            sf_strcpy(A.dlg_loc, sizeof A.dlg_loc, g_settings.projects_dir);
            open_dialog(DLG_NEW_PROJECT);
        }
        bx -= 104;
        if (ui_button(ID("pm_open"), rect(bx, y + 10, 96, 32), IC_open, "Open\xE2\x80\xA6", BTN_DEFAULT)) {
            char f[600];
            if (plat_open_file_dialog(f, sizeof f, "Open a Sector Forge project", "Sector Forge project", "*.sfproj")) {
                projects_remember(f);
                Project p;
                if (project_load(&p, f)) begin_load(f, p.name);
                else message("Could not open project", "That file is not a Sector Forge project.");
            }
        }
        bx -= 104;
        if (ui_button(ID("pm_import"), rect(bx, y + 10, 96, 32), IC_import, "Import", BTN_DEFAULT)) {
            char f[600];
            if (plat_open_file_dialog(f, sizeof f, "Add an existing project to the list", "Sector Forge project", "*.sfproj")) {
                projects_remember(f);
                projects_scan(&A.projects);
                ed_toast("Project added to the list", 1);
            }
        }
        if (A.pm_tab == PM_ALL) {
            bx -= 248;
            ui_text(ID("pm_search"), rect(bx, y + 10, 240, 32), A.pm_search, sizeof A.pm_search, "Search projects", IC_search);
        }
        y += 76;
        if (A.pm_tab != PM_ALL) {
            gfx_text(FONT_SMALL, x, y, A.pm_tab == PM_TEMPLATES ? "CHOOSE A TEMPLATE" : "START FROM A TEMPLATE", C(0xA3ABB8));
            y += 22;
            float tw = A.pm_tab == PM_TEMPLATES ? 280 : 170;
            int cols = SF_MAX(1, (int)((w + 12) / (tw + 12)));
            float cw = (w - (cols - 1) * 12) / cols;
            float th = A.pm_tab == PM_TEMPLATES ? cw * 0.75f + 54 : 150;
            for (int t = 0; t < TPL_COUNT; t++) {
                int cx = t % cols, cy = t / cols;
                pm_template_card(rect(x + cx * (cw + 12), y + cy * (th + 12), cw, th), t);
            }
            y += ((TPL_COUNT + cols - 1) / cols) * (th + 12) + 16;
        }
        if (A.pm_tab != PM_TEMPLATES) {
            gfx_text(FONT_SMALL, x, y, A.pm_tab == PM_ALL ? "ALL PROJECTS" : "RECENT PROJECTS", C(0xA3ABB8));
            gfx_text_right(FONT_MONO_SMALL, x + w, y, g_settings.projects_dir, C(TH_TEXT3));
            y += 22;
            int cols = SF_MAX(1, (int)((w + 14) / (244 + 14)));
            float cw = (w - (cols - 1) * 14) / cols, ch = 210;
            int shown = 0;
            for (int i = 0; i < A.projects.n; i++) {
                if (A.pm_tab == PM_ALL && A.pm_search[0] && !sf_stristr(A.projects.a[i].name, A.pm_search)) continue;
                if (A.pm_tab == PM_RECENT && shown >= 12) break;
                int cx = shown % cols, cy = shown / cols;
                pm_project_card(rect(x + cx * (cw + 14), y + cy * (ch + 14), cw, ch), i);
                shown++;
            }
            if (!shown) {
                Rect e = rect(x, y, w, 120);
                gfx_rrect_line(e, 12, 1, C(TH_BORDER2));
                gfx_text_center(FONT_UI, rect(e.x, e.y + 30, e.w, 24), "No projects yet", C(TH_TEXT));
                gfx_text_center(FONT_SMALL, rect(e.x, e.y + 58, e.w, 24), "Create one with New Project or pick a template above.", C(TH_TEXT3));
                y += 140;
            } else y += ((shown + cols - 1) / cols) * (ch + 14);
        }
    } else if (A.pm_tab == PM_LEARN) { pm_learn(rect(x, y, w, 600)); y += 520; }
    else { pm_settings(rect(x, y, w, 700)); y += 640; }
    ui_scroll_end(ID("pmscroll"), main, &A.pm_scroll, y + 30);

    /* detail */
    if (show_detail) {
        ProjectInfo *p = &A.projects.a[A.pm_sel];
        gfx_rect(detail, C(TH_PANEL));
        gfx_rect(rect(detail.x, detail.y, 1, detail.h), C(TH_BORDER));
        thumb(rect(detail.x + 1, detail.y, detail.w - 1, 190), p->seg, p->nseg, true);
        gfx_rect(rect(detail.x, detail.y + 190, detail.w, 1), C(TH_BORDER));
        float dx = detail.x + 18, dw = detail.w - 36, dy = detail.y + 208;
        gfx_text_fit(FONT_HEAD, dx, dy, dw, p->name, C(TH_TEXT));
        dy += 34;
        gfx_text_fit(FONT_MONO_SMALL, dx, dy, dw, p->file, C(TH_TEXT3));
        dy += 28;
        float sw = (dw - 16) / 3;
        int vals[] = { p->nmaps, p->ntex, p->nsectors };
        const char *labs[] = { "Maps", "Textures", "Sectors" };
        for (int i = 0; i < 3; i++) {
            Rect b = rect(dx + i * (sw + 8), dy, sw, 54);
            gfx_rrect(b, 8, C(TH_FIELD));
            gfx_rrect_line(b, 8, 1, C(0x23262D));
            char v[16];
            snprintf(v, sizeof v, "%d", vals[i]);
            gfx_text(FONT_MONO, b.x + 10, b.y + 9, v, C(TH_TEXT));
            gfx_text(FONT_SMALL, b.x + 10, b.y + 30, labs[i], C(TH_TEXT3));
        }
        dy += 70;
        if (p->nmaptitles) {
            gfx_text(FONT_SMALL, dx, dy, "MAPS", C(0xA3ABB8));
            dy += 22;
            for (int i = 0; i < p->nmaptitles; i++) {
                Rect mr = rect(dx, dy, dw, 32);
                gfx_rrect(mr, 8, i == 0 ? C(0x2A1709) : C(TH_FIELD));
                gfx_rrect_line(mr, 8, 1, i == 0 ? C(TH_ACCENT_BD) : C(0x23262D));
                gfx_icon(IC_mode2d, mr.x + 16, mr.y + 16, 14, i == 0 ? C(0xFF8A33) : C(TH_TEXT3));
                gfx_text_fit(FONT_UI, mr.x + 32, mr.y + 8, mr.w - 40, p->maptitles[i], C(TH_TEXT));
                dy += 36;
            }
        }
        float by = detail.y + detail.h - 96;
        if (p->missing) {
            if (ui_button(ID("pd_forget"), rect(dx, by, dw, 40), IC_trash, "Remove from list", BTN_DANGER)) { projects_forget(p->file); projects_scan(&A.projects); A.pm_sel = A.projects.n ? 0 : -1; }
        } else {
            if (ui_button(ID("pd_open"), rect(dx, by, dw, 40), IC_play, "Open Project", BTN_PRIMARY)) pm_open_selected();
            float bw = (dw - 16) / 3;
            if (ui_button(ID("pd_folder"), rect(dx, by + 48, bw, 32), IC_open, "Folder", BTN_DEFAULT)) plat_open_folder(p->dir);
            if (ui_button(ID("pd_dup"), rect(dx + bw + 8, by + 48, bw, 32), IC_copy, "Duplicate", BTN_DEFAULT)) {
                char nf[600];
                if (project_duplicate(p->file, nf, sizeof nf)) { projects_scan(&A.projects); ed_toast("Project duplicated", 1); }
                else message("Duplicate failed", "The project folder could not be copied.");
            }
            if (ui_button(ID("pd_del"), rect(dx + (bw + 8) * 2, by + 48, bw, 32), IC_trash, "Delete", BTN_DANGER)) open_dialog(DLG_DELETE_PROJECT);
        }
    }
    if (!ui.text_focus && in->kpressed[KEY_ENTER] && A.dlg == DLG_NONE && (A.pm_tab == PM_RECENT || A.pm_tab == PM_ALL)) pm_open_selected();
}

/* ------------------------------------------------------------------ loading screen */
static void loading(const Input *in) {
    Rect full = rect(0, 0, in->win_w, in->win_h);
    splash_bg(full, plat_time());
    titlebar(rect(0, 0, in->win_w, 36), "Loading project", false, in);
    double el = plat_time() - A.state_time;
    if (A.load_step == 0 && el > 0.05) {
        sf_strcpy(A.status, sizeof A.status, "Loading project \xC2\xB7 textures and maps");
        A.load_step = 1;
    } else if (A.load_step == 1 && el > 0.2) {
        char err[256];
        if (!ed_open_project(A.load_file, err, sizeof err)) {
            set_state(APP_PM);
            message("Could not open project", err);
            return;
        }
        sf_strcpy(A.status, sizeof A.status, "Triangulating sectors");
        A.load_step = 2;
    } else if (A.load_step == 2 && el > 0.45) {
        for (int s = 0; s < ed.map.sectors.n; s++) { int n; r3d_sector_tris(&ed.map, s, &n); }
        sf_strcpy(A.status, sizeof A.status, "Starting editor");
        A.load_step = 3;
    } else if (A.load_step == 3 && el > 0.8) {
        set_state(APP_EDITOR);
        A.last_autosave = plat_time();
        char t[128];
        snprintf(t, sizeof t, "%s \xE2\x80\x94 Sector Forge", ed.proj.name);
        plat_set_title(t);
        return;
    }
    A.progress = A.load_step / 3.0f;
    A.shown_progress += (A.progress - A.shown_progress) * SF_MIN(1.0f, (float)in->dt * 8);
    Rect card = rect((in->win_w - 520) * 0.5f, (in->win_h - 260) * 0.5f, 520, 260);
    gfx_shadow(card, 16, 24, col(0, 0, 0, 170));
    gfx_rrect(card, 16, col(14, 15, 18, 245));
    gfx_rrect_line(card, 16, 1, C(TH_BORDER2));
    Rect lg = rect(card.x + 32, card.y + 32, 56, 56);
    gfx_rrect(lg, 12, C(0x1D140D));
    gfx_rrect_line(lg, 12, 1, C(TH_ACCENT_BD));
    gfx_icon(IC_logo, lg.x + 28, lg.y + 28, 34, C(TH_ACCENT));
    gfx_text(FONT_SMALL, card.x + 104, card.y + 38, "OPENING PROJECT", C(0xA3ABB8));
    gfx_text_fit(FONT_HEAD, card.x + 104, card.y + 56, card.w - 136, A.load_name, C(TH_TEXT));
    float by = card.y + card.h - 70;
    gfx_text(FONT_UI, card.x + 32, by, A.status, C(TH_TEXT));
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(A.shown_progress * 100 + 0.5f));
    gfx_text_right(FONT_MONO, card.x + card.w - 32, by, pct, C(TH_ACCENT_HI));
    gfx_rrect(rect(card.x + 32, by + 28, card.w - 64, 4), 2, C(TH_BORDER));
    gfx_rrect(rect(card.x + 32, by + 28, (card.w - 64) * A.shown_progress, 4), 2, C(TH_ACCENT));
}

/* ------------------------------------------------------------------ editor screen */
static void map_tabs(Rect r) {
    gfx_rect(r, C(0x121418));
    gfx_rect(rect(r.x, r.y + r.h - 1, r.w, 1), C(TH_BORDER));
    float x = r.x + 8;
    for (int i = 0; i < ed.proj.nmaps && i < 12; i++) {
        char t[96];
        snprintf(t, sizeof t, "%s", ed.proj.maps[i].title);
        float w = gfx_text_w(FONT_UI, t) + 46 + (i == ed.map_idx && ed.dirty ? 12 : 0);
        if (x + w > r.x + r.w - 40) break;
        Rect tr = rect(x, r.y + 4, w, r.h - 4);
        bool on = i == ed.map_idx;
        bool hov = ui_hover(tr);
        if (hov) { ui.hot = ui_id("maptab", i); ui.cursor = on ? CUR_ARROW : CUR_HAND; }
        if (on) {
            gfx_rrect(rect(tr.x, tr.y, tr.w, tr.h + 6), 6, C(TH_VIEW));
            gfx_rect_line(rect(tr.x, tr.y, tr.w, 1), 1, C(TH_BORDER));
        } else if (hov) gfx_rrect(rect(tr.x, tr.y, tr.w, tr.h - 2), 6, C(0x181B20));
        gfx_icon(IC_mode2d, tr.x + 16, tr.y + tr.h * 0.5f, 14, on ? C(TH_TEXT) : C(TH_TEXT3));
        gfx_text(FONT_UI, tr.x + 30, tr.y + (tr.h - gfx_font_h(FONT_UI)) * 0.5f, t, on ? C(0xF1F3F6) : C(TH_TEXT3));
        if (on && ed.dirty) gfx_circle(tr.x + tr.w - 14, tr.y + tr.h * 0.5f, 3, C(TH_ACCENT));
        if (hov && ui.in->mpressed[0] && !on) {
            if (ed.dirty) ed_save();
            if (ed.mode == MODE_PLAY) play_stop();
            ed_load_map(i);
        }
        x += w + 2;
    }
    if (ui_icon_btn(ID("tab_new"), rect(x + 2, r.y + 5, 26, 26), IC_plus, "New map", false)) app_dialog_new_map();
}

static void toast(Rect vp, const Input *in) {
    if (!ed.toast[0]) return;
    double age = in->time - ed.toast_time;
    if (age > 2.4) return;
    float a = (float)SF_CLAMP((2.4 - age) * 4, 0, 1) * (float)SF_CLAMP(age * 10, 0, 1);
    float w = gfx_text_w(FONT_UI, ed.toast) + 50;
    Rect r = rect(vp.x + (vp.w - w) * 0.5f, vp.y + vp.h - 92, w, 34);
    gfx_shadow(r, 10, 10, col(0, 0, 0, (int)(120 * a)));
    gfx_rrect(r, 10, col(22, 24, 29, (int)(245 * a)));
    Col ac = ed.toast_kind == 1 ? C(TH_GREEN) : ed.toast_kind == 2 ? C(TH_RED) : C(TH_TEXT3);
    gfx_rrect_line(r, 10, 1, col(ac.r, ac.g, ac.b, (int)(160 * a)));
    gfx_icon(ed.toast_kind == 1 ? IC_check : ed.toast_kind == 2 ? IC_warn : IC_info, r.x + 18, r.y + 17, 15, col(ac.r, ac.g, ac.b, (int)(255 * a)));
    gfx_text(FONT_UI, r.x + 34, r.y + (34 - gfx_font_h(FONT_UI)) * 0.5f, ed.toast, col(231, 233, 238, (int)(255 * a)));
}

static void editor_screen(const Input *in) {
    float W = in->win_w, H = in->win_h;
    bool play = ed.mode == MODE_PLAY;
    Rect title = rect(0, 0, W, 36), tb = rect(0, 36, W, 46), status = rect(0, H - 28, W, 28);
    Rect body = rect(0, 82, W, H - 82 - 28);
    Rect rail = play ? rect(0, 0, 0, 0) : rect_cut_left(&body, 52);
    Rect panel = play ? rect(0, 0, 0, 0) : rect_cut_right(&body, SF_MIN(352, SF_MAX(300, W * 0.24f)));
    Rect tabs = play ? rect(0, 0, 0, 0) : rect_cut_top(&body, 36);
    Rect vp = body;
    handle_menus();
    /* viewport first: the 3D passes own the GL state while they run */
    if (ed.mode == MODE_3D) ed3d_frame(vp, in);
    else if (ed.mode == MODE_PLAY) play_frame(vp, in);
    else ed2d_frame(vp, in);
    if (ed.mode == MODE_3D) gfx_begin_2d(W, H, in->scale);
    if (!play) toast(vp, in);
    if (!play) {
        map_tabs(tabs);
        ed_tool_rail(rail, in);
        panels_frame(panel, in);
    }
    ed_toolbar(tb, in);
    ed_statusbar(status, in);
    char sub[160];
    snprintf(sub, sizeof sub, "%s \xE2\x80\x94 %s%s", ed.proj.name, ed.map.name, ed.dirty ? "  \xE2\x80\xA2" : "");
    titlebar(title, sub, true, in);
    ed_shortcuts(in);
    /* autosave */
    if (g_settings.autosave && ed.dirty && ed.mode != MODE_PLAY && in->time - A.last_autosave > 120 && in->time - ed.last_edit_time > 2) {
        A.last_autosave = in->time;
        if (ed_save()) ed_toast("Autosaved", 0);
    }
}

/* ------------------------------------------------------------------ dialogs */
void app_dialog_new_map(void) {
    snprintf(A.dlg_name, sizeof A.dlg_name, "E1M%d", ed.proj.nmaps + 1);
    A.dlg_tpl = TPL_EMPTY;
    open_dialog(DLG_NEW_MAP);
}
void app_dialog_delete_map(int idx) { A.dlg_idx = idx; open_dialog(DLG_DELETE_MAP); }
void app_dialog_shortcuts(void) { open_dialog(DLG_SHORTCUTS); }
void app_dialog_about(void) { open_dialog(DLG_ABOUT); }

static void do_close_project(void) {
    ed_close_project();
    plat_set_title("Sector Forge");
    projects_scan(&A.projects);
    A.pm_sel = A.projects.n ? 0 : -1;
    set_state(APP_PM);
}

void app_request_close_project(void) {
    if (ed.dirty) { A.dlg_after = 1; open_dialog(DLG_UNSAVED); return; }
    do_close_project();
}

void app_import_texture(void) {
    if (!ed.open) return;
    char f[600];
    if (!plat_open_file_dialog(f, sizeof f, "Import a texture", "Images", "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif")) return;
    char td[700], name[TEXNAME];
    project_tex_dir(&ed.proj, td, sizeof td);
    if (assets_import_texture(f, td, name)) {
        char msg[96];
        snprintf(msg, sizeof msg, "Imported %s", name);
        ed_toast(msg, 1);
        ed.panel = PANEL_TEXTURES;
        ed.tex_filter = 4;
    } else message("Import failed", "That image could not be read. PNG, JPG, BMP, GIF and TGA files are supported.");
}

static Rect dialog_card(float w, float h, const char *title, int icon) {
    const Input *in = ui.in;
    Rect full = rect(0, 0, in->win_w, in->win_h);
    ui_push_block(full, LAYER_MODAL);
    gfx_rect(full, col(0, 0, 0, 150));
    Rect r = rect((in->win_w - w) * 0.5f, (in->win_h - h) * 0.5f, w, h);
    gfx_shadow(r, 14, 26, col(0, 0, 0, 190));
    gfx_rrect(r, 14, C(TH_PANEL));
    gfx_rrect_line(r, 14, 1, C(TH_BORDER2));
    if (icon >= 0) gfx_icon(icon, r.x + 30, r.y + 32, 18, C(TH_ACCENT));
    gfx_text(FONT_HEAD, r.x + (icon >= 0 ? 48 : 24), r.y + 18, title, C(TH_TEXT));
    if (ui_icon_btn(ID("dlg_x"), rect(r.x + r.w - 44, r.y + 14, 30, 30), IC_close, "Close", false) || in->kpressed[KEY_ESC]) A.dlg = DLG_NONE;
    return r;
}

static void dialogs(const Input *in) {
    if (A.dlg == DLG_NONE) return;
    int saved = ui.layer;
    ui.layer = LAYER_MODAL;
    bool enter = in->kpressed[KEY_ENTER] && !A.dlg_first;
    A.dlg_first = false;
    switch (A.dlg) {
    case DLG_NEW_PROJECT: {
        Rect r = dialog_card(640, 470, "New project", IC_new);
        float x = r.x + 24, y = r.y + 68, w = r.w - 48;
        gfx_text(FONT_SMALL, x, y, "Project name", C(TH_TEXT2));
        y += 20;
        if (ui.focus == 0 && A.dlg_name[0] && !ui.active) { /* focus the name field on open */
            static int opened;
            if (opened != A.dlg) { ui.focus = ui_id("np_name", 0); ui.edit_sel = 0; ui.edit_cur = (int)strlen(A.dlg_name); }
            opened = A.dlg;
        }
        int tr = ui_text(ui_id("np_name", 0), rect(x, y, w, 34), A.dlg_name, sizeof A.dlg_name, "My project", -1);
        if (tr == 2) enter = true;
        y += 48;
        gfx_text(FONT_SMALL, x, y, "Template", C(TH_TEXT2));
        y += 20;
        float cw = (w - 36) / 4;
        for (int t = 0; t < TPL_COUNT; t++) {
            Rect c = rect(x + t * (cw + 12), y, cw, 150);
            bool on = A.dlg_tpl == t, hov = ui_hover(c);
            if (hov) { ui.hot = ui_id("np_tpl", t); ui.cursor = CUR_HAND; }
            if (hov && in->mpressed[0]) A.dlg_tpl = t;
            gfx_rrect(c, 10, on ? C(0x1D140D) : C(TH_FIELD));
            gfx_clip_push(rect(c.x, c.y, c.w, 96));
            thumb(rect(c.x + 1, c.y + 1, c.w - 2, 95), A.tpl_seg[t], A.tpl_nseg[t], on);
            gfx_clip_pop();
            gfx_rrect_line(c, 10, on ? 2.f : 1.f, on ? C(TH_ACCENT) : hov ? C(0x3A3F49) : C(0x23262D));
            gfx_text(FONT_UI_BOLD, c.x + 10, c.y + 104, g_template_names[t], on ? C(TH_ACCENT_HI) : C(TH_TEXT));
            gfx_text_fit(FONT_SMALL, c.x + 10, c.y + 124, c.w - 20, g_template_desc[t], C(TH_TEXT3));
        }
        y += 166;
        gfx_text(FONT_SMALL, x, y, "Location", C(TH_TEXT2));
        y += 20;
        Rect lf = rect(x, y, w - 108, 34);
        gfx_rrect(lf, 6, C(TH_FIELD));
        gfx_rrect_line(lf, 6, 1, C(TH_BORDER2));
        gfx_text_fit(FONT_MONO_SMALL, lf.x + 10, lf.y + 10, lf.w - 20, A.dlg_loc, C(TH_TEXT));
        if (ui_button(ui_id("np_browse", 0), rect(x + w - 100, y, 100, 34), IC_open, "Browse", BTN_DEFAULT)) {
            char dir[512];
            if (plat_pick_folder(dir, sizeof dir, "Where should the project folder go?")) sf_strcpy(A.dlg_loc, sizeof A.dlg_loc, dir);
        }
        y += 44;
        if (A.dlg_err[0]) gfx_text_fit(FONT_SMALL, x, y, w - 260, A.dlg_err, C(TH_RED));
        if (ui_button(ui_id("np_cancel", 0), rect(r.x + r.w - 244, r.y + r.h - 56, 100, 36), -1, "Cancel", BTN_DEFAULT)) A.dlg = DLG_NONE;
        if (ui_button(ui_id("np_create", 0), rect(r.x + r.w - 136, r.y + r.h - 56, 112, 36), IC_check, "Create", BTN_PRIMARY) || enter) {
            Project p;
            plat_mkdirs(A.dlg_loc);
            if (project_create(&p, A.dlg_loc, A.dlg_name, A.dlg_tpl, A.dlg_err, sizeof A.dlg_err)) {
                A.dlg = DLG_NONE;
                ui.focus = 0;
                begin_load(p.file, p.name);
            }
        }
    } break;
    case DLG_DELETE_PROJECT: {
        Rect r = dialog_card(480, 220, "Delete project?", IC_warn);
        ProjectInfo *p = A.pm_sel >= 0 && A.pm_sel < A.projects.n ? &A.projects.a[A.pm_sel] : NULL;
        char m[200];
        snprintf(m, sizeof m, "\"%s\" will be moved to the Recycle Bin.", p ? p->name : "");
        gfx_text_fit(FONT_UI, r.x + 24, r.y + 74, r.w - 48, m, C(TH_TEXT));
        gfx_text_fit(FONT_SMALL, r.x + 24, r.y + 100, r.w - 48, "You can restore it from the Recycle Bin later.", C(TH_TEXT3));
        if (ui_button(ui_id("dp_cancel", 0), rect(r.x + r.w - 244, r.y + r.h - 56, 100, 36), -1, "Cancel", BTN_DEFAULT)) A.dlg = DLG_NONE;
        if (ui_button(ui_id("dp_ok", 0), rect(r.x + r.w - 136, r.y + r.h - 56, 112, 36), IC_trash, "Delete", BTN_DANGER) || enter) {
            if (p) {
                plat_recycle(p->dir);
                projects_forget(p->file);
                projects_scan(&A.projects);
                A.pm_sel = A.projects.n ? 0 : -1;
                ed_toast("Project moved to the Recycle Bin", 1);
            }
            A.dlg = DLG_NONE;
        }
    } break;
    case DLG_NEW_MAP: {
        Rect r = dialog_card(560, 380, "New map", IC_mode2d);
        float x = r.x + 24, y = r.y + 68, w = r.w - 48;
        gfx_text(FONT_SMALL, x, y, "Map name", C(TH_TEXT2));
        y += 20;
        int tr = ui_text(ui_id("nm_name", 0), rect(x, y, w, 34), A.dlg_name, sizeof A.dlg_name, "Map name", -1);
        if (tr == 2) enter = true;
        y += 48;
        gfx_text(FONT_SMALL, x, y, "Start from", C(TH_TEXT2));
        y += 20;
        float cw = (w - 36) / 4;
        for (int t = 0; t < TPL_COUNT; t++) {
            Rect c = rect(x + t * (cw + 12), y, cw, 140);
            bool on = A.dlg_tpl == t, hov = ui_hover(c);
            if (hov) { ui.hot = ui_id("nm_tpl", t); ui.cursor = CUR_HAND; }
            if (hov && in->mpressed[0]) A.dlg_tpl = t;
            gfx_rrect(c, 10, on ? C(0x1D140D) : C(TH_FIELD));
            gfx_clip_push(rect(c.x, c.y, c.w, 90));
            thumb(rect(c.x + 1, c.y + 1, c.w - 2, 89), A.tpl_seg[t], A.tpl_nseg[t], on);
            gfx_clip_pop();
            gfx_rrect_line(c, 10, on ? 2.f : 1.f, on ? C(TH_ACCENT) : C(0x23262D));
            gfx_text(FONT_UI_BOLD, c.x + 10, c.y + 98, g_template_names[t], on ? C(TH_ACCENT_HI) : C(TH_TEXT));
            gfx_text_fit(FONT_SMALL, c.x + 10, c.y + 118, c.w - 20, g_template_desc[t], C(TH_TEXT3));
        }
        if (ui_button(ui_id("nm_cancel", 0), rect(r.x + r.w - 244, r.y + r.h - 56, 100, 36), -1, "Cancel", BTN_DEFAULT)) A.dlg = DLG_NONE;
        if (ui_button(ui_id("nm_ok", 0), rect(r.x + r.w - 136, r.y + r.h - 56, 112, 36), IC_check, "Create", BTN_PRIMARY) || enter) {
            A.dlg = DLG_NONE;
            ui.focus = 0;
            ed_new_map(A.dlg_name[0] ? A.dlg_name : "Untitled", A.dlg_tpl);
        }
    } break;
    case DLG_DELETE_MAP: {
        Rect r = dialog_card(460, 210, "Delete map?", IC_warn);
        char m[200];
        snprintf(m, sizeof m, "\"%s\" will be moved to the Recycle Bin.", A.dlg_idx < ed.proj.nmaps ? ed.proj.maps[A.dlg_idx].title : "");
        gfx_text_fit(FONT_UI, r.x + 24, r.y + 76, r.w - 48, m, C(TH_TEXT));
        if (ui_button(ui_id("dm_cancel", 0), rect(r.x + r.w - 244, r.y + r.h - 56, 100, 36), -1, "Cancel", BTN_DEFAULT)) A.dlg = DLG_NONE;
        if (ui_button(ui_id("dm_ok", 0), rect(r.x + r.w - 136, r.y + r.h - 56, 112, 36), IC_trash, "Delete", BTN_DANGER) || enter) {
            int idx = A.dlg_idx;
            bool current = idx == ed.map_idx;
            if (project_delete_map(&ed.proj, idx)) {
                if (current) ed_load_map(0);
                else if (idx < ed.map_idx) ed.map_idx--;
                ed_toast("Map moved to the Recycle Bin", 1);
            }
            A.dlg = DLG_NONE;
        }
    } break;
    case DLG_UNSAVED: {
        Rect r = dialog_card(480, 210, "Save changes?", IC_save);
        char m[200];
        snprintf(m, sizeof m, "\"%s\" has unsaved changes.", ed.map.name);
        gfx_text_fit(FONT_UI, r.x + 24, r.y + 76, r.w - 48, m, C(TH_TEXT));
        if (ui_button(ui_id("us_cancel", 0), rect(r.x + 24, r.y + r.h - 56, 96, 36), -1, "Cancel", BTN_DEFAULT)) A.dlg = DLG_NONE;
        bool discard = ui_button(ui_id("us_discard", 0), rect(r.x + r.w - 266, r.y + r.h - 56, 120, 36), -1, "Don't save", BTN_DEFAULT);
        bool save = ui_button(ui_id("us_save", 0), rect(r.x + r.w - 136, r.y + r.h - 56, 112, 36), IC_save, "Save", BTN_PRIMARY) || enter;
        if (save || discard) {
            if (save) ed_save();
            else ed.dirty = false;
            A.dlg = DLG_NONE;
            if (A.dlg_after == 0) { settings_save(); plat_quit(); }
            else do_close_project();
        }
    } break;
    case DLG_SHORTCUTS: {
        Rect r = dialog_card(760, 560, "Keyboard shortcuts", IC_keyboard);
        static const char *k2[][2] = {
            { "S", "Select tool" }, { "D", "Draw sector" }, { "R", "Rectangle" }, { "O", "Polygon / circle" }, { "L", "Draw lines" },
            { "I / X", "Insert vertex / split" }, { "M", "Move" }, { "T", "Place thing" }, { "U", "Measure" }, { "B", "Paint texture" },
            { "1 2 3 4", "Vertices / Lines / Sectors / Things" }, { "[  ]", "Grid size" }, { "G", "Grid tool" }, { "J / Shift+J", "Join / merge sectors" },
            { "H / V", "Mirror" }, { "Ctrl+[ ]", "Rotate 90\xC2\xB0" }, { "Arrows", "Nudge selection" }, { "Space / MMB", "Pan" }, { "Home", "Zoom to fit" },
        };
        static const char *k3[][2] = {
            { "Tab", "Switch 2D / 3D" }, { "Wheel", "Raise / lower floor or ceiling" }, { "Ctrl+Wheel", "Fine step (1)" }, { "RMB + WASD", "Fly (Q/E down/up)" },
            { "Alt+Click", "Pick texture" }, { "P", "Paste texture" }, { "Arrows", "Align texture" }, { "+ / \xE2\x88\x92", "Sector light" },
            { "F / G", "Fullbright / wireframe" }, { "F5", "Play from start" }, { "Shift+F5", "Play from camera" }, { "E", "Use door / lift (play)" },
            { "Ctrl+S", "Save" }, { "Ctrl+Z / Y", "Undo / redo" }, { "Ctrl+C X V D", "Copy, cut, paste, duplicate" }, { "Del", "Delete" },
        };
        float x0 = r.x + 24, x1 = r.x + r.w * 0.5f + 8, y = r.y + 70;
        gfx_text(FONT_SMALL, x0, y, "2D EDITING", C(0xA3ABB8));
        gfx_text(FONT_SMALL, x1, y, "3D, PLAY & GENERAL", C(0xA3ABB8));
        y += 24;
        for (int i = 0; i < SF_COUNT(k2); i++) {
            float kw;
            ui_kbd(x0, y + i * 23, k2[i][0], &kw);
            gfx_text(FONT_SMALL, x0 + 110, y + i * 23 + 1, k2[i][1], C(TH_TEXT2));
        }
        for (int i = 0; i < SF_COUNT(k3); i++) {
            float kw;
            ui_kbd(x1, y + i * 23, k3[i][0], &kw);
            gfx_text(FONT_SMALL, x1 + 110, y + i * 23 + 1, k3[i][1], C(TH_TEXT2));
        }
    } break;
    case DLG_ABOUT: {
        Rect r = dialog_card(460, 260, "About", -1);
        Rect lg = rect(r.x + 24, r.y + 66, 56, 56);
        gfx_rrect(lg, 12, C(0x1D140D));
        gfx_rrect_line(lg, 12, 1, C(TH_ACCENT_BD));
        gfx_icon(IC_logo, lg.x + 28, lg.y + 28, 34, C(TH_ACCENT));
        gfx_text(FONT_HEAD, r.x + 96, r.y + 70, "Sector Forge", C(TH_TEXT));
        gfx_text(FONT_MONO_SMALL, r.x + 96, r.y + 102, "Version " SF_VERSION " \xC2\xB7 C11 \xC2\xB7 OpenGL", C(TH_TEXT3));
        gfx_text_fit(FONT_UI, r.x + 24, r.y + 144, r.w - 48, "A Doom-style sector map editor and game engine.", C(TH_TEXT2));
        gfx_text_fit(FONT_SMALL, r.x + 24, r.y + 170, r.w - 48, "All textures, sprites and icons are generated procedurally.", C(TH_TEXT3));
        if (ui_button(ui_id("ab_ok", 0), rect(r.x + r.w - 124, r.y + r.h - 56, 100, 36), -1, "Close", BTN_DEFAULT) || enter) A.dlg = DLG_NONE;
    } break;
    case DLG_MESSAGE: {
        Rect r = dialog_card(480, 210, A.dlg_title, IC_info);
        gfx_text_fit(FONT_UI, r.x + 24, r.y + 76, r.w - 48, A.dlg_msg, C(TH_TEXT));
        if (ui_button(ui_id("msg_ok", 0), rect(r.x + r.w - 124, r.y + r.h - 56, 100, 36), -1, "OK", BTN_PRIMARY) || enter) A.dlg = DLG_NONE;
    } break;
    default: A.dlg = DLG_NONE;
    }
    ui.layer = saved;
}

/* ------------------------------------------------------------------ platform callbacks */
bool app_init(void) {
    settings_load();
    plat_set_vsync(g_settings.vsync);
    gfx_init();
    map_init(&ed.map);
    map_init(&ed.clip);
    A.t0 = plat_time();
    set_state(APP_SPLASH);
    sf_strcpy(A.status, sizeof A.status, "Starting");
    return true;
}

void app_frame(const Input *in) {
    if (fabsf(in->scale - A.scale) > 0.01f) build_fonts(in->scale);
    A.win_w = in->win_w;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, (GLsizei)(in->win_w * in->scale + 0.5f), (GLsizei)(in->win_h * in->scale + 0.5f));
    glClearColor(0.055f, 0.06f, 0.07f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gfx_begin_2d(in->win_w, in->win_h, in->scale);
    ui_begin(in);
    A.fps_frames++;
    if (in->time - A.fps_t > 0.5) { ed.fps = (float)(A.fps_frames / (in->time - A.fps_t)); A.fps_frames = 0; A.fps_t = in->time; }
    switch (A.state) {
    case APP_SPLASH: splash(in); break;
    case APP_PM:
        project_manager(in);
        if (ed.toast[0] && in->time - ed.toast_time < 2.4) toast(rect(232, 36, in->win_w - 232, in->win_h - 36), in);
        break;
    case APP_LOADING: loading(in); break;
    default: editor_screen(in); break;
    }
    dialogs(in);
    ui_end();
}

bool app_on_close(void) {
    if (A.state == APP_EDITOR && ed.dirty) {
        if (ed.mode == MODE_PLAY) play_stop();
        A.dlg_after = 0;
        open_dialog(DLG_UNSAVED);
        return false;
    }
    settings_save();
    return true;
}

int app_hit_test(float x, float y) {
    if (A.state == APP_SPLASH) return (x > A.win_w - 46 && y < 36) ? 0 : 1;
    if (y >= 36) return 0;
    if (A.dlg != DLG_NONE) return 0;
    return ui_is_caption_free(x, y) ? 1 : 0;
}

void app_shutdown(void) {
    if (ed.open && ed.dirty) ed_save();
    settings_save();
}
