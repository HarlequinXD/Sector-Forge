/* Headless test of the Android touch shell on Linux: real OpenGL ES 3 through Mesa (EGL surfaceless),
 * the shared POSIX platform helpers, scripted multi-touch input and PNG screenshots.
 * Build & run: tests/run_mobile.sh   (SF_SHOTS=1 writes PNGs to /tmp/mshots) */
#define _GNU_SOURCE
#include "../src/app_mobile.c"   /* unity build: the test reaches into the shell's state */
#include "../src/plat_posix.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/stat.h>

static const char *ROOT = "/tmp/sfmobile";
static double g_time;
static Input in;
static GLuint g_fbo, g_rb_c, g_rb_ds;
static int g_pw, g_ph;
static bool g_shots;
static int g_fail;
static int g_orient = ORIENT_PORTRAIT;
static const float DENS = 2.625f;          /* Galaxy S23 */
static const float PW = 1080, PH = 2340;

#define CHECK(c) do { if (c) printf("ok   %s\n", #c); else { printf("FAIL %s  (line %d)\n", #c, __LINE__); g_fail++; } } while (0)

/* ---------------------------------------------------------------- platform */
void plat_open_folder(const char *p) { (void)p; }
bool plat_open_file_dialog(char *o, size_t c, const char *t, const char *f, const char *p) { (void)o; (void)c; (void)t; (void)f; (void)p; return false; }
bool plat_pick_folder(char *o, size_t c, const char *t) { (void)o; (void)c; (void)t; return false; }
void plat_minimize(void) {}
void plat_toggle_maximize(void) {}
void plat_request_close(void) {}
static bool g_quit;
void plat_quit(void) { g_quit = true; }
void plat_set_window_mode(int m) { (void)m; }
void plat_set_title(const char *t) { (void)t; }
void plat_set_cursor(int c) { (void)c; }
void plat_set_relative_mouse(bool on) { (void)on; }
void plat_set_vsync(bool on) { (void)on; }
double plat_time(void) { return g_time; }
void plat_set_app_icon(const uint8_t *r, int w, int h) { (void)r; (void)w; (void)h; }
void plat_set_orientation(int o) { g_orient = o; }
void plat_haptic(int k) { (void)k; }
void plat_open_url(const char *u) { (void)u; }

static char g_text_answer[8][128];
static int g_text_n, g_text_req = -1;
static char g_text_title[128];
bool plat_text_input_begin(int req, const char *title, const char *initial, int kind) {
    (void)initial; (void)kind;
    g_text_req = req;
    sf_strcpy(g_text_title, sizeof g_text_title, title);
    return true;
}
bool plat_text_input_poll(int *req, char *out, size_t cap) {
    if (g_text_req < 0 || !g_text_n) return false;
    *req = g_text_req;
    sf_strcpy(out, cap, g_text_answer[0]);
    memmove(g_text_answer[0], g_text_answer[1], sizeof g_text_answer[0] * 7);
    g_text_n--;
    g_text_req = -1;
    return true;
}
static void answer(const char *s) { sf_strcpy(g_text_answer[g_text_n++], 128, s); }
static bool g_pick;
bool plat_pick_image_begin(void) { g_pick = true; return true; }
bool plat_pick_image_poll(char *path, size_t cap) {
    if (!g_pick) return false;
    g_pick = false;
    snprintf(path, cap, "%s/photo.png", ROOT);
    return plat_exists(path);
}

static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz);
    *n = fread(b, 1, (size_t)sz, f);
    fclose(f);
    return b;
}
uint8_t *plat_load_image(const char *p, int *w, int *h) {
    size_t n;
    char q[1024];
    sf_strcpy(q, sizeof q, p);
    for (char *c = q; *c; c++) if (*c == '\\') *c = '/';
    uint8_t *d = read_all(q, &n);
    if (!d) return NULL;
    uint8_t *img = image_decode(d, n, w, h);
    free(d);
    return img;
}
bool plat_font_raster(const char *face, float px, int weight, const uint32_t *cps, int ncp, FontRaster *out) {
    const char *file = font_file_for(face, weight);
    char path[512];
    if (!strncmp(file, "Inter", 5)) snprintf(path, sizeof path, "/usr/share/fonts/opentype/inter/%s", file);
    else snprintf(path, sizeof path, "/usr/share/fonts/truetype/dejavu/%s", file);
    size_t n;
    uint8_t *d = read_all(path, &n);
    if (!d) { printf("missing font %s\n", path); return false; }
    bool ok = font_raster_ttf(d, n, px, cps, ncp, out);
    free(d);
    return ok;
}

/* ---------------------------------------------------------------- GL */
static void make_target(void) {
    bool land = g_orient == ORIENT_LANDSCAPE;
    int w = (int)(land ? PH : PW), h = (int)(land ? PW : PH);
    if (w == g_pw && h == g_ph) return;
    g_pw = w; g_ph = h;
    if (!g_fbo) { glGenFramebuffers(1, &g_fbo); glGenRenderbuffers(1, &g_rb_c); glGenRenderbuffers(1, &g_rb_ds); }
    glBindRenderbuffer(GL_RENDERBUFFER, g_rb_c);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rb_ds);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rb_c);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_rb_ds);
    sfgl_set_default_fbo(g_fbo);
    sfgl_set_surface_size(w, h);
    in.win_w = w / DENS; in.win_h = h / DENS; in.scale = DENS;
    if (land) { in.inset_l = 18; in.inset_r = 0; in.inset_t = 0; in.inset_b = 0; }
    else { in.inset_l = in.inset_r = 0; in.inset_t = 30; in.inset_b = 18; }
}

static bool init_gl(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd ? gpd(0x31DD, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint ma, mi;
    if (!eglInitialize(d, &ma, &mi)) return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cx);
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) return false;
    return sfgl_init();
}

/* ---------------------------------------------------------------- input script */
static bool g_back;

static void frame(void) {
    make_target();
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    in.dt = 1.0 / 60;
    g_time += in.dt;
    in.time = g_time;
    in.touch = true;
    in.back = g_back;
    g_back = false;
    app_frame(&in);
    sfgl_flush();
    /* clear per-frame flags */
    int k = 0;
    for (int i = 0; i < in.ntouch; i++) {
        if (in.touches[i].released) continue;
        in.touches[i].pressed = false;
        in.touches[k++] = in.touches[i];
    }
    in.ntouch = k;
    memset(in.kpressed, 0, sizeof in.kpressed);
}
static void frames(int n) { for (int i = 0; i < n; i++) frame(); }
static TouchPt *tfind(int id) { for (int i = 0; i < in.ntouch; i++) if (in.touches[i].id == id) return &in.touches[i]; return NULL; }
static void down(int id, float x, float y) {
    TouchPt *t = &in.touches[in.ntouch++];
    memset(t, 0, sizeof *t);
    t->id = id; t->x = t->sx = x; t->y = t->sy = y; t->t0 = g_time; t->pressed = true;
}
static void move(int id, float x, float y) { TouchPt *t = tfind(id); if (t) { t->x = x; t->y = y; } }
static void up(int id) { TouchPt *t = tfind(id); if (t) t->released = true; }
static void ttap(float x, float y) { down(1, x, y); frame(); up(1); frames(3); }
static void tdrag(float x0, float y0, float x1, float y1, int steps) {
    down(1, x0, y0); frame();
    for (int i = 1; i <= steps; i++) { move(1, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); frame(); }
    up(1); frames(3);
}
static void thold(float x, float y, int n) { down(1, x, y); frames(n); up(1); frames(3); }
static void back(void) { g_back = true; frames(3); }

static void shot(const char *name) {
    if (!g_shots) return;
    size_t n = (size_t)g_pw * g_ph * 4;
    uint8_t *px = malloc(n);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(0, 0, g_pw, g_ph, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char raw[256], cmd[600];
    snprintf(raw, sizeof raw, "/tmp/mshots/%s.raw", name);
    FILE *f = fopen(raw, "wb");
    fwrite(px, 1, n, f);
    fclose(f);
    free(px);
    snprintf(cmd, sizeof cmd, "python3 -c \"from PIL import Image; im=Image.frombytes('RGBA',(%d,%d),open('%s','rb').read()).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB'); im.save('/tmp/mshots/%s.png')\" && rm %s",
             g_pw, g_ph, raw, name, raw);
    if (system(cmd)) printf("screenshot failed\n");
}

/* screen <-> logical helpers */
#define W (in.win_w)
#define H (in.win_h)

int main(void) {
    g_shots = getenv("SF_SHOTS") != NULL;
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/docs %s/appdata /tmp/mshots", ROOT, ROOT, ROOT);
    if (system(cmd)) return 1;
    snprintf(cmd, sizeof cmd, "%s/docs", ROOT);
    char ad[256];
    snprintf(ad, sizeof ad, "%s/appdata", ROOT);
    posix_set_dirs(cmd, ad);
    if (!init_gl()) { printf("no GLES context\n"); return 1; }
    make_target();
    CHECK(app_init());
    for (int i = 0; i < 600 && !g_assets.ready; i++) frame();
    CHECK(g_assets.ready);
    frames(10);
    shot("00_splash");
    frames(90);
    /* first run: create from template via the carousel */
    shot("01_home_empty");
    ttap(20 + 75 + 165, 140 + 30 + 62);       /* Starter Room card in the carousel */
    answer("Hellgate Keep");
    frames(2);
    CHECK(g_orient == ORIENT_LANDSCAPE);
    frames(80);
    CHECK(ed.open);
    CHECK(!strcmp(ed.proj.name, "Hellgate Keep"));
    frames(5);
    shot("02_guide");
    ttap(in.inset_l + 12 + 24, 8 + 24);        /* close guide */
    frames(5);
    CHECK(ed.mode == MODE_2D);
    shot("03_editor_2d");
    /* --- draw a sector with taps, then close it with the bar button --- */
    float rx = in.inset_l + 4 + 30, ry = 52 + 4 + 56 * 1 + 20;   /* rail: Draw */
    ttap(rx, ry);
    CHECK(ed.tool == T_DRAW);
    int nsec0 = ed.map.sectors.n;
    D2 c = ed2d_world_to_screen(ed.vx, ed.vy);
    float cx = (float)c.x, cy = (float)c.y;
    ed.grid = 32; ed.snap = true;
    ttap(cx + 150, cy - 90);
    ttap(cx + 260, cy - 90);
    /* hold the third point to show the loupe */
    down(1, cx + 260, cy + 40); frames(6); move(1, cx + 262, cy + 44); frames(3);
    shot("04_draw_loupe");
    up(1); frames(3);
    CHECK(ed.draw.n == 3);
    ttap(cx + 150, cy + 40);
    CHECK(ed.draw.n == 4);
    shot("05_draw_bar");
    /* "Close shape" sits at the right end of the draw bar */
    {
        float bw = 156 + 120 + gfx_text_w(FONT_UI_BOLD, "Close shape") + 70 + 16;
        Rect vp = rect(in.inset_l + 68, 52, W - in.inset_l - 68, H - 52);
        float bx = vp.x + vp.w * 0.5f - bw * 0.5f + bw - 40;
        ttap(bx, vp.y + vp.h - 70 + 31);
    }
    CHECK(ed.map.sectors.n == nsec0 + 1);
    CHECK(ed.draw.n == 0);
    shot("06_sector_selected");
    /* --- properties side sheet via the floating action bar --- */
    M.sheet_open = true; M.sheet_tab = SHEET_PROPS;
    frames(3);
    shot("07_props");
    int s_new = -1;
    for (int i = 0; i < ed.map.sectors.n; i++) if (ed.map.sectors.a[i].sel) s_new = i;
    CHECK(s_new >= 0);
    float fl0 = s_new >= 0 ? ed.map.sectors.a[s_new].floor_h : 0;
    /* floor "+" button: sheet x + 18 + hw - 26 */
    {
        float sheet_w = SF_MIN(360, SF_MAX(300, W * 0.36f));
        float sx = W - sheet_w + 18, aw = sheet_w - 36, hw = (aw - 12) * 0.5f;
        float y = 52 + 53 + 16 + 68 + 24 + 28;
        ttap(sx + hw - 20, y);
        if (s_new >= 0) CHECK(ed.map.sectors.a[s_new].floor_h == fl0 + ed.hstep);
    }
    M.sheet_tab = SHEET_TEX; frames(3);
    shot("08_textures");
    M.sheet_open = false; frames(2);
    /* --- pinch zoom --- */
    double z0 = ed.zoom;
    down(1, cx - 60, cy); down(2, cx + 60, cy); frame();
    for (int i = 1; i <= 10; i++) { move(1, cx - 60 - i * 8, cy); move(2, cx + 60 + i * 8, cy); frame(); }
    up(1); up(2); frames(3);
    CHECK(ed.zoom > z0 * 1.5);
    ed_fit_view(); frames(2);
    /* --- long press -> actions sheet --- */
    ttap(in.inset_l + 34, 52 + 4 + 20);        /* Select tool */
    CHECK(ed.tool == T_SELECT);
    D2 sc = ed2d_world_to_screen(ed.map.verts.a[0].x, ed.map.verts.a[0].y);
    (void)sc;
    {
        double lx, ly;
        map_sector_label_point(&ed.map, 0, &lx, &ly);
        D2 p = ed2d_world_to_screen(lx, ly);
        thold((float)p.x, (float)p.y, 40);
        CHECK(M.bsheet == BS_ACTIONS);
        CHECK(ed.map.sectors.a[0].sel);
        shot("09_actions");
        back();
        CHECK(M.bsheet == BS_NONE);
        /* drag the sector: press on it and move */
        double vx0 = ed.map.verts.a[ed.map.lines.a[0].v1].x;
        int nlines = ed.map.lines.n;
        tdrag((float)p.x, (float)p.y, (float)p.x + 60, (float)p.y, 8);
        bool moved = false;
        for (int i = 0; i < ed.map.verts.n; i++) if (ed.map.verts.a[i].x != vx0) moved = true;
        CHECK(moved);
        CHECK(ed.map.lines.n == nlines);
        /* three-finger tap undoes */
        down(1, 300, 200); down(2, 340, 200); down(3, 380, 200); frame();
        up(1); up(2); up(3); frames(3);
    }
    /* --- grid sheet --- */
    ttap(in.inset_l + 68 + 12 + 40, 52 + 10 + 22);
    CHECK(M.bsheet == BS_GRID);
    frames(12);
    shot("10_grid_sheet");
    back();
    /* --- 3D --- */
    ed_set_mode(MODE_3D);
    frames(3);
    {
        Rect vp = g_vp;
        ed_select_none(); arr_clear(ed.sel3d); frames(2);
        ttap(vp.x + vp.w * 0.45f, vp.y + vp.h * 0.72f);     /* floor in front of the camera */
        CHECK(ed.sel3d.n == 1);
        if (ed.sel3d.n) CHECK(SURF_KIND(ed.sel3d.a[0]) == SURF_FLOOR);
        ed.tool3d = T3_HEIGHT;
        frames(2);
        int s3 = ed.sel3d.n ? SURF_IDX(ed.sel3d.a[0]) : 0;
        float h0 = ed.map.sectors.a[s3].floor_h;
        float dx = vp.x + vp.w - 10 - 35, dy = vp.y + vp.h * 0.5f;
        tdrag(dx, dy, dx, dy - 40, 8);
        CHECK(ed.map.sectors.a[s3].floor_h > h0);
        shot("11_3d_dial");
        /* look drag + stick */
        float yaw0 = ed.cam.yaw;
        tdrag(vp.x + vp.w * 0.5f, vp.y + 100, vp.x + vp.w * 0.5f - 120, vp.y + 100, 10);
        CHECK(ed.cam.yaw != yaw0);
        float camx = ed.cam.x, camy = ed.cam.y;
        Rect bu, bd; float sbx, sby, srad;
        ed3d_buttons(vp, &in, &bu, &bd, &sbx, &sby, &srad);
        down(5, sbx, sby); frame(); move(5, sbx, sby - 50); frames(20); up(5); frames(2);
        CHECK(fabsf(ed.cam.x - camx) + fabsf(ed.cam.y - camy) > 20);
        M.sheet_open = true; M.sheet_tab = SHEET_TEX; frames(3);
        shot("12_3d_textures");
        M.sheet_open = false;
    }
    /* --- play --- */
    ed_set_mode(MODE_PLAY);
    frames(20);
    CHECK(ed.mode == MODE_PLAY);
    {
        Rect fire, use, jump, crouch, pause;
        play_buttons(W, H, &in, &fire, &use, &jump, &crouch, &pause);
        float px0 = ed.play.x, py0 = ed.play.y;
        down(4, 140, H - 120); frame(); move(4, 140, H - 190);
        down(6, fire.x + fire.w * 0.5f, fire.y + fire.h * 0.5f);
        frames(30);
        shot("13_play");
        up(4); up(6); frames(3);
        CHECK(fabsf(ed.play.x - px0) + fabsf(ed.play.y - py0) > 10);
        ttap(pause.x + 26, pause.y + 26);
        CHECK(M.paused);
        shot("14_pause");
        back();
        CHECK(!M.paused);
        back();
        CHECK(M.paused);
        /* "Back to the editor" */
        ttap(W * 0.5f, (H - 300) * 0.5f + 76 + 124 + 26);
        CHECK(ed.mode != MODE_PLAY);
    }
    /* --- leave to home, check the project is listed --- */
    ed_set_mode(MODE_2D);
    frames(2);
    ttap(in.inset_l + 8 + 22, 26);
    frames(5);
    CHECK(!ed.open);
    CHECK(g_orient == ORIENT_PORTRAIT);
    frames(20);
    CHECK(M.projects.n == 1);
    shot("15_home");
    M.home_tab = HOME_LEARN; frames(3); shot("16_learn");
    M.home_tab = HOME_SETTINGS; frames(3); shot("17_settings");
    M.home_tab = HOME_TEMPLATES; frames(3); shot("18_templates");
    M.home_tab = HOME_PROJECTS; frames(3);
    /* long-press the project card -> sheet */
    thold(20 + 80, 300 + 120, 40);
    CHECK(M.bsheet == BS_PROJECT);
    shot("19_project_sheet");
    back();
    app_shutdown();
    printf(g_fail ? "MOBILE HARNESS: %d failure(s)\n" : "MOBILE HARNESS OK (%d failures)\n", g_fail);
    return g_fail ? 1 : 0;
}
