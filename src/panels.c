/* Sector Forge - editor chrome: toolbar, tool rail, properties / textures / things / map panels, status bar, shortcuts */
#include "editor.h"
#include "assets.h"
#include "plat.h"
#include <stddef.h>

static Col C(uint32_t h) { return col_hex(h); }
#define ID(s) ui_id(s, __LINE__)

/* ================================================================== toolbar */
void ed_toolbar(Rect r, const Input *in) {
    (void)in;
    gfx_rect(r, C(0x14161A));
    gfx_rect(rect(r.x, r.y + r.h - 1, r.w, 1), C(TH_BORDER));
    float x = r.x + 10, y = r.y + 7, h = 32;
    if (ui_icon_btn(ID("tb_new"), rect(x, y, 32, h), IC_new, "New map  (Ctrl+N)", false)) app_dialog_new_map();
    x += 34;
    if (ui_icon_btn(ID("tb_open"), rect(x, y, 32, h), IC_open, "Maps in this project", ed.panel == PANEL_MAP)) ed.panel = PANEL_MAP;
    x += 34;
    if (ui_icon_btn(ID("tb_save"), rect(x, y, 32, h), IC_save, "Save  (Ctrl+S)", false)) { if (ed_save()) ed_toast("Saved", 1); }
    x += 38;
    gfx_rect(rect(x, y + 5, 1, 22), C(TH_BORDER2));
    x += 8;
    if (ui_icon_btn(ID("tb_undo"), rect(x, y, 32, h), IC_undo, "Undo  (Ctrl+Z)", false)) ed_undo();
    x += 34;
    if (ui_icon_btn(ID("tb_redo"), rect(x, y, 32, h), IC_redo, "Redo  (Ctrl+Y)", false)) ed_redo();
    x += 38;
    gfx_rect(rect(x, y + 5, 1, 22), C(TH_BORDER2));
    x += 8;
    /* mode segmented control */
    Rect seg = rect(x, y + 1, 268, 30);
    gfx_rrect(seg, 8, C(0x121418));
    gfx_rrect_line(seg, 8, 1, C(TH_BORDER2));
    static const char *ml[] = { "2D Map", "3D View", "Play" };
    static const int mi[] = { IC_mode2d, IC_cube, IC_play };
    float sx = seg.x + 2;
    for (int i = 0; i < 3; i++) {
        float bw = i == 2 ? 72 : 96;
        if (ui_button_on(ui_id("tb_mode", i), rect(sx, seg.y + 2, bw, 26), mi[i], ml[i], BTN_SEG, (int)ed.mode == i)) ed_set_mode((EdMode)i);
        sx += bw + 1;
    }
    x += 276;
    if (ed.mode == MODE_2D) {
        gfx_rect(rect(x, y + 5, 1, 22), C(TH_BORDER2));
        x += 10;
        gfx_text(FONT_SMALL, x, y + 16 - gfx_font_h(FONT_SMALL) * 0.5f, "Select", C(TH_TEXT2));
        x += gfx_text_w(FONT_SMALL, "Select") + 6;
        Rect g = rect(x, y + 1, 4 * 30 + 6, 30);
        gfx_rrect(g, 8, C(0x121418));
        gfx_rrect_line(g, 8, 1, C(TH_BORDER2));
        static const char *sn[] = { "Vertices  (1)", "Lines  (2)", "Sectors  (3)", "Things  (4)" };
        static const int si[] = { IC_vertex, IC_line, IC_sector, IC_thing };
        for (int i = 0; i < 4; i++)
            if (ui_icon_btn(ui_id("tb_sel", i), rect(g.x + 3 + i * 30, g.y + 2, 28, 26), si[i], sn[i], ed.selmode == i)) ed_set_selmode(i);
        x += g.w + 8;
    } else if (ed.mode == MODE_3D) {
        gfx_rect(rect(x, y + 5, 1, 22), C(TH_BORDER2));
        x += 10;
        if (ui_icon_btn(ID("tb_wire"), rect(x, y, 32, h), IC_wireframe, "Wireframe overlay  (G)", ed.wireframe)) ed.wireframe = !ed.wireframe;
        x += 34;
        if (ui_icon_btn(ID("tb_lit"), rect(x, y, 32, h), IC_light, "Fullbright  (F)", ed.fullbright)) ed.fullbright = !ed.fullbright;
        x += 38;
        gfx_text(FONT_SMALL, x, y + 16 - gfx_font_h(FONT_SMALL) * 0.5f, "Step", C(TH_TEXT2));
        x += gfx_text_w(FONT_SMALL, "Step") + 6;
        static const int steps[] = { 1, 4, 8, 16, 32 };
        for (int i = 0; i < 5; i++) {
            char t[8];
            snprintf(t, sizeof t, "%d", steps[i]);
            if (ui_button_on(ui_id("tb_step", i), rect(x, y + 3, 30, 26), -1, t, BTN_SEG, ed.hstep == steps[i])) ed.hstep = steps[i];
            x += 31;
        }
        x += 6;
    }
    if (ed.mode != MODE_PLAY) {
        gfx_rect(rect(x, y + 5, 1, 22), C(TH_BORDER2));
        x += 8;
        if (ui_icon_btn(ID("tb_grid"), rect(x, y, 32, h), IC_grid, "Grid tool  (G)", ed.grid_popup)) { ed.grid_popup = !ed.grid_popup; if (ed.mode != MODE_2D) ed_set_mode(MODE_2D); }
        x += 34;
        if (ui_icon_btn(ID("tb_gm"), rect(x, y + 3, 26, 26), IC_minus, "Smaller grid  [", false)) ed_grid_step(-1);
        x += 28;
        char gs[16];
        snprintf(gs, sizeof gs, "%d", ed.grid);
        gfx_text_center(FONT_MONO, rect(x, y, 38, h), gs, C(TH_ACCENT_HI));
        x += 40;
        if (ui_icon_btn(ID("tb_gp"), rect(x, y + 3, 26, 26), IC_plus, "Larger grid  ]", false)) ed_grid_step(1);
        x += 30;
        if (ui_icon_btn(ID("tb_snap"), rect(x, y, 32, h), IC_magnet, "Snap to grid", ed.snap)) ed.snap = !ed.snap;
        x += 34;
    }
    /* play button on the right */
    if (r.x + r.w - 160 > x) {
        Rect pb = rect(r.x + r.w - 150, y + 1, 140, 30);
        if (ed.mode == MODE_PLAY) { if (ui_button(ID("tb_stop"), pb, IC_stop, "Stop", BTN_DANGER)) play_stop(); }
        else {
            if (ui_button(ID("tb_play"), pb, -1, NULL, BTN_PRIMARY)) ed_set_mode(MODE_PLAY);
            gfx_icon(IC_play, pb.x + 18, pb.y + 15, 14, C(0x1A0D02));
            gfx_text(FONT_UI_BOLD, pb.x + 32, pb.y + (30 - gfx_font_h(FONT_UI_BOLD)) * 0.5f, "Play Map", C(0x1A0D02));
            Rect kb = rect(pb.x + pb.w - 34, pb.y + 6, 26, 18);
            gfx_rrect(kb, 4, C(0xC95F12));
            gfx_text_center(FONT_MONO_SMALL, kb, "F5", C(0x1A0D02));
        }
    }
}

/* ================================================================== rail */
void ed_tool_rail(Rect r, const Input *in) {
    (void)in;
    gfx_rect(r, C(0x14161A));
    gfx_rect(rect(r.x + r.w - 1, r.y, 1, r.h), C(TH_BORDER));
    float y = r.y + 8, x = r.x + (r.w - 40) * 0.5f;
    if (ed.mode == MODE_2D) {
        static const struct { int tool, icon; const char *tip; } T[] = {
            { T_SELECT, IC_select, "Select  (S)" }, { T_PAN, IC_hand, "Pan  (Space / MMB)" }, { -1, 0, NULL },
            { T_DRAW, IC_draw_sector, "Draw Sector  (D)" }, { T_RECT, IC_rect, "Rectangle Sector  (R)" }, { T_POLY, IC_polygon, "Polygon / Circle  (O)" },
            { T_LINES, IC_line, "Draw Lines  (L)" }, { -1, 0, NULL },
            { T_VERTEX, IC_vertex, "Insert Vertex  (I)" }, { T_SPLIT, IC_split, "Split Line  (X)" }, { -2, IC_join, "Join Sectors  (J)" }, { -1, 0, NULL },
            { T_MOVE, IC_move, "Move  (M)" }, { T_ROTATE, IC_rotate, "Rotate" }, { T_SCALE, IC_scale, "Scale" }, { -3, IC_flip, "Mirror  (H / V)" }, { -1, 0, NULL },
            { T_THING, IC_thing, "Place Thing  (T)" }, { T_MEASURE, IC_measure, "Measure  (U)" }, { T_PAINT, IC_bucket, "Paint Texture  (B)" },
        };
        for (int i = 0; i < SF_COUNT(T); i++) {
            if (T[i].tool == -1) { gfx_rect(rect(r.x + 12, y + 4, r.w - 24, 1), C(TH_BORDER2)); y += 10; continue; }
            if (y + 40 > r.y + r.h) break;
            bool on = T[i].tool >= 0 && ed.tool == T[i].tool;
            if (ui_icon_btn(ui_id("rail", i), rect(x, y, 40, 40), T[i].icon, T[i].tip, on)) {
                if (T[i].tool >= 0) ed_set_tool(T[i].tool);
                else if (T[i].tool == -2) ed_join(in->shift);
                else if (T[i].tool == -3) ed_mirror(in->shift);
            }
            y += 42;
        }
    } else if (ed.mode == MODE_3D) {
        static const struct { int tool, icon; const char *tip; } T[] = {
            { T3_SELECT, IC_select, "Select surface  (S)" }, { T3_FLY, IC_cube, "Fly camera  (hold RMB)" }, { -1, 0, NULL },
            { T3_HEIGHT, IC_height, "Raise / Lower  (wheel)" }, { T3_PAINT, IC_bucket, "Apply texture  (B)" }, { T3_PICK, IC_eyedropper, "Pick texture  (Alt+click)" }, { -1, 0, NULL },
            { T3_ALIGN, IC_move, "Align texture  (arrows)" }, { T3_LIGHT, IC_light, "Sector light  (+ / \xE2\x88\x92)" },
        };
        for (int i = 0; i < SF_COUNT(T); i++) {
            if (T[i].tool == -1) { gfx_rect(rect(r.x + 12, y + 4, r.w - 24, 1), C(TH_BORDER2)); y += 10; continue; }
            if (ui_icon_btn(ui_id("rail3", i), rect(x, y, 40, 40), T[i].icon, T[i].tip, ed.tool3d == T[i].tool)) ed.tool3d = T[i].tool;
            y += 42;
        }
    }
}

/* ================================================================== status bar */
void ed_statusbar(Rect r, const Input *in) {
    (void)in;
    gfx_rect(r, C(TH_TITLE));
    gfx_rect(rect(r.x, r.y, r.w, 1), C(TH_BORDER));
    float x = r.x + 12, cy = r.y + r.h * 0.5f;
    float fh = gfx_font_h(FONT_SMALL);
    char t[160];
    static const char *modes[] = { "2D Map", "3D View", "Playing" };
    gfx_circle(x + 3, cy, 3, ed.mode == MODE_PLAY ? C(TH_RED) : C(TH_ACCENT));
    x = gfx_text(FONT_SMALL, x + 12, cy - fh * 0.5f, modes[ed.mode], C(TH_TEXT2)) + 18;
    snprintf(t, sizeof t, "Grid %d", ed.grid);
    x = gfx_text(FONT_SMALL, x, cy - fh * 0.5f, t, C(TH_TEXT2)) + 18;
    x = gfx_text(FONT_SMALL, x, cy - fh * 0.5f, ed.snap ? "Snap on" : "Snap off", C(TH_TEXT2)) + 18;
    if (ed.mode == MODE_2D) snprintf(t, sizeof t, "X %.0f  Y %.0f", ed.snap_pt.x, ed.snap_pt.y);
    else if (ed.mode == MODE_3D) snprintf(t, sizeof t, "Camera  X %.0f  Y %.0f  Z %.0f", ed.cam.x, ed.cam.y, ed.cam.z);
    else snprintf(t, sizeof t, "X %.0f  Y %.0f  Z %.0f", ed.play.x, ed.play.y, ed.play.z);
    x = gfx_text(FONT_MONO_SMALL, x, cy - gfx_font_h(FONT_MONO_SMALL) * 0.5f, t, C(TH_TEXT2)) + 18;
    snprintf(t, sizeof t, "Sectors %d \xC2\xB7 Lines %d \xC2\xB7 Vertices %d \xC2\xB7 Things %d", ed.map.sectors.n, ed.map.lines.n, ed.map.verts.n, ed.map.things.n);
    if (x + gfx_text_w(FONT_SMALL, t) < r.x + r.w - 260) gfx_text(FONT_SMALL, x, cy - fh * 0.5f, t, C(TH_TEXT3));
    /* right side */
    float rx = r.x + r.w - 12;
    snprintf(t, sizeof t, "OpenGL \xC2\xB7 %.0f fps", ed.fps);
    rx -= gfx_text_w(FONT_MONO_SMALL, t);
    gfx_text(FONT_MONO_SMALL, rx, cy - gfx_font_h(FONT_MONO_SMALL) * 0.5f, t, C(TH_TEXT3));
    rx -= 20;
    const char *st = ed.dirty ? "Unsaved changes" : "Saved";
    rx -= gfx_text_w(FONT_SMALL, st);
    gfx_text(FONT_SMALL, rx, cy - fh * 0.5f, st, ed.dirty ? C(TH_ACCENT_HI) : C(TH_TEXT2));
    gfx_icon(ed.dirty ? IC_dot : IC_check, rx - 10, cy, ed.dirty ? 8 : 13, ed.dirty ? C(TH_ACCENT) : C(TH_GREEN));
}

/* ================================================================== panel helpers */
typedef struct { float x, y, w; } Lay;

static void sec_title(Lay *L, int icon, const char *t) {
    Rect a = rect(L->x, L->y, L->w, 24);
    ui_section_title(&a, icon, t);
    L->y = a.y;
}
static void divider(Lay *L) { gfx_rect(rect(L->x - 14, L->y + 4, L->w + 28, 1), C(TH_BORDER)); L->y += 16; }
static void flabel(float x, float y, const char *t) { gfx_text(FONT_SMALL, x, y, t, C(TH_TEXT2)); }

static bool tex_button(uint32_t id, Rect r, const char *label, const char *tex, const char *sub) {
    bool hov = ui_hover(r);
    if (hov) { ui.hot = id; ui.cursor = CUR_HAND; ui_tooltip("Pick from the texture browser"); }
    bool click = hov && ui.in->mreleased[0];
    flabel(r.x, r.y, label);
    Rect b = rect(r.x, r.y + 18, r.w, r.h - 18);
    gfx_rrect(b, 6, C(TH_FIELD));
    gfx_rrect_line(b, 6, 1, hov ? C(0x3A3F49) : C(TH_BORDER2));
    float ts = b.h - 8;
    Rect tr = rect(b.x + 4, b.y + 4, ts, ts);
    if (tex && tex[0]) {
        const Tex *t = tex_get(tex);
        gfx_quad_tex(t, tr, 0, 0, 1, 1, col(255, 255, 255, 255));
    } else gfx_rrect(tr, 4, C(0x1B1E24));
    gfx_rect_line(tr, 1, col(255, 255, 255, 20));
    gfx_text_fit(FONT_MONO_SMALL, tr.x + ts + 8, b.y + 6, b.w - ts - 16, tex && tex[0] ? tex : "\xE2\x80\x94", C(TH_TEXT));
    if (sub) gfx_text_fit(FONT_SMALL, tr.x + ts + 8, b.y + 24, b.w - ts - 16, sub, C(TH_TEXT3));
    return click;
}

static void open_textures(int target) { ed.panel = PANEL_TEXTURES; ed.tex_target = target; }

/* ---- multi-selection helpers for sector fields ---- */
static int first_sel_sector(void) { for (int i = 0; i < ed.map.sectors.n; i++) if (ed.map.sectors.a[i].sel) return i; return -1; }

static bool sector_float_field(uint32_t id, Rect r, size_t off, float step, int dec, float mn, float mx, const char *undo) {
    Map *m = &ed.map;
    int f = first_sel_sector();
    if (f < 0) return false;
    float v = *(float *)((char *)&m->sectors.a[f] + off);
    bool mixed = false;
    for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel && *(float *)((char *)&m->sectors.a[i] + off) != v) mixed = true;
    float nv = v;
    if (ui_number_mixed(id, r, &nv, step, dec, mn, mx, mixed)) {
        ed_snapshot_merge(undo);
        for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) *(float *)((char *)&m->sectors.a[i] + off) = nv;
        return true;
    }
    return false;
}

static void props_sectors(Lay *L) {
    Map *m = &ed.map;
    int n = map_sel_count(m, SM_SECTORS), f = first_sel_sector();
    MSector *S = &m->sectors.a[f];
    /* header */
    Rect ic = rect(L->x, L->y, 36, 36);
    gfx_rrect(ic, 8, C(TH_ACCENT_DK));
    gfx_icon(IC_sector, ic.x + 18, ic.y + 18, 18, C(0xFF8A33));
    if (n == 1) {
        static char nb[32];
        static int nb_for = -1;
        if (nb_for != f || ui.focus != ID("secname")) { sf_strcpy(nb, sizeof nb, S->name); nb_for = f; }
        int r = ui_text(ID("secname"), rect(L->x + 44, L->y, L->w - 44, 26), nb, sizeof nb, "Unnamed sector", -1);
        if (r) { ed_snapshot_merge("Rename sector"); sf_strcpy(S->name, sizeof S->name, nb); }
        char sub[64];
        snprintf(sub, sizeof sub, "Sector %d \xC2\xB7 %d lines", f, map_sector_line_count(m, f));
        gfx_text(FONT_MONO_SMALL, L->x + 46, L->y + 28, sub, C(TH_TEXT3));
    } else {
        char t[48];
        snprintf(t, sizeof t, "%d sectors selected", n);
        gfx_text(FONT_UI_BOLD, L->x + 46, L->y + 4, t, C(TH_TEXT));
        gfx_text(FONT_SMALL, L->x + 46, L->y + 22, "Edits apply to all of them", C(TH_TEXT3));
    }
    L->y += 48;
    divider(L);
    sec_title(L, IC_height, "Heights");
    float hw = (L->w - 10) * 0.5f;
    flabel(L->x, L->y, "Floor");
    flabel(L->x + hw + 10, L->y, "Ceiling");
    L->y += 18;
    sector_float_field(ID("sfl"), rect(L->x, L->y, hw, 28), offsetof(MSector, floor_h), (float)ed.hstep > 0 ? (float)ed.hstep : 8, 1, -32768, 32768, "Floor height");
    sector_float_field(ID("scl"), rect(L->x + hw + 10, L->y, hw, 28), offsetof(MSector, ceil_h), (float)ed.hstep > 0 ? (float)ed.hstep : 8, 1, -32768, 32768, "Ceiling height");
    L->y += 36;
    char rh[32];
    snprintf(rh, sizeof rh, "%g", S->ceil_h - S->floor_h);
    flabel(L->x, L->y, "Room height");
    gfx_text_right(FONT_MONO_SMALL, L->x + L->w, L->y, rh, S->ceil_h - S->floor_h < 56 ? C(TH_ACCENT_HI) : C(TH_TEXT));
    L->y += 24;
    divider(L);
    sec_title(L, IC_texture, "Surfaces");
    if (tex_button(ID("sft"), rect(L->x, L->y, hw, 66), "Floor", S->floor_tex, "click to change")) open_textures(TEXT_FLOOR);
    if (tex_button(ID("sct"), rect(L->x + hw + 10, L->y, hw, 66), "Ceiling", S->ceil_tex, "click to change")) open_textures(TEXT_CEIL);
    L->y += 74;
    if (ui_button(ID("swt"), rect(L->x, L->y, L->w, 28), IC_wall, "Set walls from current texture", BTN_DEFAULT)) {
        TexEntry e;
        (void)e;
        int save = ed.tex_target;
        ed.tex_target = TEXT_WALL;
        ed_apply_texture(ed.cur_wall);
        ed.tex_target = save;
    }
    L->y += 36;
    flabel(L->x, L->y, "Floor offset X / Y");
    flabel(L->x + hw + 10, L->y, "Scale / Rotation");
    L->y += 18;
    float qw = (hw - 6) * 0.5f;
    sector_float_field(ID("sfox"), rect(L->x, L->y, qw, 28), offsetof(MSector, fox), 8, 1, -4096, 4096, "Floor offset");
    sector_float_field(ID("sfoy"), rect(L->x + qw + 6, L->y, qw, 28), offsetof(MSector, foy), 8, 1, -4096, 4096, "Floor offset");
    sector_float_field(ID("sfsc"), rect(L->x + hw + 10, L->y, qw, 28), offsetof(MSector, fscale), 0.25f, 2, 0.0625f, 16, "Floor scale");
    sector_float_field(ID("sfro"), rect(L->x + hw + 16 + qw, L->y, qw, 28), offsetof(MSector, frot), 15, 0, -360, 360, "Floor rotation");
    L->y += 38;
    divider(L);
    sec_title(L, IC_light, "Lighting & type");
    float lv = (float)S->light;
    flabel(L->x, L->y, "Light level");
    char lt[16];
    snprintf(lt, sizeof lt, "%d", S->light);
    gfx_text_right(FONT_MONO_SMALL, L->x + L->w, L->y, lt, C(TH_TEXT));
    L->y += 18;
    if (ui_slider(ID("slight"), rect(L->x, L->y, L->w, 20), &lv, 0, 255, 0)) {
        ed_snapshot_merge("Light level");
        for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) m->sectors.a[i].light = (int)lv;
    }
    L->y += 30;
    flabel(L->x, L->y, "Sector type");
    flabel(L->x + hw + 10, L->y, "Tag");
    L->y += 18;
    int type = S->type;
    if (ui_dropdown(ID("stype"), rect(L->x, L->y, hw, 28), g_sector_type_names, ST_COUNT, &type)) {
        ed_snapshot("Sector type");
        for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) m->sectors.a[i].type = type;
    }
    float tag = (float)S->tag;
    if (ui_number(ID("stag"), rect(L->x + hw + 10, L->y, hw, 28), &tag, 1, 0, 0, 65535, true)) {
        ed_snapshot_merge("Sector tag");
        for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) m->sectors.a[i].tag = (int)tag;
    }
    L->y += 36;
    static const char *type_help[ST_COUNT] = {
        "A regular room.", "Opens when used (E) in play mode. Draw it open; it starts closed.",
        "Lowers to the lowest neighbour floor when used, then rises.", "Hurts the player standing on it.",
        "Counts as a secret when entered.", "Light flickers randomly.", "Ends the level when entered." };
    gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, type_help[S->type], C(TH_TEXT3));
    L->y += 24;
    if (n >= 2) {
        divider(L);
        if (ui_button(ID("sjoin"), rect(L->x, L->y, hw, 30), IC_join, "Join", BTN_DEFAULT)) ed_join(false);
        if (ui_button(ID("smerge"), rect(L->x + hw + 10, L->y, hw, 30), IC_join, "Merge", BTN_DEFAULT)) ed_join(true);
        L->y += 38;
    }
}

static void side_editor(Lay *L, MLine *l, int k, const char *title) {
    MSide *sd = &l->side[k];
    float hw = (L->w - 10) * 0.5f;
    char t[64];
    snprintf(t, sizeof t, "%s \xC2\xB7 sector %d", title, sd->sector);
    sec_title(L, IC_wall, t);
    bool two = l->side[1].sector >= 0;
    float tw3 = (L->w - 12) / 3;
    if (two) {
        if (tex_button(ui_id("supper", k), rect(L->x, L->y, tw3, 66), "Upper", sd->upper, NULL)) open_textures(TEXT_WALL);
        if (tex_button(ui_id("smid", k), rect(L->x + tw3 + 6, L->y, tw3, 66), "Middle", sd->middle, NULL)) open_textures(TEXT_WALL);
        if (tex_button(ui_id("slow", k), rect(L->x + (tw3 + 6) * 2, L->y, tw3, 66), "Lower", sd->lower, NULL)) open_textures(TEXT_WALL);
    } else {
        if (tex_button(ui_id("smid1", k), rect(L->x, L->y, L->w, 66), "Wall", sd->middle, "click to change")) open_textures(TEXT_WALL);
    }
    L->y += 74;
    flabel(L->x, L->y, "Offset X");
    flabel(L->x + hw + 10, L->y, "Offset Y");
    L->y += 18;
    float ox = sd->offx, oy = sd->offy;
    if (ui_number(ui_id("sox", k), rect(L->x, L->y, hw, 28), &ox, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); sd->offx = ox; }
    if (ui_number(ui_id("soy", k), rect(L->x + hw + 10, L->y, hw, 28), &oy, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); sd->offy = oy; }
    L->y += 38;
}

static void props_lines(Lay *L) {
    Map *m = &ed.map;
    int n = map_sel_count(m, SM_LINES), f = -1;
    for (int i = 0; i < m->lines.n; i++) if (m->lines.a[i].sel) { f = i; break; }
    MLine *l = &m->lines.a[f];
    Rect ic = rect(L->x, L->y, 36, 36);
    gfx_rrect(ic, 8, C(TH_ACCENT_DK));
    gfx_icon(IC_line, ic.x + 18, ic.y + 18, 18, C(0xFF8A33));
    char t[64];
    if (n == 1) snprintf(t, sizeof t, "Line %d", f);
    else snprintf(t, sizeof t, "%d lines selected", n);
    gfx_text(FONT_UI_BOLD, L->x + 46, L->y + 2, t, C(TH_TEXT));
    snprintf(t, sizeof t, "Length %.1f \xC2\xB7 %s", map_line_length(m, f), l->side[1].sector >= 0 ? "two-sided" : "one-sided");
    gfx_text(FONT_MONO_SMALL, L->x + 46, L->y + 22, t, C(TH_TEXT3));
    L->y += 48;
    divider(L);
    sec_title(L, IC_settings, "Flags");
    bool blk = (l->flags & LF_BLOCK) != 0, up = (l->flags & LF_UPPER_UNPEG) != 0, lo = (l->flags & LF_LOWER_UNPEG) != 0;
#define FLAGT(label, var, bit) \
    if (ui_toggle(ID(label), rect(L->x, L->y, L->w, 26), label, &var)) { \
        ed_snapshot("Line flag"); \
        for (int i = 0; i < m->lines.n; i++) if (m->lines.a[i].sel) { if (var) m->lines.a[i].flags |= bit; else m->lines.a[i].flags &= ~bit; } \
    } \
    L->y += 30;
    FLAGT("Blocks the player", blk, LF_BLOCK)
    FLAGT("Upper texture unpegged", up, LF_UPPER_UNPEG)
    FLAGT("Lower texture unpegged", lo, LF_LOWER_UNPEG)
#undef FLAGT
    divider(L);
    if (n == 1) {
        side_editor(L, l, 0, "Front side");
        if (l->side[1].sector >= 0) { divider(L); side_editor(L, l, 1, "Back side"); }
        divider(L);
    }
    float hw = (L->w - 10) * 0.5f;
    if (ui_button(ID("lflip"), rect(L->x, L->y, hw, 30), IC_flip, "Flip", BTN_DEFAULT)) ed_flip_lines();
    if (ui_button(ID("lsplit"), rect(L->x + hw + 10, L->y, hw, 30), IC_split, "Split tool", BTN_DEFAULT)) ed_set_tool(T_SPLIT);
    L->y += 40;
}

static void props_verts(Lay *L) {
    Map *m = &ed.map;
    int n = map_sel_count(m, SM_VERTS), f = -1;
    for (int i = 0; i < m->verts.n; i++) if (m->verts.a[i].sel) { f = i; break; }
    Rect ic = rect(L->x, L->y, 36, 36);
    gfx_rrect(ic, 8, C(TH_ACCENT_DK));
    gfx_icon(IC_vertex, ic.x + 18, ic.y + 18, 18, C(0xFF8A33));
    char t[64];
    if (n == 1) snprintf(t, sizeof t, "Vertex %d", f);
    else snprintf(t, sizeof t, "%d vertices selected", n);
    gfx_text(FONT_UI_BOLD, L->x + 46, L->y + 9, t, C(TH_TEXT));
    L->y += 48;
    divider(L);
    if (n == 1) {
        float hw = (L->w - 10) * 0.5f;
        flabel(L->x, L->y, "X");
        flabel(L->x + hw + 10, L->y, "Y");
        L->y += 18;
        float x = (float)m->verts.a[f].x, y = (float)m->verts.a[f].y;
        bool ch = false;
        if (ui_number(ID("vx"), rect(L->x, L->y, hw, 28), &x, (float)ed.grid, 2, -65536, 65536, true)) ch = true;
        if (ui_number(ID("vy"), rect(L->x + hw + 10, L->y, hw, 28), &y, (float)ed.grid, 2, -65536, 65536, true)) ch = true;
        if (ch) {
            ed_snapshot_merge("Move vertex");
            m->verts.a[f].x = x; m->verts.a[f].y = y;
            m->geo_rev++;
            r3d_invalidate();
        }
        L->y += 40;
    }
    gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Delete a vertex between two lines to merge them.", C(TH_TEXT3));
    L->y += 24;
}

static void angle_buttons(Lay *L, float *ang, bool *changed) {
    static const char *lab[] = { "E", "NE", "N", "NW", "W", "SW", "S", "SE" };
    float bw = (L->w - 7 * 4) / 8;
    for (int i = 0; i < 8; i++) {
        if (ui_button_on(ui_id("ang", i), rect(L->x + i * (bw + 4), L->y, bw, 26), -1, lab[i], BTN_SEG, fabsf(*ang - i * 45) < 0.5f)) { *ang = (float)(i * 45); *changed = true; }
        if (fabsf(*ang - i * 45) >= 0.5f) gfx_rrect_line(rect(L->x + i * (bw + 4), L->y, bw, 26), 6, 1, C(TH_BORDER2));
    }
    L->y += 34;
}

static void props_things(Lay *L) {
    Map *m = &ed.map;
    int n = map_sel_count(m, SM_THINGS), f = -1;
    for (int i = 0; i < m->things.n; i++) if (m->things.a[i].sel) { f = i; break; }
    MThing *t = &m->things.a[f];
    Rect ic = rect(L->x, L->y, 36, 36);
    gfx_rrect(ic, 8, C(TH_ACCENT_DK));
    gfx_icon(IC_thing, ic.x + 18, ic.y + 18, 18, C(0xFF8A33));
    char s[64];
    if (n == 1) snprintf(s, sizeof s, "%s", g_thing_info[t->type].name);
    else snprintf(s, sizeof s, "%d things selected", n);
    gfx_text(FONT_UI_BOLD, L->x + 46, L->y + 2, s, C(TH_TEXT));
    snprintf(s, sizeof s, "Thing %d", f);
    gfx_text(FONT_MONO_SMALL, L->x + 46, L->y + 22, s, C(TH_TEXT3));
    L->y += 48;
    divider(L);
    sec_title(L, IC_thing, "Thing");
    static const char *names[TH_COUNT];
    for (int i = 0; i < TH_COUNT; i++) names[i] = g_thing_info[i].name;
    flabel(L->x, L->y, "Type");
    L->y += 18;
    int type = t->type;
    if (ui_dropdown(ID("ttype"), rect(L->x, L->y, L->w, 28), names, TH_COUNT, &type)) ed_apply_thing_type(type);
    L->y += 38;
    flabel(L->x, L->y, "Angle");
    L->y += 18;
    float ang = t->angle;
    bool ch = false;
    if (ui_number(ID("tang"), rect(L->x, L->y, L->w, 28), &ang, 45, 0, -360, 720, true)) ch = true;
    L->y += 36;
    angle_buttons(L, &ang, &ch);
    if (ch) {
        ed_snapshot_merge("Thing angle");
        ang = fmodf(ang + 720, 360);
        for (int i = 0; i < m->things.n; i++) if (m->things.a[i].sel) m->things.a[i].angle = ang;
    }
    if (n == 1) {
        float hw = (L->w - 10) * 0.5f;
        flabel(L->x, L->y, "X");
        flabel(L->x + hw + 10, L->y, "Y");
        L->y += 18;
        float x = (float)t->x, y = (float)t->y;
        bool mv = false;
        if (ui_number(ID("tx"), rect(L->x, L->y, hw, 28), &x, (float)ed.grid, 1, -65536, 65536, true)) mv = true;
        if (ui_number(ID("ty"), rect(L->x + hw + 10, L->y, hw, 28), &y, (float)ed.grid, 1, -65536, 65536, true)) mv = true;
        if (mv) { ed_snapshot_merge("Move thing"); t->x = x; t->y = y; }
        L->y += 40;
    }
}

static void props_defaults(Lay *L) {
    gfx_icon(IC_info, L->x + 8, L->y + 9, 16, C(TH_TEXT3));
    gfx_text(FONT_UI_BOLD, L->x + 24, L->y, "Nothing selected", C(TH_TEXT));
    L->y += 22;
    gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Click to select, drag to box-select. 1-4 switch mode.", C(TH_TEXT3));
    L->y += 28;
    divider(L);
    sec_title(L, IC_settings, "New sector defaults");
    MapDefaults *d = &ed.proj.def;
    float hw = (L->w - 10) * 0.5f;
    flabel(L->x, L->y, "Floor");
    flabel(L->x + hw + 10, L->y, "Ceiling");
    L->y += 18;
    ui_number(ID("dfl"), rect(L->x, L->y, hw, 28), &d->floor_h, 8, 0, -32768, 32768, true);
    ui_number(ID("dcl"), rect(L->x + hw + 10, L->y, hw, 28), &d->ceil_h, 8, 0, -32768, 32768, true);
    L->y += 38;
    if (tex_button(ID("dft"), rect(L->x, L->y, hw, 66), "Floor texture", d->floor_tex, NULL)) open_textures(TEXT_FLOOR);
    if (tex_button(ID("dct"), rect(L->x + hw + 10, L->y, hw, 66), "Ceiling texture", d->ceil_tex, NULL)) open_textures(TEXT_CEIL);
    L->y += 74;
    if (tex_button(ID("dwt"), rect(L->x, L->y, L->w, 66), "Wall texture (new lines)", ed.cur_wall, "the current wall texture")) open_textures(TEXT_WALL);
    L->y += 74;
    float lv = (float)d->light;
    flabel(L->x, L->y, "Light level");
    L->y += 18;
    if (ui_slider(ID("dlight"), rect(L->x, L->y, L->w, 20), &lv, 0, 255, 0)) d->light = (int)lv;
    L->y += 30;
    if (ui_button(ID("dsave"), rect(L->x, L->y, L->w, 30), IC_save, "Save as project defaults", BTN_DEFAULT)) {
        sf_strcpy(d->floor_tex, TEXNAME, ed.cur_flat);
        sf_strcpy(d->wall_tex, TEXNAME, ed.cur_wall);
        project_save(&ed.proj);
        ed_toast("Project defaults saved", 1);
    }
    L->y += 40;
}

/* 3D surface properties */
static void props_3d(Lay *L) {
    Map *m = &ed.map;
    if (!ed.sel3d.n) {
        gfx_icon(IC_info, L->x + 8, L->y + 9, 16, C(TH_TEXT3));
        gfx_text(FONT_UI_BOLD, L->x + 24, L->y, "No surface selected", C(TH_TEXT));
        L->y += 22;
        gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Click a floor, ceiling or wall. Shift adds.", C(TH_TEXT3));
        L->y += 18;
        gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Scroll the wheel to raise or lower it.", C(TH_TEXT3));
        L->y += 30;
        return;
    }
    uint32_t id = ed.sel3d.a[0];
    int k = SURF_KIND(id), idx = SURF_IDX(id);
    static const char *kn[] = { "", "Floor", "Ceiling", "Upper wall", "Wall", "Lower wall", "Thing" };
    static const int ki[] = { 0, IC_floor, IC_ceiling, IC_wall, IC_wall, IC_wall, IC_thing };
    Rect ic = rect(L->x, L->y, 36, 36);
    gfx_rrect(ic, 8, C(TH_ACCENT_DK));
    gfx_icon(ki[k], ic.x + 18, ic.y + 18, 18, C(0xFF8A33));
    char t[96];
    int s = -1;
    if (k == SURF_FLOOR || k == SURF_CEIL) s = idx;
    else if (k >= SURF_UPPER && k <= SURF_LOWER) s = m->lines.a[idx / 2].side[idx & 1].sector;
    if (ed.sel3d.n > 1) snprintf(t, sizeof t, "%d surfaces", ed.sel3d.n);
    else if (s >= 0 && m->sectors.a[s].name[0]) snprintf(t, sizeof t, "%s \xE2\x80\x94 %s", kn[k], m->sectors.a[s].name);
    else snprintf(t, sizeof t, "%s", kn[k]);
    gfx_text_fit(FONT_UI_BOLD, L->x + 46, L->y + 2, L->w - 46, t, C(TH_TEXT));
    if (k == SURF_THING) snprintf(t, sizeof t, "Thing %d", idx);
    else if (k >= SURF_UPPER) snprintf(t, sizeof t, "Line %d \xC2\xB7 %s side \xC2\xB7 sector %d", idx / 2, (idx & 1) ? "back" : "front", s);
    else snprintf(t, sizeof t, "Sector %d", idx);
    gfx_text(FONT_MONO_SMALL, L->x + 46, L->y + 22, t, C(TH_TEXT3));
    L->y += 48;
    divider(L);
    float hw = (L->w - 10) * 0.5f;
    if (k == SURF_FLOOR || k == SURF_CEIL) {
        MSector *S = &m->sectors.a[idx];
        sec_title(L, IC_height, "Height");
        Rect hb = rect(L->x, L->y, L->w, 64);
        gfx_rrect(hb, 8, C(TH_FIELD));
        gfx_rrect_line(hb, 8, 1, C(TH_BORDER2));
        gfx_icon(IC_wheel, hb.x + 24, hb.y + 32, 26, C(0xFF8A33));
        float v = k == SURF_FLOOR ? S->floor_h : S->ceil_h;
        char vb[32];
        snprintf(vb, sizeof vb, "%g", v);
        gfx_text(FONT_HEAD, hb.x + 48, hb.y + 8, vb, C(0xFFFFFF));
        snprintf(vb, sizeof vb, "step %d \xC2\xB7 %s %g", ed.hstep, k == SURF_FLOOR ? "ceiling" : "floor", k == SURF_FLOOR ? S->ceil_h : S->floor_h);
        gfx_text(FONT_SMALL, hb.x + 48, hb.y + 40, vb, C(TH_TEXT2));
        if (ui_icon_btn(ID("hup"), rect(hb.x + hb.w - 40, hb.y + 6, 32, 24), IC_chev_up, "Raise", false)) {
            ed_snapshot_merge("Change height");
            for (int i = 0; i < ed.sel3d.n; i++) {
                int kk = SURF_KIND(ed.sel3d.a[i]), ii = SURF_IDX(ed.sel3d.a[i]);
                if (kk == SURF_FLOOR) m->sectors.a[ii].floor_h = SF_MIN(m->sectors.a[ii].floor_h + ed.hstep, m->sectors.a[ii].ceil_h);
                if (kk == SURF_CEIL) m->sectors.a[ii].ceil_h += ed.hstep;
            }
        }
        if (ui_icon_btn(ID("hdn"), rect(hb.x + hb.w - 40, hb.y + 34, 32, 24), IC_chev_down, "Lower", false)) {
            ed_snapshot_merge("Change height");
            for (int i = 0; i < ed.sel3d.n; i++) {
                int kk = SURF_KIND(ed.sel3d.a[i]), ii = SURF_IDX(ed.sel3d.a[i]);
                if (kk == SURF_FLOOR) m->sectors.a[ii].floor_h -= ed.hstep;
                if (kk == SURF_CEIL) m->sectors.a[ii].ceil_h = SF_MAX(m->sectors.a[ii].ceil_h - ed.hstep, m->sectors.a[ii].floor_h);
            }
        }
        L->y += 72;
        static const int steps[] = { 1, 4, 8, 16, 32 };
        float bw = (L->w - 16) / 5;
        for (int i = 0; i < 5; i++) {
            char st[16];
            snprintf(st, sizeof st, "step %d", steps[i]);
            if (ui_button_on(ui_id("pstep", i), rect(L->x + i * (bw + 4), L->y, bw, 26), -1, st, BTN_SEG, ed.hstep == steps[i])) ed.hstep = steps[i];
            if (ed.hstep != steps[i]) gfx_rrect_line(rect(L->x + i * (bw + 4), L->y, bw, 26), 6, 1, C(TH_BORDER2));
        }
        L->y += 36;
        divider(L);
        sec_title(L, IC_texture, "Texture");
        if (tex_button(ID("ptex"), rect(L->x, L->y, L->w, 66), "Applied", k == SURF_FLOOR ? S->floor_tex : S->ceil_tex, "click a texture in the library to apply")) ed.panel = PANEL_TEXTURES;
        L->y += 74;
        flabel(L->x, L->y, "Offset X");
        flabel(L->x + hw + 10, L->y, "Offset Y");
        L->y += 18;
        float *ox = k == SURF_FLOOR ? &S->fox : &S->cox, *oy = k == SURF_FLOOR ? &S->foy : &S->coy;
        float *sc = k == SURF_FLOOR ? &S->fscale : &S->cscale, *ro = k == SURF_FLOOR ? &S->frot : &S->crot;
        float a = *ox, b = *oy;
        if (ui_number(ID("p3ox"), rect(L->x, L->y, hw, 28), &a, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); *ox = a; }
        if (ui_number(ID("p3oy"), rect(L->x + hw + 10, L->y, hw, 28), &b, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); *oy = b; }
        L->y += 36;
        flabel(L->x, L->y, "Scale");
        flabel(L->x + hw + 10, L->y, "Rotation");
        L->y += 18;
        a = *sc; b = *ro;
        if (ui_number(ID("p3sc"), rect(L->x, L->y, hw, 28), &a, 0.25f, 2, 0.0625f, 16, true)) { ed_snapshot_merge("Texture scale"); *sc = a; }
        if (ui_number(ID("p3ro"), rect(L->x + hw + 10, L->y, hw, 28), &b, 15, 0, -360, 360, true)) { ed_snapshot_merge("Texture rotation"); *ro = b; }
        L->y += 38;
    } else if (k >= SURF_UPPER && k <= SURF_LOWER) {
        MSide *sd = &m->lines.a[idx / 2].side[idx & 1];
        sec_title(L, IC_texture, "Texture");
        const char *tx = k == SURF_UPPER ? sd->upper : k == SURF_MID ? sd->middle : sd->lower;
        if (tex_button(ID("pwtex"), rect(L->x, L->y, L->w, 66), "Applied", tx, "click a texture in the library to apply")) ed.panel = PANEL_TEXTURES;
        L->y += 74;
        flabel(L->x, L->y, "Offset X");
        flabel(L->x + hw + 10, L->y, "Offset Y");
        L->y += 18;
        float a = sd->offx, b = sd->offy;
        if (ui_number(ID("pwox"), rect(L->x, L->y, hw, 28), &a, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); sd->offx = a; }
        if (ui_number(ID("pwoy"), rect(L->x + hw + 10, L->y, hw, 28), &b, 8, 1, -4096, 4096, true)) { ed_snapshot_merge("Texture offset"); sd->offy = b; }
        L->y += 38;
        MLine *ln = &m->lines.a[idx / 2];
        bool up = (ln->flags & LF_UPPER_UNPEG) != 0, lo = (ln->flags & LF_LOWER_UNPEG) != 0;
        if (ui_toggle(ID("pwup"), rect(L->x, L->y, L->w, 26), "Upper texture unpegged", &up)) { ed_snapshot("Line flag"); ln->flags ^= LF_UPPER_UNPEG; }
        L->y += 30;
        if (ui_toggle(ID("pwlo"), rect(L->x, L->y, L->w, 26), "Lower texture unpegged", &lo)) { ed_snapshot("Line flag"); ln->flags ^= LF_LOWER_UNPEG; }
        L->y += 34;
    } else if (k == SURF_THING) {
        sec_title(L, IC_thing, "Thing");
        gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Switch to the 2D view (Tab) to move or retype things.", C(TH_TEXT3));
        L->y += 24;
        return;
    }
    if (s >= 0) {
        divider(L);
        sec_title(L, IC_light, "Sector light");
        float lv = (float)m->sectors.a[s].light;
        if (ui_slider(ID("p3light"), rect(L->x, L->y, L->w - 40, 20), &lv, 0, 255, 0)) { ed_snapshot_merge("Light level"); m->sectors.a[s].light = (int)lv; }
        char lt[16];
        snprintf(lt, sizeof lt, "%d", m->sectors.a[s].light);
        gfx_text_right(FONT_MONO_SMALL, L->x + L->w, L->y + 2, lt, C(TH_TEXT));
        L->y += 32;
    }
}

/* ================================================================== textures tab */
static void panel_textures(Lay *L, Rect area) {
    (void)area;
    ui_text(ID("tsearch"), rect(L->x, L->y, L->w - 82, 30), ed.tex_search, sizeof ed.tex_search, "Search textures", IC_search);
    if (ui_button(ID("timport"), rect(L->x + L->w - 76, L->y, 76, 30), IC_import, "Import", BTN_DEFAULT)) app_import_texture();
    L->y += 38;
    static const char *flt[] = { "All", "Walls", "Flats", "Liquids", "Project" };
    float fx = L->x;
    for (int i = 0; i < 5; i++) {
        float w = gfx_text_w(FONT_UI_BOLD, flt[i]) + 22;
        if (ui_button_on(ui_id("tflt", i), rect(fx, L->y, w, 24), -1, flt[i], BTN_SEG, ed.tex_filter == i)) ed.tex_filter = i;
        if (ed.tex_filter != i) gfx_rrect_line(rect(fx, L->y, w, 24), 6, 1, C(TH_BORDER2));
        fx += w + 4;
    }
    L->y += 32;
    if (ed.mode == MODE_2D) {
        gfx_text(FONT_SMALL, L->x, L->y + 5, "Apply to", C(TH_TEXT2));
        static const char *tg[] = { "Auto", "Floor", "Ceiling", "Walls" };
        float tx = L->x + 58;
        float bw = (L->w - 58 - 9) / 4;
        for (int i = 0; i < 4; i++) {
            if (ui_button_on(ui_id("ttgt", i), rect(tx, L->y, bw, 24), -1, tg[i], BTN_SEG, ed.tex_target == i)) ed.tex_target = i;
            if (ed.tex_target != i) gfx_rrect_line(rect(tx, L->y, bw, 24), 6, 1, C(TH_BORDER2));
            tx += bw + 3;
        }
        L->y += 32;
    }
    /* current */
    float hw = (L->w - 10) * 0.5f;
    tex_button(ID("curw"), rect(L->x, L->y, hw, 60), "Current wall", ed.cur_wall, NULL);
    tex_button(ID("curf"), rect(L->x + hw + 10, L->y, hw, 60), "Current flat", ed.cur_flat, NULL);
    L->y += 70;
    /* grid */
    int cols = 3;
    float gap = 8, cw = (L->w - gap * (cols - 1)) / cols, ch = cw + 18;
    int col_i = 0;
    for (int i = 0; i < g_assets.tex.n; i++) {
        TexEntry *e = &g_assets.tex.a[i];
        if (ed.tex_search[0] && !sf_stristr(e->name, ed.tex_search)) continue;
        if (ed.tex_filter == 1 && (e->cat != TEXCAT_WALL || !e->builtin)) continue;
        if (ed.tex_filter == 2 && e->cat != TEXCAT_FLAT && e->cat != TEXCAT_SKY) continue;
        if (ed.tex_filter == 3 && e->cat != TEXCAT_LIQUID) continue;
        if (ed.tex_filter == 4 && e->builtin) continue;
        Rect r = rect(L->x + col_i * (cw + gap), L->y, cw, ch);
        bool cur = !strcmp(e->name, ed.cur_wall) || !strcmp(e->name, ed.cur_flat);
        bool hov = ui_hover(r);
        uint32_t id = ui_id("texcell", i);
        if (hov) { ui.hot = id; ui.cursor = CUR_HAND; ui_tooltip(e->name); }
        gfx_rrect(r, 6, cur ? C(0x2A1709) : hov ? C(0x1B1E24) : C(TH_FIELD));
        gfx_rrect_line(r, 6, 1, cur ? C(TH_ACCENT) : hov ? C(0x3A3F49) : C(0x23262D));
        Rect tr = rect(r.x + 4, r.y + 4, cw - 8, cw - 8);
        if (r.y + r.h > gfx_clip_current().y && r.y < gfx_clip_current().y + gfx_clip_current().h)
            gfx_quad_tex(&e->tex, tr, 0, 0, 1, 1, col(255, 255, 255, 255));
        gfx_text_fit(FONT_MONO_SMALL, r.x + 5, r.y + cw - 1, cw - 10, e->name, cur ? C(TH_ACCENT_HI) : C(TH_TEXT2));
        if (!e->builtin) gfx_circle(r.x + r.w - 9, r.y + 9, 4, C(TH_BLUE));
        if (hov && ui.in->mreleased[0] && ui.layer >= ui.top_layer) ed_apply_texture(e->name);
        col_i++;
        if (col_i == cols) { col_i = 0; L->y += ch + gap; }
    }
    if (col_i) L->y += ch + gap;
}

/* ================================================================== things tab */
static void panel_things(Lay *L) {
    gfx_text_fit(FONT_SMALL, L->x, L->y, L->w, "Click a type to place it with the Thing tool (T).", C(TH_TEXT3));
    L->y += 24;
    static const char *cats[] = { "Player", "Lights", "Enemies", "Pickups", "Props" };
    for (int c = 0; c <= TC_PROP; c++) {
        sec_title(L, -1, cats[c]);
        for (int i = 0; i < TH_COUNT; i++) {
            if (g_thing_info[i].category != c) continue;
            Rect r = rect(L->x, L->y, L->w, 44);
            bool on = ed.thing_type == i;
            bool hov = ui_hover(r);
            uint32_t id = ui_id("throw", i);
            if (hov) { ui.hot = id; ui.cursor = CUR_HAND; }
            gfx_rrect(r, 8, on ? C(0x2A1709) : hov ? C(0x1B1E24) : C(TH_FIELD));
            gfx_rrect_line(r, 8, 1, on ? C(TH_ACCENT_BD) : C(0x23262D));
            Rect sp = rect(r.x + 4, r.y + 2, 20, 40);
            gfx_quad_tex(&g_assets.sprites[i], sp, 0, 0, 1, 1, col(255, 255, 255, 255));
            gfx_circle(r.x + 36, r.y + 22, 5, col_hex(g_thing_info[i].color));
            gfx_text(FONT_UI, r.x + 48, r.y + 6, g_thing_info[i].name, on ? C(TH_ACCENT_HI) : C(TH_TEXT));
            char d[64];
            snprintf(d, sizeof d, "radius %g \xC2\xB7 height %g", g_thing_info[i].radius, g_thing_info[i].height);
            gfx_text(FONT_SMALL, r.x + 48, r.y + 24, d, C(TH_TEXT3));
            int cnt = 0;
            for (int k = 0; k < ed.map.things.n; k++) if (ed.map.things.a[k].type == i) cnt++;
            snprintf(d, sizeof d, "%d", cnt);
            gfx_text_right(FONT_MONO_SMALL, r.x + r.w - 10, r.y + 14, d, C(TH_TEXT3));
            if (hov && ui.in->mreleased[0]) {
                ed_apply_thing_type(i);
                if (ed.mode == MODE_2D && !map_sel_count(&ed.map, SM_THINGS)) ed_set_tool(T_THING);
            }
            L->y += 48;
        }
        L->y += 6;
    }
}

/* ================================================================== map tab */
static void panel_map(Lay *L) {
    Map *m = &ed.map;
    sec_title(L, IC_mode2d, "This map");
    static char nb[64];
    if (ui.focus != ID("mapname")) sf_strcpy(nb, sizeof nb, m->name);
    flabel(L->x, L->y, "Name");
    L->y += 18;
    if (ui_text(ID("mapname"), rect(L->x, L->y, L->w, 30), nb, sizeof nb, "Map name", -1)) {
        sf_strcpy(m->name, sizeof m->name, nb);
        sf_strcpy(ed.proj.maps[ed.map_idx].title, sizeof ed.proj.maps[ed.map_idx].title, nb);
        ed_mark_dirty();
    }
    L->y += 40;
    float cw = (L->w - 12) / 4;
    const char *lab[] = { "Sectors", "Lines", "Vertices", "Things" };
    int val[] = { m->sectors.n, m->lines.n, m->verts.n, m->things.n };
    for (int i = 0; i < 4; i++) {
        Rect r = rect(L->x + i * (cw + 4), L->y, cw, 50);
        gfx_rrect(r, 8, C(TH_FIELD));
        gfx_rrect_line(r, 8, 1, C(0x23262D));
        char v[16];
        snprintf(v, sizeof v, "%d", val[i]);
        gfx_text(FONT_MONO, r.x + 8, r.y + 7, v, C(TH_TEXT));
        gfx_text(FONT_SMALL, r.x + 8, r.y + 28, lab[i], C(TH_TEXT3));
    }
    L->y += 62;
    divider(L);
    sec_title(L, IC_layers, "Maps in project");
    for (int i = 0; i < ed.proj.nmaps; i++) {
        Rect r = rect(L->x, L->y, L->w, 36);
        bool on = i == ed.map_idx;
        bool hov = ui_hover(r);
        uint32_t id = ui_id("maprow", i);
        if (hov) { ui.hot = id; ui.cursor = CUR_HAND; }
        gfx_rrect(r, 8, on ? C(0x2A1709) : hov ? C(0x1B1E24) : C(TH_FIELD));
        gfx_rrect_line(r, 8, 1, on ? C(TH_ACCENT_BD) : C(0x23262D));
        gfx_icon(IC_mode2d, r.x + 16, r.y + 18, 15, on ? C(0xFF8A33) : C(TH_TEXT3));
        gfx_text_fit(FONT_UI, r.x + 32, r.y + 9, r.w - 110, ed.proj.maps[i].title, on ? C(TH_ACCENT_HI) : C(TH_TEXT));
        gfx_text_right(FONT_MONO_SMALL, r.x + r.w - (ed.proj.nmaps > 1 ? 40 : 10), r.y + 11, ed.proj.maps[i].file, C(TH_TEXT3));
        bool del_hot = false;
        if (ed.proj.nmaps > 1) {
            Rect dr = rect(r.x + r.w - 34, r.y + 5, 26, 26);
            del_hot = rect_has(dr, ui.mx, ui.my);
            if (ui_icon_btn(ui_id("mapdel", i), dr, IC_trash, "Delete map", false)) app_dialog_delete_map(i);
        }
        if (hov && !del_hot && ui.in->mreleased[0] && !on) {
            if (ed.dirty) ed_save();
            if (ed.mode == MODE_PLAY) play_stop();
            ed_load_map(i);
        }
        L->y += 40;
    }
    if (ui_button(ID("mapnew"), rect(L->x, L->y, L->w, 32), IC_plus, "New map\xE2\x80\xA6", BTN_DEFAULT)) app_dialog_new_map();
    L->y += 42;
    divider(L);
    sec_title(L, IC_open, "Project");
    gfx_text_fit(FONT_UI, L->x, L->y, L->w, ed.proj.name, C(TH_TEXT));
    L->y += 20;
    gfx_text_fit(FONT_MONO_SMALL, L->x, L->y, L->w, ed.proj.dir, C(TH_TEXT3));
    L->y += 24;
    float hw = (L->w - 10) * 0.5f;
    if (ui_button(ID("mapfolder"), rect(L->x, L->y, hw, 30), IC_open, "Open folder", BTN_DEFAULT)) plat_open_folder(ed.proj.dir);
    if (ui_button(ID("mapclose"), rect(L->x + hw + 10, L->y, hw, 30), IC_close, "Close project", BTN_DEFAULT)) app_request_close_project();
    L->y += 40;
}

/* ================================================================== panel frame */
void panels_frame(Rect r, const Input *in) {
    (void)in;
    gfx_rect(r, C(TH_PANEL));
    gfx_rect(rect(r.x, r.y, 1, r.h), C(TH_BORDER));
    Rect tabs = rect(r.x, r.y, r.w, 38);
    gfx_rect(rect(r.x, tabs.y + tabs.h - 1, r.w, 1), C(TH_BORDER));
    static const char *tl[] = { "Properties", "Textures", "Things", "Map" };
    ui_tabs(ID("ptabs"), rect(r.x + 4, r.y, r.w - 8, 38), tl, 4, &ed.panel);
    Rect body = rect(r.x + 1, r.y + 38, r.w - 1, r.h - 38);
    float *scroll = &ed.panel_scroll[ed.panel];
    float y0 = ui_scroll_begin(ui_id("pscroll", ed.panel), body, scroll);
    Lay L = { body.x + 14, y0 + 14, body.w - 28 - 6 };
    switch (ed.panel) {
    case PANEL_PROPS:
        if (ed.mode == MODE_3D) props_3d(&L);
        else if (ed.mode == MODE_PLAY) {
            gfx_text(FONT_UI_BOLD, L.x, L.y, "Playing", C(TH_TEXT));
            L.y += 24;
            gfx_text_fit(FONT_SMALL, L.x, L.y, L.w, "Esc returns to the editor. E uses doors & lifts.", C(TH_TEXT3));
            L.y += 30;
        } else if (map_sel_count(&ed.map, ed.selmode)) {
            if (ed.selmode == SM_SECTORS) props_sectors(&L);
            else if (ed.selmode == SM_LINES) props_lines(&L);
            else if (ed.selmode == SM_VERTS) props_verts(&L);
            else props_things(&L);
        } else props_defaults(&L);
        break;
    case PANEL_TEXTURES: panel_textures(&L, body); break;
    case PANEL_THINGS: panel_things(&L); break;
    default: panel_map(&L); break;
    }
    ui_scroll_end(ui_id("pscroll", ed.panel), body, scroll, L.y + 10);
}

/* ================================================================== shortcuts */
void ed_shortcuts(const Input *in) {
    if (ui.text_focus || ed.mode == MODE_PLAY || ui.top_layer >= LAYER_MODAL) return;
    bool c = in->ctrl, s = in->shift;
    const bool *k = in->kpressed;
    if (c && k['S']) { if (ed_save()) ed_toast("Saved", 1); return; }
    if (c && k['Z'] && !s) { ed_undo(); return; }
    if ((c && k['Y']) || (c && s && k['Z'])) { ed_redo(); return; }
    if (c && k['N']) { app_dialog_new_map(); return; }
    if (k[KEY_F1]) { app_dialog_shortcuts(); return; }
    if (k[KEY_F5]) { if (s) { if (!ed.cam_valid) ed3d_sync_camera_from_2d(); play_start(true); } else ed_set_mode(MODE_PLAY); return; }
    if (k[KEY_TAB]) { ed_set_mode(ed.mode == MODE_2D ? MODE_3D : MODE_2D); return; }
    if (k[KEY_DELETE]) { ed_delete(); return; }
    if (ed.mode == MODE_3D) {
        if (c && k['A']) ed_select_all();
        if (!c && k['F']) ed.fullbright = !ed.fullbright;
        if (!c && k['G']) ed.wireframe = !ed.wireframe;
        if (!c && k['B']) ed.tool3d = ed.tool3d == T3_PAINT ? T3_SELECT : T3_PAINT;
        if (k[KEY_LBRACKET]) { static const int st[] = { 1, 4, 8, 16, 32 }; for (int i = 4; i >= 0; i--) if (st[i] < ed.hstep) { ed.hstep = st[i]; break; } }
        if (k[KEY_RBRACKET]) { static const int st[] = { 1, 4, 8, 16, 32 }; for (int i = 0; i < 5; i++) if (st[i] > ed.hstep) { ed.hstep = st[i]; break; } }
        return;
    }
    /* 2D */
    if (c) {
        if (k['C']) ed_copy(false);
        if (k['X']) ed_copy(true);
        if (k['V']) ed_paste();
        if (k['D']) ed_duplicate();
        if (k['A']) ed_select_all();
        if (k['I']) ed_invert_selection();
        if (k['F']) ed_flip_lines();
        if (k[KEY_RBRACKET]) ed_rotate90(true);
        if (k[KEY_LBRACKET]) ed_rotate90(false);
        return;
    }
    if (k['1']) ed_set_selmode(SM_VERTS);
    if (k['2']) ed_set_selmode(SM_LINES);
    if (k['3']) ed_set_selmode(SM_SECTORS);
    if (k['4']) ed_set_selmode(SM_THINGS);
    if (k['S']) ed_set_tool(T_SELECT);
    if (k['D']) ed_set_tool(T_DRAW);
    if (k['R']) ed_set_tool(T_RECT);
    if (k['O']) ed_set_tool(T_POLY);
    if (k['L']) ed_set_tool(T_LINES);
    if (k['I']) ed_set_tool(T_VERTEX);
    if (k['X']) ed_set_tool(T_SPLIT);
    if (k['M']) ed_set_tool(T_MOVE);
    if (k['T']) ed_set_tool(T_THING);
    if (k['U']) ed_set_tool(T_MEASURE);
    if (k['B']) ed_set_tool(T_PAINT);
    if (k['J']) ed_join(s);
    if (k['H']) ed_mirror(false);
    if (k['V']) ed_mirror(true);
    if (k['G']) ed.grid_popup = !ed.grid_popup;
    if (k[KEY_LBRACKET]) ed_grid_step(-1);
    if (k[KEY_RBRACKET]) ed_grid_step(1);
    if (k[KEY_HOME]) ed_fit_view();
    if (k[KEY_ESC] && !ed.drawing && ed.drag == DRAG_NONE) {
        if (ed.grid_popup) ed.grid_popup = false;
        else if (ed.tool != T_SELECT) ed_set_tool(T_SELECT);
        else map_select_none(&ed.map);
    }
}
