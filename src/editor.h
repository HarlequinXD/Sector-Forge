/* Sector Forge - editor state shared by the 2D view, 3D view, play mode and panels */
#ifndef SF_EDITOR_H
#define SF_EDITOR_H
#include "map.h"
#include "project.h"
#include "render3d.h"
#include "ui.h"

typedef enum { MODE_2D, MODE_3D, MODE_PLAY } EdMode;

enum { T_SELECT, T_PAN, T_DRAW, T_RECT, T_POLY, T_LINES, T_VERTEX, T_SPLIT, T_MOVE, T_ROTATE, T_SCALE, T_THING, T_MEASURE, T_PAINT, T_COUNT };
enum { T3_SELECT, T3_FLY, T3_HEIGHT, T3_PAINT, T3_PICK, T3_ALIGN, T3_LIGHT, T3_COUNT };
enum { DRAG_NONE, DRAG_BOX, DRAG_MOVE, DRAG_ROTATE, DRAG_SCALE, DRAG_PAN, DRAG_MEASURE, DRAG_RECT, DRAG_POLY, DRAG_THING_ANGLE };
enum { PANEL_PROPS, PANEL_TEXTURES, PANEL_THINGS, PANEL_MAP };
enum { TEXT_AUTO, TEXT_FLOOR, TEXT_CEIL, TEXT_WALL };

typedef struct {
    float x, y, z, vz;
    float yaw, pitch;
    float health, armor;
    int ammo;
    bool on_ground, crouch;
    float bob, bob_t, eye;
    double fire_time, msg_time;
    char msg[96];
    float *floor_h, *ceil_h, *light;
    int nsec;
    int *door_state;     /* 0 closed/rest, 1 moving to open, 2 open wait, 3 closing */
    float *door_timer;
    uint8_t *thing_hidden, *thing_dead;
    float *thing_hp;
    int nthing;
    uint8_t *secret_found;
    int secrets_found, secrets_total, kills, kills_total, items, items_total;
    bool exited, dead;
    float hurt_flash, pickup_flash, damage_acc;
    double start_time, end_time;
} PlayState;

typedef struct Editor {
    Project proj;
    bool open;
    Map map;
    int map_idx;
    bool dirty;
    double last_save_time, last_edit_time;
    EdMode mode;
    int tool, tool3d, selmode;
    /* 2D view */
    double vx, vy, zoom;
    Rect view;
    int grid, major;
    bool show_grid, snap, snap_geo, show_lengths, show_things, show_heights, textured_2d;
    bool grid_popup;
    double mwx, mwy;
    D2 snap_pt;
    int snap_kind;               /* 0 none, 1 grid, 2 vertex, 3 line */
    int hv_vert, hv_line, hv_sector, hv_thing;
    /* drawing tools */
    ARR(D2) draw;
    bool drawing;
    int poly_sides;
    D2 rect_a;
    /* dragging */
    int drag;
    D2 drag_start, drag_cur, drag_center;
    D2 *orig_v; int norig_v;
    D2 *orig_t; int norig_t;
    uint8_t *move_mask;
    bool drag_moved;
    double drag_angle0, drag_dist0;
    float pan_sx, pan_sy;
    /* history */
    ARR(char *) undo;
    ARR(char *) redo;
    char undo_label[64];
    /* clipboard */
    Map clip;
    int clip_mode;
    bool has_clip;
    /* textures & things */
    char cur_flat[TEXNAME], cur_wall[TEXNAME];
    int tex_target;
    char tex_search[64];
    float tex_scroll;
    int tex_filter;
    int thing_type;
    /* panels */
    int panel;
    float panel_scroll[4];
    /* 3D */
    Cam3 cam;
    bool cam_valid;
    uint32_t hover3d;
    ARR(uint32_t) sel3d;
    bool looking;
    bool fullbright, wireframe;
    int hstep;
    double last_wheel_time;
    double wheel_label_time;
    uint32_t wheel_label_id;
    float wheel_label_from;
    /* play */
    PlayState play;
    EdMode play_return;
    /* feedback */
    char toast[160];
    double toast_time;
    int toast_kind;
    float fps;
    bool touch_ui;   /* running the touch shell (Android) */
} Editor;

extern Editor ed;

/* core (ed_core.c) */
bool ed_open_project(const char *file, char *err, size_t errcap);
void ed_close_project(void);
bool ed_load_map(int idx);
bool ed_save(void);
void ed_snapshot(const char *label);
void ed_snapshot_merge(const char *label);
void ed_undo(void);
void ed_redo(void);
void ed_mark_dirty(void);
void ed_toast(const char *msg, int kind);
void ed_select_all(void);
void ed_select_none(void);
void ed_invert_selection(void);
void ed_delete(void);
void ed_copy(bool cut);
void ed_paste(void);
void ed_duplicate(void);
void ed_join(bool merge);
void ed_flip_lines(void);
void ed_mirror(bool vertical);
void ed_rotate90(bool cw);
void ed_set_mode(EdMode m);
void ed_set_selmode(int sm);
void ed_set_tool(int t);
void ed_fit_view(void);
void ed_grid_step(int dir);
void ed_apply_texture(const char *name);
void ed_apply_thing_type(int type);
void ed_new_map(const char *title, int tpl);
double ed_snap_value(double v);
int ed_sel_sector_count(void);

/* 2D (ed2d.c) */
void ed2d_frame(Rect vp, const Input *in);
D2 ed2d_world_to_screen(double x, double y);
D2 ed2d_screen_to_world(float sx, float sy);
/* touch shell hooks */
void ed2d_finish_draw(bool closed);
bool ed2d_item_at(float sx, float sy);
void ed2d_select_at(float sx, float sy);
void ed2d_zoom_about(float sx, float sy, double factor);
void ed2d_pan_px(float dx, float dy);
void ed2d_render_view(Rect vp, const Input *in, double cx, double cy, double zoom);

/* 3D (ed3d.c) */
void ed3d_frame(Rect vp, const Input *in);
void ed3d_sync_camera_from_2d(void);
void ed3d_adjust(float dir, bool fine);   /* the mouse wheel / touch height dial */
void ed3d_texture_applied(const char *name);
const char *ed3d_surface_texture(uint32_t id);

/* play (play.c) */
void play_start(bool from_camera);
void play_stop(void);
void play_frame(Rect vp, const Input *in);

/* panels (panels.c) */
void panels_frame(Rect r, const Input *in);
void ed_toolbar(Rect r, const Input *in);
void ed_tool_rail(Rect r, const Input *in);
void ed_statusbar(Rect r, const Input *in);
void ed_shortcuts(const Input *in);

/* app-level dialogs (app.c) */
void app_dialog_new_map(void);
void app_dialog_delete_map(int idx);
void app_dialog_shortcuts(void);
void app_dialog_about(void);
void app_request_close_project(void);
void app_import_texture(void);

#endif
