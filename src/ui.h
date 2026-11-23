/* Sector Forge - immediate-mode UI toolkit */
#ifndef SF_UI_H
#define SF_UI_H
#include "gfx.h"

enum { BTN_DEFAULT, BTN_PRIMARY, BTN_GHOST, BTN_DANGER, BTN_SEG };
enum { LAYER_BASE = 0, LAYER_POPUP = 1, LAYER_MODAL = 2, LAYER_MODAL_POPUP = 3 };

typedef struct {
    const char *label;
    const char *shortcut;
    int icon;           /* -1 none */
    bool checked, disabled, separator;
} MenuItem;

typedef struct {
    const Input *in;
    float mx, my;
    int layer;                       /* layer of widgets currently being drawn */
    int top_layer;                   /* highest overlay layer under the mouse (last frame) */
    Rect blocks[64]; int block_layer[64]; int nblocks;
    Rect nblk[64]; int nblk_layer[64]; int nnblk;
    uint32_t hot, active, focus;
    uint32_t prev_hot;
    double hot_since;
    const char *tip; Rect tip_r; char tipbuf[160];
    int cursor;
    /* text editing */
    char edit[256]; int edit_cur, edit_sel; uint32_t edit_id; bool edit_commit, edit_cancel;
    double caret_time;
    /* drag helpers */
    float drag_x, drag_y, drag_val; bool dragging;
    /* popup (dropdown / menu) */
    uint32_t popup_id; int popup_kind; Rect popup_anchor; int popup_layer;
    const char **popup_items; int popup_n; int popup_sel; float popup_scroll;
    const MenuItem *menu_items; int menu_n;
    bool popup_opened_now;
    uint32_t result_id; int result;
    /* scroll content heights */
    uint32_t scroll_ids[32]; float scroll_h[32];
    /* caption interactive rects (for borderless title bar hit testing) */
    Rect nocap[96]; int nnocap;
    Rect nocap_next[96]; int nnocap_next;
    bool text_focus;
    bool any_hover_widget;
    double time;
} UI;

extern UI ui;

uint32_t ui_id(const char *s, int n);
void ui_begin(const Input *in);
void ui_end(void);
void ui_push_block(Rect r, int layer);
bool ui_hover(Rect r);
bool ui_mouse_free(void);            /* mouse not over any overlay above current layer */
void ui_tooltip(const char *text);
void ui_nocaption(Rect r);           /* mark a rect inside the title bar as interactive */
bool ui_is_caption_free(float x, float y);

bool ui_button(uint32_t id, Rect r, int icon, const char *label, int style);
bool ui_button_on(uint32_t id, Rect r, int icon, const char *label, int style, bool on);
bool ui_icon_btn(uint32_t id, Rect r, int icon, const char *tip, bool on);
bool ui_win_btn(uint32_t id, Rect r, int icon, bool danger);
bool ui_number(uint32_t id, Rect r, float *v, float step, int decimals, float vmin, float vmax, bool stepper);
bool ui_number_mixed(uint32_t id, Rect r, float *v, float step, int decimals, float vmin, float vmax, bool mixed);
int ui_text(uint32_t id, Rect r, char *buf, int cap, const char *placeholder, int icon);
bool ui_toggle(uint32_t id, Rect r, const char *label, bool *v);
bool ui_slider(uint32_t id, Rect r, float *v, float vmin, float vmax, int decimals);
bool ui_dropdown(uint32_t id, Rect r, const char **items, int n, int *sel);
bool ui_tabs(uint32_t id, Rect r, const char **labels, int n, int *active);
bool ui_menu_button(uint32_t id, Rect r, const char *label, const MenuItem *items, int n);
int ui_popup_result(uint32_t id);    /* -1 if nothing picked this frame */
bool ui_popup_open(uint32_t id);
void ui_close_popup(void);
void ui_open_menu_at(uint32_t id, float x, float y, const MenuItem *items, int n);

float ui_scroll_begin(uint32_t id, Rect r, float *scroll);
void ui_scroll_end(uint32_t id, Rect r, float *scroll, float content_bottom);

void ui_label(int font, float x, float y, const char *s, uint32_t hex);
void ui_section_title(Rect *area, int icon, const char *title);
void ui_kbd(float x, float y, const char *key, float *outw);
void ui_chip(float x, float y, int icon, const char *text, bool accent, float *outw);
void ui_panel(Rect r, uint32_t bg);

#endif
