/* Headless test harness: POSIX platform layer + scripted input driving the whole app.
 * Build (Linux): see tests/run_harness.sh.  Not part of the Windows build. */
#define _GNU_SOURCE
#include "../src/plat.h"
#include "../src/editor.h"
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include "GL/gl.h"

static double g_time;
static const char *ROOT = "/tmp/sftest";

FILE *plat_fopen(const char *p, const char *m) {
    char q[1200]; sf_strcpy(q, sizeof q, p);
    for (char *c = q; *c; c++) if (*c == '\\') *c = '/';
    return fopen(q, m);
}
static void fix(const char *p, char *q) { sf_strcpy(q, 1200, p); for (char *c = q; *c; c++) if (*c == '\\') *c = '/'; }
bool plat_exists(const char *p) { char q[1200]; fix(p, q); struct stat s; return stat(q, &s) == 0; }
bool plat_is_dir(const char *p) { char q[1200]; fix(p, q); struct stat s; return stat(q, &s) == 0 && S_ISDIR(s.st_mode); }
int64_t plat_mtime(const char *p) { char q[1200]; fix(p, q); struct stat s; return stat(q, &s) == 0 ? (int64_t)s.st_mtime : 0; }
bool plat_mkdirs(const char *p) { char q[1200]; fix(p, q); char cmd[1300]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", q); return system(cmd) == 0; }
int plat_list_dir(const char *d, PlatDirEnt **out) {
    char q[1200]; fix(d, q);
    DIR *dir = opendir(q); *out = NULL;
    if (!dir) return 0;
    int n = 0, cap = 0; struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        *out = sf_grow(*out, &cap, n + 1, sizeof(PlatDirEnt));
        sf_strcpy((*out)[n].name, 260, e->d_name);
        char f[1500]; snprintf(f, sizeof f, "%s/%s", q, e->d_name);
        (*out)[n].is_dir = plat_is_dir(f); (*out)[n].mtime = plat_mtime(f); n++;
    }
    closedir(dir); return n;
}
bool plat_recycle(const char *p) { char q[1200]; fix(p, q); char cmd[1300]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", q); return system(cmd) == 0; }
bool plat_copy_dir(const char *a, const char *b) { char q[1200], r[1200]; fix(a, q); fix(b, r); char cmd[2500]; snprintf(cmd, sizeof cmd, "cp -r '%s' '%s'", q, r); return system(cmd) == 0; }
bool plat_copy_file(const char *a, const char *b) { char q[1200], r[1200]; fix(a, q); fix(b, r); char cmd[2500]; snprintf(cmd, sizeof cmd, "cp '%s' '%s'", q, r); return system(cmd) == 0; }
bool plat_delete_file(const char *p) { char q[1200]; fix(p, q); remove(q); return true; }
void plat_documents_dir(char *o, size_t c) { snprintf(o, c, "%s/docs", ROOT); }
void plat_appdata_dir(char *o, size_t c) { snprintf(o, c, "%s/appdata", ROOT); }
void plat_open_folder(const char *p) { (void)p; }
bool plat_open_file_dialog(char *o, size_t c, const char *t, const char *f, const char *p) { (void)o; (void)c; (void)t; (void)f; (void)p; return false; }
bool plat_pick_folder(char *o, size_t c, const char *t) { (void)o; (void)c; (void)t; return false; }
static bool g_closed;
void plat_minimize(void) {}
void plat_toggle_maximize(void) {}
void plat_request_close(void) { if (app_on_close()) g_closed = true; }
void plat_quit(void) { g_closed = true; }
void plat_set_window_mode(int m) { (void)m; }
void plat_set_title(const char *t) { (void)t; }
void plat_set_cursor(int c) { (void)c; }
void plat_set_relative_mouse(bool on) { (void)on; }
void plat_set_vsync(bool on) { (void)on; }
double plat_time(void) { return g_time; }
uint8_t *plat_load_image(const char *p, int *w, int *h) { (void)p; *w = *h = 0; return NULL; }
void plat_set_app_icon(const uint8_t *r, int w, int h) { (void)r; (void)w; (void)h; }
bool plat_font_raster(const char *face, float px, int weight, const uint32_t *cps, int n, FontRaster *out) {
    char list[4096] = "", tmp[16];
    for (int i = 0; i < n; i++) { snprintf(tmp, sizeof tmp, "%s%u", i ? "," : "", cps[i]); strcat(list, tmp); }
    char cmd[5000], path[256];
    snprintf(path, sizeof path, "%s/font_%s_%g_%d.bin", ROOT, face[0] == 'C' ? "mono" : "sans", px, weight);
    snprintf(cmd, sizeof cmd, "python3 tests/fontgen.py '%s' %g %d %s %s", face, px, weight, path, list);
    if (system(cmd)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    int cnt;
    if (fscanf(f, "%d %d %f %f %d\n", &out->w, &out->h, &out->ascent, &out->height, &cnt) != 5) { fclose(f); return false; }
    out->glyphs = calloc((size_t)cnt, sizeof(GlyphInfo));
    out->nglyphs = cnt;
    for (int i = 0; i < cnt; i++) {
        GlyphInfo *g = &out->glyphs[i];
        unsigned cp; int x, y, w, h; float xo, adv;
        if (fscanf(f, "%u %d %d %d %d %f %f\n", &cp, &x, &y, &w, &h, &xo, &adv) != 7) break;
        g->cp = cp; g->x = x; g->y = y; g->w = w; g->h = h; g->xoff = xo; g->yoff = 0; g->adv = adv;
    }
    out->alpha = malloc((size_t)out->w * out->h);
    size_t got = fread(out->alpha, 1, (size_t)out->w * out->h, f);
    (void)got;
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ script */
static Input in;
static void frame(void) {
    in.dt = 1.0 / 60; g_time += in.dt; in.time = g_time;
    app_frame(&in);
    memset(in.kpressed, 0, sizeof in.kpressed); memset(in.kreleased, 0, sizeof in.kreleased);
    memset(in.mpressed, 0, sizeof in.mpressed); memset(in.mreleased, 0, sizeof in.mreleased);
    memset(in.mdouble, 0, sizeof in.mdouble);
    in.nchars = 0; in.wheel = 0; in.mdx = in.mdy = 0;
}
static void frames(int n) { for (int i = 0; i < n; i++) frame(); }
static int g_shot_on;
static void shot(const char *name) {
    if (!g_shot_on) return;
    g_sw_enabled = 1; frame(); frame();
    char p[256]; snprintf(p, sizeof p, "/tmp/shots/%s.ppm", name); sw_dump(p);
    g_sw_enabled = 0;
}
static void move(float x, float y) { in.mx = x; in.my = y; frame(); }
static void click(float x, float y, int b, bool dbl) {
    in.mx = x; in.my = y; frame();
    in.mdown[b] = true; in.mpressed[b] = true; in.mdouble[b] = dbl; frame();
    in.mdown[b] = false; in.mreleased[b] = true; frame();
}
static void drag(float x0, float y0, float x1, float y1) {
    in.mx = x0; in.my = y0; frame();
    in.mdown[0] = true; in.mpressed[0] = true; frame();
    for (int i = 1; i <= 8; i++) { in.mx = x0 + (x1 - x0) * i / 8; in.my = y0 + (y1 - y0) * i / 8; frame(); }
    in.mdown[0] = false; in.mreleased[0] = true; frame();
}
static void key(int k) { in.kdown[k] = true; in.kpressed[k] = true; frame(); in.kdown[k] = false; in.kreleased[k] = true; frame(); }
static void ckey(int k) { in.ctrl = true; key(k); in.ctrl = false; }
static void wheel(float x, float y, float w) { in.mx = x; in.my = y; in.wheel = w; frame(); }

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } else printf("ok   %s\n", #c); } while (0)

int main(void) {
    int fails = 0;
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", ROOT, ROOT);
    if (system(cmd)) return 1;
    in.win_w = 1440; in.win_h = 900; in.scale = 1; in.focused = true;
    g_shot_on = getenv("SF_SHOTS") != NULL;
    if (g_shot_on && system("mkdir -p /tmp/shots")) return 1;
    app_init();
    frames(25); shot("01_splash");
    frames(200); shot("02_pm_empty");                                    /* splash -> project manager */
    /* New Project button: main area starts at x=232, content x=264, w=1144 */
    click(264 + 1144 - 70, 36 + 28 + 26, 0, false);
    frames(3); shot("03_new_project");
    key(KEY_ENTER);                                 /* create with defaults (Starter Room) */
    frames(20); shot("04_loading");
    frames(80);                                     /* loading screen -> editor */
    CHECK(ed.open);
    CHECK(ed.map.sectors.n == 2);
    float vx = 52, vy = 118, vw = 1440 - 52 - 345.6f, vh = 900 - 82 - 28 - 36;
    float cx = vx + vw * 0.5f, cy = vy + vh * 0.5f;
    ed.zoom = 0.5; ed.vx = 0; ed.vy = 0; ed.grid = 32;
    /* draw a sector next to the starter room (room spans -256..256) */
    key('D');
    CHECK(ed.tool == T_DRAW);
    /* world x 256..512, y -128..128 -> screen */
    D2 a = ed2d_world_to_screen(256, -128), b = ed2d_world_to_screen(512, -128), c = ed2d_world_to_screen(512, 128), d = ed2d_world_to_screen(256, 128);
    click((float)a.x, (float)a.y, 0, false);
    click((float)b.x, (float)b.y, 0, false);
    click((float)c.x, (float)c.y, 0, false);
    move((float)d.x + 20, (float)d.y + 30); shot("05_drawing");
    click((float)d.x, (float)d.y, 0, false);
    click((float)a.x, (float)a.y, 0, false);        /* close on first point */
    CHECK(ed.map.sectors.n == 3);
    CHECK(map_sector_at(&ed.map, 400, 0) >= 0);
    int two = 0;
    for (int i = 0; i < ed.map.lines.n; i++) if (ed.map.lines.a[i].side[1].sector >= 0) two++;
    CHECK(two >= 5);                                /* pillar(4) + shared wall */
    /* rectangle tool */
    key('R');
    D2 r0 = ed2d_world_to_screen(-512, -512), r1 = ed2d_world_to_screen(-320, -320);
    drag((float)r0.x, (float)r0.y, (float)r1.x, (float)r1.y);
    CHECK(ed.map.sectors.n == 4);
    /* polygon tool */
    key('O');
    D2 p0 = ed2d_world_to_screen(800, 0), p1 = ed2d_world_to_screen(900, 0);
    drag((float)p0.x, (float)p0.y, (float)p1.x, (float)p1.y);
    CHECK(ed.map.sectors.n == 5);
    move(cx, cy); shot("06_2d_shapes");
    /* select + move the polygon */
    key('S');
    D2 pc = ed2d_world_to_screen(800, 0), pm = ed2d_world_to_screen(864, 64);
    drag((float)pc.x, (float)pc.y, (float)pm.x, (float)pm.y);
    CHECK(map_sector_at(&ed.map, 864, 64) >= 0);
    /* undo / redo */
    ckey('Z');
    CHECK(map_sector_at(&ed.map, 800, 0) >= 0);
    ckey('Y');
    /* properties panel interactions: wheel over floor field */
    frames(5);
    /* copy / paste / duplicate / delete */
    ckey('C'); move(cx + 200, cy + 200); ckey('V'); ckey('D'); key(KEY_DELETE);
    /* modes */
    key('1'); ckey('A'); key('2'); ckey('A'); key('4'); ckey('A'); key('3');
    /* box select everything and mirror / rotate */
    key(KEY_ESC);
    drag(vx + 20, vy + 60, vx + vw - 20, vy + vh - 60);
    key('H'); key('V'); ckey(KEY_RBRACKET);
    ckey('Z'); ckey('Z'); ckey('Z');
    /* lines tool splitting the starter room */
    key('L');
    D2 l0 = ed2d_world_to_screen(-100, -256), l1 = ed2d_world_to_screen(-100, 256);
    int before = ed.map.sectors.n;
    click((float)l0.x, (float)l0.y, 0, false);
    click((float)l1.x, (float)l1.y, 0, false);
    key(KEY_ENTER);
    CHECK(ed.map.sectors.n == before + 1);
    /* insert vertex & split */
    key('I');
    D2 iv = ed2d_world_to_screen(-256, 0);
    click((float)iv.x, (float)iv.y, 0, false);
    key('X');
    click((float)iv.x, (float)iv.y - 30, 0, false);
    /* things */
    key('T');
    D2 tp = ed2d_world_to_screen(0, 64);
    drag((float)tp.x, (float)tp.y, (float)tp.x + 40, (float)tp.y);
    CHECK(ed.map.things.n >= 7);
    /* paint & measure */
    key('B'); click(cx, cy, 0, false);
    key('U'); drag(cx, cy, cx + 100, cy);
    /* grid */
    key(KEY_LBRACKET); key(KEY_RBRACKET); key('G'); frames(3); shot("07_grid_tool"); key('G');
    /* zoom & pan */
    key('S'); wheel(cx, cy, 2); wheel(cx, cy, -3);
    in.mx = cx; in.my = cy; in.mdown[2] = true; in.mpressed[2] = true; frame(); in.mx += 50; frame(); in.mdown[2] = false; in.mreleased[2] = true; frame();
    key(KEY_HOME);
    /* context menu */
    click(cx, cy, 1, false); frames(3); key(KEY_ESC);
    /* panels: click each tab */
    float px = 1440 - 345.6f;
    { const char *nm[] = { "08_props", "09_textures", "10_things", "11_map" }; for (int t = 0; t < 4; t++) { click(px + 40 + t * 82, 100, 0, false); frames(2); shot(nm[t]); } }
    click(px + 40, 100, 0, false);
    /* 3D view */
    key(KEY_TAB);
    CHECK(ed.mode == MODE_3D);
    move(cx, cy);
    ed.hover3d = SURF_ID(SURF_FLOOR, 0);
    arr_clear(ed.sel3d); arr_push(ed.sel3d, SURF_ID(SURF_FLOOR, 0));
    float before_h = ed.map.sectors.a[0].floor_h;
    /* the pick stub returns nothing, so drive the wheel with a selection */
    wheel(cx, cy, 1); wheel(cx, cy, 1);
    CHECK(ed.map.sectors.a[0].floor_h == before_h + 16);
    shot("12_3d_wheel");
    in.mx = cx; in.my = cy; in.mdown[1] = true; in.mpressed[1] = true; frame();
    in.mdx = 30; in.mdy = 5; in.kdown['W'] = true; frames(10); in.kdown['W'] = false; in.mdown[1] = false; in.mreleased[1] = true; frame();
    key(KEY_LEFT); key(KEY_UP); key(KEY_PLUS); key('F'); key('G');
    for (int t = 0; t < 4; t++) { click(px + 40 + t * 82, 100, 0, false); frames(2); }
    /* play mode */
    key(KEY_F5);
    CHECK(ed.mode == MODE_PLAY);
    shot("13_play");
    in.kdown['W'] = true; frames(60); in.kdown['W'] = false;
    in.mdx = 100; frame();
    key(KEY_SPACE); frames(30);
    key('E'); frames(10);
    in.mdown[0] = true; in.mpressed[0] = true; frames(30); in.mdown[0] = false; in.mreleased[0] = true; frame();
    key(KEY_ESC);
    CHECK(ed.mode != MODE_PLAY);
    key(KEY_TAB);
    /* save */
    ckey('S');
    CHECK(!ed.dirty);
    /* new map via dialog */
    ckey('N'); frames(3); key(KEY_ENTER); frames(3);
    CHECK(ed.proj.nmaps == 2);
    /* templates through new-map: dungeon + arena play */
    for (int tpl = TPL_DUNGEON; tpl <= TPL_ARENA; tpl++) {
        ed_new_map(tpl == TPL_DUNGEON ? "Dungeon" : "Arena", tpl);
        frames(5);
        shot(tpl == TPL_DUNGEON ? "14_dungeon_2d" : "17_arena_2d");
        key(KEY_TAB); frames(3); shot(tpl == TPL_DUNGEON ? "15_dungeon_3d" : "18_arena_3d"); key(KEY_TAB);
        key(KEY_F5); frames(2); shot(tpl == TPL_DUNGEON ? "16_dungeon_play" : "19_arena_play"); key(KEY_ESC);
        key(KEY_F5); in.kdown['W'] = true; in.kdown[KEY_SHIFT] = true; frames(120); in.kdown['W'] = false; in.kdown[KEY_SHIFT] = false;
        key('E'); frames(240); key(KEY_ESC);
        key(KEY_TAB); frames(10); key(KEY_TAB);
    }
    CHECK(ed.proj.nmaps == 4);
    /* menus: open each menu bar entry */
    float mx = 12 + 26 + 90;
    for (int i = 0; i < 7; i++) { click(mx + i * 44, 18, 0, false); frames(2); if (i == 3) shot("20_menu_map"); }
    key(KEY_ESC);
    /* shortcuts dialog and about */
    key(KEY_F1); frames(3); shot("21_shortcuts"); key(KEY_ESC);
    /* close project (dirty -> dialog) */
    ed_mark_dirty();
    app_request_close_project(); frames(3); key(KEY_ENTER); frames(10);
    CHECK(!ed.open);
    frames(30); shot("22_pm_projects");
    /* reopen through the project manager: select first card and press enter */
    key(KEY_ENTER); frames(80);
    CHECK(ed.open);
    CHECK(ed.proj.nmaps == 4);
    printf("%s (%d failures)\n", fails ? "HARNESS FAILED" : "HARNESS OK", fails);
    app_shutdown();
    return fails != 0;
}
