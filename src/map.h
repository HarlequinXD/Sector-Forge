/* Sector Forge - Doom-style sector map model and editing operations */
#ifndef SF_MAP_H
#define SF_MAP_H
#include "base.h"

#define TEXNAME 16
#define MAP_EPS 1e-3

typedef struct {
    int sector;                      /* -1 = void */
    char upper[TEXNAME], middle[TEXNAME], lower[TEXNAME];
    float offx, offy;
} MSide;

enum { LF_BLOCK = 1, LF_UPPER_UNPEG = 2, LF_LOWER_UNPEG = 4 };

typedef struct {
    int v1, v2;
    int flags;
    MSide side[2];                   /* 0 = front (right of v1->v2), 1 = back */
    uint8_t sel, mark;
} MLine;

typedef struct { double x, y; uint8_t sel, mark; } MVert;

enum { ST_NORMAL, ST_DOOR, ST_LIFT, ST_DAMAGE, ST_SECRET, ST_FLICKER, ST_EXIT, ST_COUNT };

typedef struct {
    float floor_h, ceil_h;
    char floor_tex[TEXNAME], ceil_tex[TEXNAME];
    int light, type, tag;
    float fox, foy, fscale, frot;    /* floor texture offset / scale / rotation (deg) */
    float cox, coy, cscale, crot;    /* ceiling */
    char name[32];
    uint8_t sel, mark;
} MSector;

enum { TH_PLAYER, TH_LIGHT, TH_IMP, TH_SOLDIER, TH_HEALTH, TH_ARMOR, TH_AMMO, TH_BARREL, TH_LAMP, TH_TREE, TH_COUNT };
enum { TC_START, TC_LIGHT, TC_ENEMY, TC_PICKUP, TC_PROP };

typedef struct {
    double x, y;
    float angle;                     /* degrees, 0 = east, CCW */
    int type, flags;
    uint8_t sel, mark;
} MThing;

typedef struct {
    const char *name;
    int category;
    uint32_t color;
    float radius, height;
    const char *icon;
} ThingInfo;
extern const ThingInfo g_thing_info[TH_COUNT];
extern const char *g_sector_type_names[ST_COUNT];

typedef struct {
    ARR(MVert) verts;
    ARR(MLine) lines;
    ARR(MSector) sectors;
    ARR(MThing) things;
    char name[64];
    bool has_cam;
    float cam_x, cam_y, cam_z, cam_yaw, cam_pitch;
    unsigned geo_rev;                /* bumped on any geometry / topology change */
} Map;

typedef struct {
    float floor_h, ceil_h;
    char floor_tex[TEXNAME], ceil_tex[TEXNAME], wall_tex[TEXNAME];
    int light;
} MapDefaults;

enum { SM_VERTS, SM_LINES, SM_SECTORS, SM_THINGS };

void map_init(Map *m);
void map_free(Map *m);
void map_copy(Map *dst, const Map *src);
void map_defaults_init(MapDefaults *d);
void sector_init(MSector *s, const MapDefaults *d);
void side_init(MSide *s, int sector, const char *tex);

/* queries */
int map_find_vertex(const Map *m, double x, double y, double eps);
int map_find_line(const Map *m, int a, int b);
int map_sector_at(const Map *m, double x, double y);
double map_line_length(const Map *m, int l);
double seg_dist(double px, double py, double ax, double ay, double bx, double by, double *t);
bool poly_contains(const D2 *p, int n, double x, double y);
bool map_bounds(const Map *m, double *x0, double *y0, double *x1, double *y1);
void map_sector_label_point(const Map *m, int s, double *x, double *y);
int map_sector_line_count(const Map *m, int s);
float map_lowest_neighbor_ceil(const Map *m, int s, float fallback);
float map_lowest_neighbor_floor(const Map *m, int s, float fallback);
float map_highest_neighbor_floor(const Map *m, int s, float fallback);

/* sector boundary loops (vertex indices); loops[] holds start offsets, last = count */
void map_sector_loops(const Map *m, int s, IntArr *verts, IntArr *loop_start);

/* edits */
int map_add_vertex(Map *m, double x, double y);
int map_split_line(Map *m, int line, int vert);
void map_draw_shape(Map *m, const D2 *pts, int n, bool closed, const MapDefaults *def);
void map_delete_selection(Map *m, int selmode);
void map_finalize_move(Map *m, const uint8_t *moved);
void map_join_sectors(Map *m, bool merge);
void map_flip_lines(Map *m);
void map_cleanup(Map *m);
void map_collect_sel_verts(const Map *m, int selmode, uint8_t *mask);
void map_mirror_fix(Map *m, const uint8_t *vmask);
void map_select_none(Map *m);
int map_sel_count(const Map *m, int selmode);

/* clipboard */
void map_extract_selection(const Map *src, int selmode, Map *clip);
void map_paste(Map *m, const Map *clip, double dx, double dy, int selmode);

/* templates */
enum { TPL_EMPTY, TPL_STARTER, TPL_DUNGEON, TPL_ARENA, TPL_COUNT };
extern const char *g_template_names[TPL_COUNT];
extern const char *g_template_desc[TPL_COUNT];
void map_make_template(Map *m, int tpl, const MapDefaults *def);

/* io */
void map_write(const Map *m, Buf *out);
bool map_read(Map *m, const char *text);
bool map_save(const Map *m, const char *path);
bool map_load(Map *m, const char *path);

#endif
