/* Sector Forge - platform interface (implemented by win32.c) */
#ifndef SF_PLAT_H
#define SF_PLAT_H
#include "base.h"

/* Virtual key codes (match Windows VK_*) */
enum {
    KEY_BACK = 0x08, KEY_TAB = 0x09, KEY_ENTER = 0x0D, KEY_SHIFT = 0x10, KEY_CTRL = 0x11, KEY_ALT = 0x12,
    KEY_ESC = 0x1B, KEY_SPACE = 0x20, KEY_PGUP = 0x21, KEY_PGDN = 0x22, KEY_END = 0x23, KEY_HOME = 0x24,
    KEY_LEFT = 0x25, KEY_UP = 0x26, KEY_RIGHT = 0x27, KEY_DOWN = 0x28, KEY_INSERT = 0x2D, KEY_DELETE = 0x2E,
    KEY_F1 = 0x70, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_PLUS = 0xBB, KEY_COMMA = 0xBC, KEY_MINUS = 0xBD, KEY_PERIOD = 0xBE,
    KEY_LBRACKET = 0xDB, KEY_RBRACKET = 0xDD, KEY_NUMPLUS = 0x6B, KEY_NUMMINUS = 0x6D,
};

enum { CUR_ARROW, CUR_HAND, CUR_CROSS, CUR_IBEAM, CUR_MOVE, CUR_SIZEWE, CUR_SIZENS, CUR_NONE };
enum { WINMODE_SPLASH, WINMODE_MAIN };

typedef struct {
    float mx, my;       /* mouse in logical pixels */
    float mdx, mdy;     /* raw mouse delta (device units) */
    bool mdown[3], mpressed[3], mreleased[3], mdouble[3];
    float wheel;        /* notches, + = away from user */
    bool kdown[256], kpressed[256], kreleased[256];
    uint32_t chars[32];
    int nchars;
    bool ctrl, shift, alt;
    float win_w, win_h; /* logical size */
    float scale;        /* dpi scale */
    double time, dt;
    bool focused, maximized;
} Input;

typedef struct { uint32_t cp; int x, y, w, h; float xoff, yoff, adv; } GlyphInfo;
typedef struct {
    int w, h;
    uint8_t *alpha;
    GlyphInfo *glyphs;
    int nglyphs;
    float ascent, height;
} FontRaster;

typedef struct { char name[260]; bool is_dir; int64_t mtime; } PlatDirEnt;

/* files */
FILE *plat_fopen(const char *path, const char *mode);
bool plat_exists(const char *path);
bool plat_is_dir(const char *path);
int64_t plat_mtime(const char *path);
bool plat_mkdirs(const char *path);
int plat_list_dir(const char *dir, PlatDirEnt **out);
bool plat_recycle(const char *path);
bool plat_copy_dir(const char *src, const char *dst);
bool plat_copy_file(const char *src, const char *dst);
bool plat_delete_file(const char *path);
void plat_documents_dir(char *out, size_t cap);
void plat_appdata_dir(char *out, size_t cap);
void plat_open_folder(const char *path);
bool plat_open_file_dialog(char *out, size_t cap, const char *title, const char *filter_name, const char *patterns);
bool plat_pick_folder(char *out, size_t cap, const char *title);

/* window */
void plat_minimize(void);
void plat_toggle_maximize(void);
void plat_request_close(void);
void plat_quit(void);
void plat_set_window_mode(int mode);
void plat_set_title(const char *title);
void plat_set_cursor(int cursor);
void plat_set_relative_mouse(bool on);
void plat_set_vsync(bool on);
double plat_time(void);

/* resources */
uint8_t *plat_load_image(const char *path, int *w, int *h);
bool plat_font_raster(const char *face, float px, int weight, const uint32_t *cps, int ncp, FontRaster *out);
void plat_set_app_icon(const uint8_t *rgba, int w, int h);

/* implemented by the app */
bool app_init(void);
void app_frame(const Input *in);
bool app_on_close(void);
int app_hit_test(float x, float y); /* 0 = client, 1 = caption */
void app_shutdown(void);

#endif
