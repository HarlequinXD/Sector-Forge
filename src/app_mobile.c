/* Sector Forge - touch shell for Android: splash, home (projects / templates / learn / settings),
   loading screen and a landscape editor redesigned for phones. The map engine (ed2d, ed3d, play)
   is shared with the desktop build; this file turns raw touches into the editor's mouse model
   plus virtual sticks, and draws Material-style chrome around it. */
#include "editor.h"
#include "assets.h"
#include "plat.h"
#include "glinc.h"
#include <time.h>
#include <ctype.h>

/* ================================================================== state */
enum { ST_SPLASH, ST_HOME, ST_LOADING, ST_EDITOR };
enum { HOME_PROJECTS, HOME_TEMPLATES, HOME_LEARN, HOME_SETTINGS };
enum { SHEET_PROPS, SHEET_TEX, SHEET_THINGS, SHEET_MAP };
enum { BS_NONE, BS_GRID, BS_MORE, BS_ACTIONS, BS_MENU, BS_PROJECT, BS_NEW_PROJECT, BS_NEW_MAP, BS_CONFIRM, BS_MESSAGE, BS_GUIDE, BS_PAUSE };
enum { CF_DELETE_PROJECT, CF_DELETE_MAP, CF_LEAVE };
enum { TXT_NONE, TXT_PROJECT_NAME, TXT_MAP_NAME, TXT_SECTOR_NAME, TXT_TEX_SEARCH, TXT_PROJ_SEARCH, TXT_NUMBER };
enum { NUM_FLOOR, NUM_CEIL, NUM_LIGHT, NUM_TAG, NUM_GRID, NUM_OFFX, NUM_OFFY, NUM_VX, NUM_VY, NUM_ANGLE };

#define TOPBAR_H 52.0f
#define RAIL_W 68.0f
#define SLOP 9.0f

typedef struct { Rect r; float *scroll; bool horiz; float content; } ScrollReg;

typedef struct {
    bool used, released;
    int id, owner;
    float x, y, sx, sy, px, py;
    double t0;
    bool moved, pressed_sent, long_fired;
    int btn, scroll;
} Finger;

enum { OWN_NONE, OWN_UI, OWN_SCROLL, OWN_CANVAS, OWN_STICK, OWN_LOOK, OWN_BTN, OWN_IGNORE };
enum { PB_NONE, PB_FIRE, PB_USE, PB_JUMP, PB_CROUCH, PB_UP, PB_DOWN };

static struct {
    int state;
    double state_time;
    float scale;
    int asset_stage;
    float progress, shown;
    char status[128];
    /* home */
    ProjectList projects;
    int home_tab;
    float home_scroll, tpl_scroll, tpls_scroll, learn_scroll, set_scroll;
    char proj_filter[64];
    float *tpl_seg[TPL_COUNT];
    int tpl_nseg[TPL_COUNT];
    int proj_sel;               /* project for the action sheet / confirm */
    int new_tpl;
    bool first_run_guide;
    /* loading */
    char load_file[600], load_name[64];
    int load_step;
    double last_autosave;
    /* editor chrome */
    bool sheet_open;
    int sheet_tab;
    float sheet_scroll[4];
    int bsheet;
    double bsheet_t;
    float bs_scroll;
    int confirm_kind, confirm_idx;
    char msg_title[80], msg_body[300];
    int tex_filter, tex_target;
    char tex_search[32];
    char recent[8][TEXNAME];
    int nrecent;
    int guide_tab;
    float act_x, act_y;
    /* height dial */
    float dial_acc, dial_prev;
    bool dial_drag;
    double h_change_t;
    float h_from, h_to;
    int h_kind;
    /* text input */
    int txt_req, txt_num;
    /* gestures */
    Finger F[10];
    Input vin;
    bool pinch;
    float pinch_d, pinch_mx, pinch_my;
    int multi_max;
    bool multi_moved;
    double multi_t0, last_two_tap;
    double last_tap_t;
    float last_tap_x, last_tap_y;
    bool pend_release;
    float pend_x, pend_y;
    bool lp_fired;
    float lp_x, lp_y;
    bool loupe;
    float loupe_x, loupe_y;
    ScrollReg sreg[16], sreg_next[16];
    int nsreg, nsreg_next;
    Rect reg[64], reg_next[64];
    int nreg, nreg_next;
    float fling_v[16];
    /* play */
    bool paused;
    float stick_bx, stick_by, stick_x, stick_y;
    bool stick_on;
    bool haptics;
    double fps_t;
    int fps_frames;
} M;

static Col C(uint32_t h) { return col_hex(h); }
static Col CA(uint32_t h, int a) { Col c = col_hex(h); c.a = (uint8_t)a; return c; }
#define ID(s) ui_id(s, __LINE__)
#define IDI(s, i) ui_id(s, __LINE__ * 977 + (i))

/* Material-ish dark palette on top of the desktop theme */
#define MS_BG       0x0E0F12
#define MS_SURF     0x16181C
#define MS_SURF2    0x1C1F24
#define MS_SURF3    0x23262D
#define MS_OUTLINE  0x2B2F37
#define MS_ON       0xE7E9EE
#define MS_ON2      0xAAB2BF
#define MS_ON3      0x7F8794
#define MS_ACC      0xFF7A1A
#define MS_ACC_CT   0x3A2210
#define MS_ACC_ON   0xFF9A4D

/* ================================================================== fonts */
static void build_fonts(float scale) {
    gfx_font_build(FONT_UI, "Inter", 15.0f, 400, scale);
    gfx_font_build(FONT_UI_BOLD, "Inter", 15.0f, 600, scale);
    gfx_font_build(FONT_SMALL, "Inter", 12.5f, 500, scale);
    gfx_font_build(FONT_MONO, "Consolas", 14.0f, 400, scale);
    gfx_font_build(FONT_MONO_SMALL, "Consolas", 12.0f, 400, scale);
    gfx_font_build(FONT_HEAD, "Inter", 22.0f, 700, scale);
    gfx_font_build(FONT_TITLE, "Inter", 38.0f, 800, scale);
    gfx_font_build(FONT_HUD, "Consolas", 28.0f, 700, scale);
    M.scale = scale;
}

/* ================================================================== small helpers */
static void set_state(int s) { M.state = s; M.state_time = plat_time(); }
static void haptic(int k) { if (M.haptics) plat_haptic(k); }

static void region(Rect r) {
    if (M.nreg_next < 64) M.reg_next[M.nreg_next++] = r;
    ui_push_block(r, ui.layer);
}
static bool in_region(float x, float y) {
    for (int i = 0; i < M.nreg; i++) if (rect_has(M.reg[i], x, y)) return true;
    return false;
}

static void open_bsheet(int k) { M.bsheet = k; M.bsheet_t = plat_time(); M.bs_scroll = 0; ui.active = 0; }
static void close_bsheet(void) { M.bsheet = BS_NONE; }
static void message(const char *t, const char *b) {
    sf_strcpy(M.msg_title, sizeof M.msg_title, t);
    sf_strcpy(M.msg_body, sizeof M.msg_body, b);
    open_bsheet(BS_MESSAGE);
}

static void text_request(int req, const char *title, const char *initial, int kind) {
    M.txt_req = req;
    if (!plat_text_input_begin(req, title, initial, kind)) M.txt_req = TXT_NONE;
}

static void fmt_age(int64_t t, char *out, size_t cap) {
    int64_t d = (int64_t)time(NULL) - t;
    if (t <= 0) snprintf(out, cap, "unknown");
    else if (d < 60) snprintf(out, cap, "just now");
    else if (d < 3600) snprintf(out, cap, "%d min ago", (int)(d / 60));
    else if (d < 86400) snprintf(out, cap, "%d hour%s ago", (int)(d / 3600), d / 3600 == 1 ? "" : "s");
    else if (d < 86400 * 2) snprintf(out, cap, "yesterday");
    else if (d < 86400 * 7) snprintf(out, cap, "%d days ago", (int)(d / 86400));
    else if (d < 86400 * 14) snprintf(out, cap, "last week");
    else snprintf(out, cap, "%d weeks ago", (int)(d / (86400 * 7)));
}

static void push_recent(const char *name) {
    int at = -1;
    for (int i = 0; i < M.nrecent; i++) if (!strcmp(M.recent[i], name)) at = i;
    if (at < 0) { at = SF_MIN(M.nrecent, 7); if (M.nrecent < 8) M.nrecent++; }
    for (int i = at; i > 0; i--) memcpy(M.recent[i], M.recent[i - 1], TEXNAME);
    sf_strcpy(M.recent[0], TEXNAME, name);
}

/* ================================================================== touch routing
   Raw touches -> one virtual mouse (UI and the 2D/3D canvas) + virtual sticks/buttons. */
static Rect g_vp;                   /* editor viewport (last layout) */
static bool canvas_active(void) { return M.state == ST_EDITOR && M.bsheet == BS_NONE; }

static void play_buttons(float W, float H, const Input *in, Rect *fire, Rect *use, Rect *jump, Rect *crouch, Rect *pause) {
    float r = H * 0.2f;  /* ~82dp on a phone */
    float rx = W - in->inset_r - 28, by = H - in->inset_b - 20;
    *fire = rect(rx - r * 1.1f, by - r * 1.6f, r * 1.1f, r * 1.1f);
    *use = rect(fire->x - r * 0.95f, fire->y - r * 0.55f, r * 0.72f, r * 0.72f);
    *jump = rect(fire->x - r * 0.75f, fire->y + fire->h - r * 0.28f, r * 0.72f, r * 0.72f);
    *crouch = rect(rx - r * 0.72f, fire->y - r * 1.05f, r * 0.72f, r * 0.72f);
    *pause = rect(in->inset_l + 16, in->inset_t + 14, 52, 52);
}
static void ed3d_buttons(Rect vp, const Input *in, Rect *up, Rect *dn, float *sbx, float *sby, float *srad) {
    *srad = 54;
    *sbx = vp.x + 22 + *srad + 4;
    *sby = vp.y + vp.h - 22 - *srad - in->inset_b;
    *up = rect(*sbx + *srad + 18, *sby - 4, 48, 48);
    *dn = rect(up->x + 56, up->y, 48, 48);
}

static int classify(float x, float y, const Input *in) {
    if (!canvas_active() || in_region(x, y)) return OWN_UI;
    if (ed.mode == MODE_PLAY) {
        if (M.paused || ed.play.dead || ed.play.exited) return OWN_UI;
        Rect fire, use, jump, crouch, pause;
        play_buttons(in->win_w, in->win_h, in, &fire, &use, &jump, &crouch, &pause);
        if (rect_has(pause, x, y)) return OWN_UI;
        if (rect_has(fire, x, y) || rect_has(use, x, y) || rect_has(jump, x, y) || rect_has(crouch, x, y)) return OWN_BTN;
        return x < in->win_w * 0.45f ? OWN_STICK : OWN_LOOK;
    }
    if (!rect_has(g_vp, x, y)) return OWN_UI;
    if (ed.mode == MODE_3D) {
        Rect up, dn; float bx, by, rr;
        ed3d_buttons(g_vp, in, &up, &dn, &bx, &by, &rr);
        if (rect_has(up, x, y) || rect_has(dn, x, y)) return OWN_BTN;
        float dx = x - bx, dy = y - by;
        if (dx * dx + dy * dy < rr * rr * 1.7f) return OWN_STICK;
    }
    return OWN_CANVAS;
}

static int btn_at(float x, float y, const Input *in) {
    if (ed.mode == MODE_PLAY) {
        Rect fire, use, jump, crouch, pause;
        play_buttons(in->win_w, in->win_h, in, &fire, &use, &jump, &crouch, &pause);
        if (rect_has(fire, x, y)) return PB_FIRE;
        if (rect_has(use, x, y)) return PB_USE;
        if (rect_has(jump, x, y)) return PB_JUMP;
        if (rect_has(crouch, x, y)) return PB_CROUCH;
    } else {
        Rect up, dn; float bx, by, rr;
        ed3d_buttons(g_vp, in, &up, &dn, &bx, &by, &rr);
        if (rect_has(up, x, y)) return PB_UP;
        if (rect_has(dn, x, y)) return PB_DOWN;
    }
    return PB_NONE;
}

static bool draw_tool(void) { return ed.mode == MODE_2D && (ed.tool == T_DRAW || ed.tool == T_LINES); }

static void open_actions(float x, float y);

static void route(const Input *in) {
    Input *v = &M.vin;
    *v = *in;   /* keyboard, gamepad sticks/buttons (already virtual) and the physical mouse */
    M.loupe = false;
    M.lp_fired = false;
    if (!in->touch) { M.pinch = false; return; }   /* mouse / DeX: pass straight through */
    memset(v->mdown, 0, sizeof v->mdown);
    memset(v->mpressed, 0, sizeof v->mpressed);
    memset(v->mreleased, 0, sizeof v->mreleased);
    memset(v->mdouble, 0, sizeof v->mdouble);
    v->mx = v->my = -9999;
    v->mdx = v->mdy = 0;
    if (M.pend_release) {
        v->mx = M.pend_x; v->my = M.pend_y;
        v->mreleased[0] = true;
        M.pend_release = false;
    }
    /* sync fingers */
    for (int i = 0; i < 10; i++) M.F[i].released = false;
    for (int t = 0; t < in->ntouch; t++) {
        const TouchPt *tp = &in->touches[t];
        Finger *f = NULL;
        for (int i = 0; i < 10; i++) if (M.F[i].used && M.F[i].id == tp->id) f = &M.F[i];
        if (!f) {
            if (!tp->pressed && !tp->released) continue;
            for (int i = 0; i < 10; i++) if (!M.F[i].used) { f = &M.F[i]; break; }
            if (!f) continue;
            memset(f, 0, sizeof *f);
            f->used = true; f->id = tp->id;
            f->sx = f->x = f->px = tp->x; f->sy = f->y = f->py = tp->y;
            f->t0 = in->time;
            f->scroll = -1;
            f->owner = classify(tp->x, tp->y, in);
            /* only one finger drives the virtual mouse; extra fingers on the UI are ignored */
            for (int i = 0; i < 10; i++) {
                const Finger *o = &M.F[i];
                if (o == f || !o->used) continue;
                if (f->owner == OWN_UI && (o->owner == OWN_UI || o->owner == OWN_SCROLL || o->owner == OWN_CANVAS)) f->owner = OWN_IGNORE;
                if (f->owner == OWN_CANVAS && (o->owner == OWN_UI || o->owner == OWN_SCROLL)) f->owner = OWN_IGNORE;
            }
            if (f->owner == OWN_UI)
                for (int s = 0; s < M.nsreg; s++) if (rect_has(M.sreg[s].r, tp->x, tp->y)) f->scroll = s;
            if (f->owner == OWN_BTN) f->btn = btn_at(tp->x, tp->y, in);
            if (f->owner == OWN_STICK) {
                if (ed.mode == MODE_PLAY) { M.stick_bx = tp->x; M.stick_by = tp->y; }
                else { Rect a, b; float rr; ed3d_buttons(g_vp, in, &a, &b, &M.stick_bx, &M.stick_by, &rr); }
                M.stick_on = true;
            }
            for (int s = 0; s < 16; s++) M.fling_v[s] = 0;
        }
        f->px = f->x; f->py = f->y;
        f->x = tp->x; f->y = tp->y;
        if (fabsf(f->x - f->sx) > SLOP || fabsf(f->y - f->sy) > SLOP) f->moved = true;
        if (tp->released) f->released = true;
    }
    /* multi-finger taps (undo / grid size) */
    int down = 0;
    bool any_moved = false;
    for (int i = 0; i < 10; i++) if (M.F[i].used) { down++; if (M.F[i].moved) any_moved = true; }
    if (down > 0 && M.multi_max == 0) M.multi_t0 = in->time;
    M.multi_max = SF_MAX(M.multi_max, down);
    M.multi_moved |= any_moved;

    int ncanvas = 0;
    Finger *cf[2] = { 0 };
    for (int i = 0; i < 10; i++)
        if (M.F[i].used && M.F[i].owner == OWN_CANVAS) { if (ncanvas < 2) cf[ncanvas] = &M.F[i]; ncanvas++; }
    /* second canvas finger -> pinch; cancel anything the first finger started */
    if (ncanvas >= 2 && !M.pinch) {
        M.pinch = true;
        M.pinch_d = hypotf(cf[0]->x - cf[1]->x, cf[0]->y - cf[1]->y);
        M.pinch_mx = (cf[0]->x + cf[1]->x) * 0.5f; M.pinch_my = (cf[0]->y + cf[1]->y) * 0.5f;
        for (int k = 0; k < 2; k++) if (cf[k]->pressed_sent) {
            v->kpressed[KEY_ESC] = true;
            v->mx = cf[k]->sx; v->my = cf[k]->sy;
            v->mreleased[0] = true;
            cf[k]->pressed_sent = false;
        }
    }
    if (M.pinch) {
        if (ncanvas >= 2) {
            float d = hypotf(cf[0]->x - cf[1]->x, cf[0]->y - cf[1]->y);
            float mx = (cf[0]->x + cf[1]->x) * 0.5f, my = (cf[0]->y + cf[1]->y) * 0.5f;
            if (ed.mode == MODE_2D) {
                if (M.pinch_d > 10 && d > 10) ed2d_zoom_about(mx, my, d / M.pinch_d);
                ed2d_pan_px(mx - M.pinch_mx, my - M.pinch_my);
            } else if (ed.mode == MODE_3D) {
                V3 f, r, u;
                r3d_cam_vectors(&ed.cam, &f, &r, &u);
                float dolly = (d - M.pinch_d) * 3.0f, sx = -(mx - M.pinch_mx) * 1.6f, sz = (my - M.pinch_my) * 1.6f;
                ed.cam.x += f.x * dolly + r.x * sx; ed.cam.y += f.y * dolly + r.y * sx; ed.cam.z += f.z * dolly + sz;
            }
            M.pinch_d = d; M.pinch_mx = mx; M.pinch_my = my;
        }
        for (int k = 0; k < 2 && k < ncanvas; k++) if (cf[k]->released) M.pinch = false;
        if (!M.pinch) for (int i = 0; i < 10; i++) if (M.F[i].used && M.F[i].owner == OWN_CANVAS && !M.F[i].released) M.F[i].owner = OWN_IGNORE;
    }

    for (int i = 0; i < 10; i++) {
        Finger *f = &M.F[i];
        if (!f->used) continue;
        float dx = f->x - f->px, dy = f->y - f->py;
        switch (f->owner) {
        case OWN_UI:
            if (f->long_fired) { f->owner = OWN_IGNORE; break; }
            v->mx = f->x; v->my = f->y;
            if (!f->pressed_sent) { v->mpressed[0] = true; f->pressed_sent = true; }
            v->mdown[0] = !f->released;
            if (f->released) v->mreleased[0] = true;
            if (!f->released && f->moved && f->scroll >= 0 && f->scroll < M.nsreg) {
                ScrollReg *s = &M.sreg[f->scroll];
                float along = s->horiz ? fabsf(f->x - f->sx) : fabsf(f->y - f->sy);
                if (along > SLOP) { f->owner = OWN_SCROLL; ui.active = 0; v->mdown[0] = false; v->mx = v->my = -9999; }
            }
            /* long-press (project cards): reported once, the press never becomes a click */
            if (f->owner == OWN_UI && !f->released && !f->moved && in->time - f->t0 > 0.5) {
                f->long_fired = true;
                M.lp_fired = true; M.lp_x = f->x; M.lp_y = f->y;
                ui.active = 0;
                v->mdown[0] = false;
                haptic(HAPTIC_LONG);
            }
            break;
        case OWN_SCROLL: {
            if (f->scroll >= 0 && f->scroll < M.nsreg) {
                ScrollReg *s = &M.sreg[f->scroll];
                float d = s->horiz ? dx : dy;
                *s->scroll -= d;
                M.fling_v[f->scroll] = (float)(-d / SF_MAX(in->dt, 1e-3));
                if (f->released) {}
            }
        } break;
        case OWN_CANVAS: {
            if (M.pinch) break;
            if (ed.mode == MODE_2D) {
                if (draw_tool()) {
                    v->mx = f->x; v->my = f->y;
                    M.loupe = !f->released; M.loupe_x = f->x; M.loupe_y = f->y;
                    if (f->released) {
                        v->mpressed[0] = true;
                        if (in->time - M.last_tap_t < 0.35 && hypotf(f->x - M.last_tap_x, f->y - M.last_tap_y) < 28) v->mdouble[0] = true;
                        M.last_tap_t = in->time; M.last_tap_x = f->x; M.last_tap_y = f->y;
                        M.pend_release = true; M.pend_x = f->x; M.pend_y = f->y;
                        haptic(HAPTIC_TICK);
                    }
                } else {
                    if (!f->pressed_sent && !f->long_fired) {
                        v->mx = f->sx; v->my = f->sy;
                        if (f->moved) {
                            v->mpressed[0] = true; v->mdown[0] = true; f->pressed_sent = true;
                        } else if (f->released) {
                            v->mpressed[0] = true; v->mdown[0] = true;
                            if (in->time - M.last_tap_t < 0.35 && hypotf(f->x - M.last_tap_x, f->y - M.last_tap_y) < 28) v->mdouble[0] = true;
                            M.last_tap_t = in->time; M.last_tap_x = f->x; M.last_tap_y = f->y;
                            M.pend_release = true; M.pend_x = f->sx; M.pend_y = f->sy;
                        } else if (in->time - f->t0 > 0.45 && ncanvas == 1) {
                            f->long_fired = true;
                            haptic(HAPTIC_LONG);
                            ed2d_select_at(f->sx, f->sy);
                            open_actions(f->sx, f->sy);
                        }
                    } else if (f->pressed_sent) {
                        v->mx = f->x; v->my = f->y;
                        v->mdown[0] = !f->released;
                        if (f->released) v->mreleased[0] = true;
                    }
                }
            } else if (ed.mode == MODE_3D) {
                if (f->moved) { v->vlook_x += dx; v->vlook_y += dy; }
                if (f->released && !f->moved && in->time - f->t0 < 0.4) {
                    v->mx = f->x; v->my = f->y;
                    v->mpressed[0] = true; v->mdown[0] = true;
                    if (in->time - M.last_tap_t < 0.35 && hypotf(f->x - M.last_tap_x, f->y - M.last_tap_y) < 28) v->mdouble[0] = true;
                    M.last_tap_t = in->time; M.last_tap_x = f->x; M.last_tap_y = f->y;
                    M.pend_release = true; M.pend_x = f->x; M.pend_y = f->y;
                }
            }
        } break;
        case OWN_STICK: {
            float rad = ed.mode == MODE_PLAY ? 60.0f : 54.0f;
            float ox = f->x - M.stick_bx, oy = f->y - M.stick_by;
            float l = hypotf(ox, oy);
            if (l > rad) { ox *= rad / l; oy *= rad / l; }
            M.stick_x = ox; M.stick_y = oy;
            float nx = ox / rad, ny = oy / rad;
            /* small dead zone, then a gentle curve for fine control */
            float m = hypotf(nx, ny);
            if (m < 0.12f) nx = ny = 0;
            else { float k = (m - 0.12f) / 0.88f; k = k * (0.4f + 0.6f * k) / m; nx *= k; ny *= k; }
            v->vmove_x += nx; v->vmove_y += -ny;
            if (f->released) { M.stick_on = false; M.stick_x = M.stick_y = 0; }
        } break;
        case OWN_LOOK:
            v->vlook_x += dx * 1.15f; v->vlook_y += dy * 1.15f;
            break;
        case OWN_BTN:
            if (f->released) break;
            switch (f->btn) {
            case PB_FIRE: v->vfire = true; break;
            case PB_CROUCH: v->vcrouch = true; break;
            case PB_JUMP: if (in->time == f->t0 || !f->pressed_sent) { v->vjump = true; f->pressed_sent = true; } break;
            case PB_USE: if (!f->pressed_sent) { v->vuse = true; f->pressed_sent = true; haptic(HAPTIC_TICK); } break;
            case PB_UP: v->vlift = 1; break;
            case PB_DOWN: v->vlift = -1; break;
            default: break;
            }
            break;
        default: break;
        }
    }
    /* release bookkeeping */
    bool any_left = false;
    for (int i = 0; i < 10; i++) {
        if (!M.F[i].used) continue;
        if (M.F[i].released) {
            if (M.F[i].owner == OWN_STICK) { M.stick_on = false; M.stick_x = M.stick_y = 0; }
            M.F[i].used = false;
        } else any_left = true;
    }
    if (!any_left && M.multi_max > 0) {
        double dur = in->time - M.multi_t0;
        if (!M.multi_moved && dur < 0.5 && M.state == ST_EDITOR && M.bsheet == BS_NONE && ed.mode != MODE_PLAY) {
            if (M.multi_max == 3) { ed_undo(); haptic(HAPTIC_CONFIRM); }
            else if (M.multi_max == 2 && ed.mode == MODE_2D) {
                if (in->time - M.last_two_tap < 0.45) {
                    static const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
                    int k = 0;
                    while (k < 9 && sizes[k] <= ed.grid) k++;
                    if (ed.grid >= 512) k = 0;
                    ed.grid = sizes[k];
                    char t[32]; snprintf(t, sizeof t, "Grid %d", ed.grid);
                    ed_toast(t, 0);
                    M.last_two_tap = 0;
                } else M.last_two_tap = in->time;
            }
        }
        M.multi_max = 0;
        M.multi_moved = false;
        M.pinch = false;
    }
    /* kinetic scrolling */
    for (int s = 0; s < M.nsreg; s++) {
        bool held = false;
        for (int i = 0; i < 10; i++) if (M.F[i].used && M.F[i].owner == OWN_SCROLL && M.F[i].scroll == s) held = true;
        if (held || fabsf(M.fling_v[s]) < 5) { if (!held) M.fling_v[s] = 0; continue; }
        *M.sreg[s].scroll += M.fling_v[s] * (float)in->dt;
        M.fling_v[s] *= powf(0.06f, (float)in->dt);
    }
}

/* ================================================================== widgets */
static bool tap(uint32_t id, Rect r, bool *held) {
    bool h = ui_hover(r), hd = false, click = false;
    if (h) ui.hot = id;
    if (h && ui.in->mpressed[0] && !ui.active) ui.active = id;
    if (ui.active == id) {
        hd = true;
        if (ui.in->mreleased[0]) { click = h; ui.active = 0; }
        else if (!ui.in->mdown[0]) ui.active = 0;
    }
    if (held) *held = hd && h;
    if (click) haptic(HAPTIC_TICK);
    return click;
}
static bool long_pressed(Rect r) { return M.lp_fired && rect_has(r, M.lp_x, M.lp_y) && ui.layer >= ui.top_layer; }

static void ripple(Rect r, float rad, bool held) { if (held) gfx_rrect(r, rad, col(255, 255, 255, 18)); }

static bool icon_button(uint32_t id, Rect r, int icon, bool on, float isz) {
    bool held;
    bool c = tap(id, r, &held);
    if (on) gfx_rrect(r, r.h * 0.5f, C(MS_ACC_CT));
    ripple(r, r.h * 0.5f, held);
    gfx_icon(icon, r.x + r.w * 0.5f, r.y + r.h * 0.5f, isz, on ? C(MS_ACC_ON) : C(MS_ON));
    return c;
}

static void kebab(float cx, float cy, Col c) {
    for (int i = -1; i <= 1; i++) gfx_circle(cx, cy + i * 6.5f, 2.2f, c);
}

enum { MB_FILLED, MB_TONAL, MB_OUTLINE, MB_TEXT, MB_DANGER };
static bool mbutton(uint32_t id, Rect r, int icon, const char *label, int style) {
    bool held;
    bool c = tap(id, r, &held);
    Col bg = col(0, 0, 0, 0), fg = C(MS_ON), bd = col(0, 0, 0, 0);
    int font = FONT_UI_BOLD;
    switch (style) {
    case MB_FILLED: bg = C(held ? 0xE86A10 : MS_ACC); fg = C(0x1A0D02); break;
    case MB_TONAL: bg = C(MS_ACC_CT); fg = C(MS_ACC_ON); break;
    case MB_OUTLINE: bd = C(MS_OUTLINE); break;
    case MB_DANGER: bg = C(0x3A1414); fg = C(0xFF8A7A); break;
    default: fg = C(MS_ACC_ON); break;
    }
    float rad = r.h * 0.5f;
    if (bg.a) gfx_rrect(r, rad, bg);
    if (bd.a) gfx_rrect_line(r, rad, 1, bd);
    ripple(r, rad, held && style != MB_FILLED);
    float tw = label ? gfx_text_w(font, label) : 0, isz = 20;
    float total = tw + (icon >= 0 ? isz + (label ? 8 : 0) : 0);
    float x = r.x + (r.w - total) * 0.5f, cy = r.y + r.h * 0.5f;
    if (icon >= 0) { gfx_icon(icon, x + isz * 0.5f, cy, isz, fg); x += isz + 8; }
    if (label) gfx_text(font, x, cy - gfx_font_h(font) * 0.5f, label, fg);
    return c;
}

static bool chip(uint32_t id, Rect r, int icon, const char *label, bool on, bool caret) {
    bool held;
    bool c = tap(id, r, &held);
    gfx_rrect(r, 10, on ? C(MS_ACC_CT) : CA(0x101114, 235));
    gfx_rrect_line(r, 10, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
    ripple(r, 10, held);
    Col fg = on ? C(MS_ACC_ON) : C(MS_ON);
    float x = r.x + 14, cy = r.y + r.h * 0.5f;
    if (icon >= 0) { gfx_icon(icon, x + 9, cy, 18, fg); x += 28; }
    gfx_text(FONT_UI_BOLD, x, cy - gfx_font_h(FONT_UI_BOLD) * 0.5f, label, fg);
    if (caret) gfx_icon(IC_chev_down, r.x + r.w - 18, cy, 14, fg);
    return c;
}
static float chip_w(int icon, const char *label, bool caret) {
    return 28 + (icon >= 0 ? 28 : 0) + gfx_text_w(FONT_UI_BOLD, label) + (caret ? 20 : 0);
}

static bool toggle_switch(uint32_t id, Rect r, const char *label, bool *v) {
    bool held;
    bool c = tap(id, r, &held);
    ripple(r, 10, held);
    gfx_text(FONT_UI, r.x + 4, r.y + (r.h - gfx_font_h(FONT_UI)) * 0.5f, label, C(MS_ON));
    Rect s = rect(r.x + r.w - 52, r.y + (r.h - 30) * 0.5f, 52, 30);
    gfx_rrect(s, 15, *v ? C(MS_ACC) : C(MS_SURF3));
    if (!*v) gfx_rrect_line(s, 15, 1.5f, C(0x4A505B));
    float kx = *v ? s.x + s.w - 15 : s.x + 15;
    gfx_circle(kx, s.y + 15, *v ? 11 : 8, *v ? C(0x2A1606) : C(0x8E96A3));
    if (c) *v = !*v;
    return c;
}

static bool slider(uint32_t id, Rect r, float *v, float mn, float mx) {
    bool held;
    tap(id, r, &held);
    bool changed = false;
    if (ui.active == id && ui.in->mdown[0]) {
        float t = SF_CLAMP((ui.in->mx - r.x - 12) / SF_MAX(r.w - 24, 1), 0, 1);
        float nv = mn + (mx - mn) * t;
        if (nv != *v) { *v = nv; changed = true; }
    }
    float t = (*v - mn) / (mx - mn);
    float cy = r.y + r.h * 0.5f, x0 = r.x + 12, x1 = r.x + r.w - 12;
    gfx_rrect(rect(x0, cy - 3, x1 - x0, 6), 3, C(MS_SURF3));
    gfx_rrect(rect(x0, cy - 3, (x1 - x0) * t, 6), 3, C(MS_ACC));
    gfx_circle(x0 + (x1 - x0) * t, cy, ui.active == id ? 13 : 11, C(0xF5F6F8));
    return changed;
}

/* big - value + stepper; tap the value for the number keyboard */
static int stepper(uint32_t id, Rect r, const char *val) {
    gfx_rrect(r, 14, C(MS_SURF2));
    gfx_rrect_line(r, 14, 1, C(MS_OUTLINE));
    float bw = SF_MIN(52, r.w * 0.3f);
    int res = 0;
    bool h1, h2, h3;
    Rect a = rect(r.x, r.y, bw, r.h), b = rect(r.x + r.w - bw, r.y, bw, r.h), mid = rect(r.x + bw, r.y, r.w - bw * 2, r.h);
    if (tap(id, a, &h1)) res = -1;
    if (tap(id ^ 0x55u, b, &h2)) res = 1;
    if (tap(id ^ 0xAAu, mid, &h3)) res = 2;
    ripple(a, 14, h1); ripple(b, 14, h2); ripple(mid, 10, h3);
    gfx_icon(IC_minus, a.x + a.w * 0.5f, a.y + a.h * 0.5f, 20, C(MS_ON));
    gfx_icon(IC_plus, b.x + b.w * 0.5f, b.y + b.h * 0.5f, 20, C(MS_ON));
    gfx_text_center(r.h > 44 ? FONT_HEAD : FONT_UI_BOLD, mid, val, C(MS_ON));
    return res;
}

static float scroll_area(Rect r, float *scroll, float content, bool horiz) {
    float maxs = SF_MAX(0, content - (horiz ? r.w : r.h));
    if (*scroll < 0) *scroll += (0 - *scroll) * 0.35f;
    if (*scroll > maxs) *scroll += (maxs - *scroll) * 0.35f;
    if (fabsf(*scroll) < 0.3f && *scroll < 0) *scroll = 0;
    if (M.nsreg_next < 16) {
        ScrollReg *s = &M.sreg_next[M.nsreg_next++];
        s->r = r; s->scroll = scroll; s->horiz = horiz; s->content = content;
    }
    if (!ui.in->touch && ui_hover(r) && ui.in->wheel != 0) *scroll -= ui.in->wheel * 60;
    return *scroll;
}

static void section_label(float x, float y, const char *t) { gfx_text(FONT_SMALL, x, y, t, C(MS_ON3)); }

static void tex_tile(const char *name, Rect r) {
    const Tex *t = tex_get(name);
    gfx_quad_tex(t, r, 0, 0, 1, 1, col(255, 255, 255, 255));
}

static void segment_lines(Rect r, const float *seg, int n, Col c, float th) {
    for (int i = 0; i < n; i++) {
        const float *s = seg + i * 4;
        gfx_line(r.x + s[0] * r.w, r.y + s[1] * r.h, r.x + s[2] * r.w, r.y + s[3] * r.h, th, c);
    }
}
static void map_thumb(Rect r, const float *seg, int n, bool accent, float rad) {
    gfx_rrect(r, rad, C(0x0B0C0F));
    gfx_rect(rect(r.x, r.y + r.h - rad, r.w, rad), C(0x0B0C0F));   /* square bottom: the card text sits under it */
    gfx_clip_push(r);
    for (float x = r.x + 6; x < r.x + r.w; x += 14) gfx_rect(rect(x, r.y, 1, r.h), C(0x16181D));
    for (float y = r.y + 6; y < r.y + r.h; y += 14) gfx_rect(rect(r.x, y, r.w, 1), C(0x16181D));
    float s = SF_MIN(r.w, r.h) * 0.8f;
    Rect q = rect(r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s);
    if (n) segment_lines(q, seg, n, accent ? C(0xFF9A4D) : C(0xC9CFD8), 1.6f);
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
        M.tpl_nseg[t] = 0;
        if (m.lines.n && map_bounds(&m, &x0, &y0, &x1, &y1)) {
            double span = SF_MAX(x1 - x0, y1 - y0), cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            M.tpl_seg[t] = malloc(sizeof(float) * 4 * (size_t)m.lines.n);
            for (int i = 0; i < m.lines.n; i++) {
                const MVert *a = &m.verts.a[m.lines.a[i].v1], *b = &m.verts.a[m.lines.a[i].v2];
                M.tpl_seg[t][i * 4] = (float)((a->x - cx) / span + 0.5); M.tpl_seg[t][i * 4 + 1] = (float)(0.5 - (a->y - cy) / span);
                M.tpl_seg[t][i * 4 + 2] = (float)((b->x - cx) / span + 0.5); M.tpl_seg[t][i * 4 + 3] = (float)(0.5 - (b->y - cy) / span);
            }
            M.tpl_nseg[t] = m.lines.n;
        }
        map_free(&m);
    }
}

static const char *g_tpl_names[TPL_COUNT] = { "Empty map", "Starter Room", "Dungeon", "Arena" };
static const char *g_tpl_sub[TPL_COUNT] = { "Blank grid", "Lit room", "Rooms & door", "Pillars & pit" };
static const char *g_tpl_desc[TPL_COUNT] = {
    "Start from nothing and draw every sector yourself.",
    "One lit room with a player start. The quickest way in.",
    "Two rooms joined by a corridor and a working door.",
    "An octagonal arena with pillars, a lava pit and pickups." };

/* ================================================================== toast */
static void toast(float cx, float bottom, const Input *in) {
    if (!ed.toast[0] || in->time - ed.toast_time > 2.6) return;
    float a = (float)SF_CLAMP((2.6 - (in->time - ed.toast_time)) * 4, 0, 1);
    float w = SF_MIN(gfx_text_w(FONT_UI, ed.toast) + 56, in->win_w - 40);
    Rect r = rect(cx - w * 0.5f, bottom - 48, w, 44);
    gfx_shadow(r, 22, 12, col(0, 0, 0, (int)(120 * a)));
    gfx_rrect(r, 22, col(40, 43, 50, (int)(245 * a)));
    int ic = ed.toast_kind == 1 ? IC_check : ed.toast_kind == 2 ? IC_warn : IC_info;
    Col icc = ed.toast_kind == 1 ? C(TH_GREEN) : ed.toast_kind == 2 ? C(TH_RED) : C(TH_BLUE);
    icc.a = (uint8_t)(255 * a);
    gfx_icon(ic, r.x + 24, r.y + 22, 18, icc);
    gfx_text_fit(FONT_UI, r.x + 42, r.y + 22 - gfx_font_h(FONT_UI) * 0.5f, r.w - 54, ed.toast, col(235, 237, 241, (int)(255 * a)));
}

/* ================================================================== splash */
static void logo_tile(Rect r) {
    gfx_rrect(r, r.w * 0.24f, C(0x1D140D));
    gfx_rrect_line(r, r.w * 0.24f, 1, C(TH_ACCENT_BD));
    gfx_icon(IC_logo, r.x + r.w * 0.5f, r.y + r.h * 0.5f, r.w * 0.62f, C(MS_ACC));
}

static void splash_backdrop(Rect r, double t) {
    gfx_rect(r, C(0x0B0C0F));
    for (float x = r.x; x < r.x + r.w; x += 24) gfx_rect(rect(x, r.y, 1, r.h), C(0x14171C));
    for (float y = r.y; y < r.y + r.h; y += 24) gfx_rect(rect(r.x, y, r.w, 1), C(0x14171C));
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.36f, rr = SF_MIN(r.w, r.h) * 0.3f;
    float pulse = 0.5f + 0.5f * (float)sin(t * 2.2);
    for (int k = 0; k < 8; k++) {
        float a0 = (float)((22.5 + 45 * k) * SF_DEG2RAD), a1 = (float)((22.5 + 45 * (k + 1)) * SF_DEG2RAD);
        gfx_line(cx + cosf(a0) * rr, cy + sinf(a0) * rr, cx + cosf(a1) * rr, cy + sinf(a1) * rr, 1.5f, col(255, 122, 26, (int)(60 + 60 * pulse)));
        gfx_line(cx + cosf(a0) * rr * 0.62f, cy + sinf(a0) * rr * 0.62f, cx + cosf(a1) * rr * 0.62f, cy + sinf(a1) * rr * 0.62f, 1.2f, col(60, 66, 78, 200));
    }
}

static void splash(const Input *in) {
    double now = plat_time();
    if (!g_assets.ready) {
        double start = now;
        do { M.progress = assets_load_step(&M.asset_stage, M.status, sizeof M.status); } while (!g_assets.ready && plat_time() - start < 0.014);
        if (g_assets.ready) { make_tpl_thumbs(); sf_strcpy(M.status, sizeof M.status, "Ready"); }
    }
    M.shown += (M.progress - M.shown) * SF_MIN(1.0f, (float)in->dt * 10);
    Rect r = rect(0, 0, in->win_w, in->win_h);
    splash_backdrop(r, now);
    float cx = in->win_w * 0.5f, cy = in->win_h * 0.36f;
    if (g_assets.ready || M.asset_stage > 0) logo_tile(rect(cx - 44, cy - 44, 88, 88));
    float ty = cy + 90;
    const char *a = "SECTOR", *b = "FORGE";
    gfx_text(FONT_TITLE, cx - gfx_text_w(FONT_TITLE, a) * 0.5f, ty, a, C(0xF1F3F6));
    gfx_text(FONT_TITLE, cx - gfx_text_w(FONT_TITLE, b) * 0.5f, ty + gfx_font_h(FONT_TITLE) * 0.92f, b, C(MS_ACC));
    const char *sub = "Sector map editor & game engine";
    gfx_text(FONT_UI, cx - gfx_text_w(FONT_UI, sub) * 0.5f, ty + gfx_font_h(FONT_TITLE) * 2.0f, sub, C(MS_ON2));
    float by = in->win_h - in->inset_b - 64;
    float bw = SF_MIN(in->win_w - 80, 320);
    gfx_text(FONT_SMALL, cx - bw * 0.5f, by - 26, M.status, C(MS_ON2));
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(M.shown * 100 + 0.5f));
    gfx_text_right(FONT_MONO_SMALL, cx + bw * 0.5f, by - 26, pct, C(TH_ACCENT_HI));
    gfx_rrect(rect(cx - bw * 0.5f, by, bw, 4), 2, C(MS_SURF3));
    gfx_rrect(rect(cx - bw * 0.5f, by, bw * SF_CLAMP(M.shown, 0, 1), 4), 2, C(MS_ACC));
    const char *ver = "v" SF_VERSION " \xC2\xB7 C \xC2\xB7 OpenGL ES";
    gfx_text(FONT_MONO_SMALL, cx - gfx_text_w(FONT_MONO_SMALL, ver) * 0.5f, by + 18, ver, C(MS_ON3));
    if (g_assets.ready && now - M.state_time > 1.0 && M.shown > 0.98f) {
        projects_scan(&M.projects);
        set_state(ST_HOME);
    }
}

/* ================================================================== home */
static void start_open(const char *file, const char *name) {
    sf_strcpy(M.load_file, sizeof M.load_file, file);
    sf_strcpy(M.load_name, sizeof M.load_name, name);
    M.load_step = 0;
    M.progress = M.shown = 0;
    plat_set_orientation(ORIENT_LANDSCAPE);
    set_state(ST_LOADING);
}

static void open_project_idx(int i) {
    if (i < 0 || i >= M.projects.n) return;
    ProjectInfo *p = &M.projects.a[i];
    if (p->missing) { message("Project not found", "The project file could not be read. It may have been moved or deleted."); return; }
    start_open(p->file, p->name);
}

static void create_project(const char *name, int tpl) {
    Project p;
    char err[256];
    if (!project_create(&p, g_settings.projects_dir, name, tpl, err, sizeof err)) { message("Could not create the project", err); return; }
    projects_remember(p.file);
    start_open(p.file, p.name);
}

static void ask_new_project(int tpl) {
    M.new_tpl = tpl;
    char def[64];
    int n = 1;
    for (;;) {
        snprintf(def, sizeof def, n == 1 ? "My Map Project" : "My Map Project %d", n);
        bool used = false;
        for (int i = 0; i < M.projects.n; i++) if (!sf_stricmp(M.projects.a[i].name, def)) used = true;
        if (!used || n > 50) break;
        n++;
    }
    text_request(TXT_PROJECT_NAME, "Name your project", def, TEXTIN_NAME);
}

static float home_header(const Input *in, float y, const char *title, bool tools) {
    float W = in->win_w, x = 20;
    if (!tools) {
        gfx_text(FONT_HEAD, x, y + 22 - gfx_font_h(FONT_HEAD) * 0.5f, title, C(MS_ON));
        return y + 56;
    }
    logo_tile(rect(x, y, 44, 44));
    gfx_text(FONT_HEAD, x + 58, y + 22 - gfx_font_h(FONT_HEAD) * 0.5f, title, C(MS_ON));
    if (icon_button(ID("h_search"), rect(W - 104, y, 44, 44), IC_search, M.proj_filter[0] != 0, 22))
        text_request(TXT_PROJ_SEARCH, "Search projects", M.proj_filter, TEXTIN_NAME);
    if (icon_button(ID("h_help"), rect(W - 56, y, 44, 44), IC_book, false, 22)) { M.guide_tab = 0; open_bsheet(BS_GUIDE); }
    region(rect(W - 104, y, 92, 44));
    return y + 62;
}

static void home_projects(const Input *in, Rect area) {
    float W = in->win_w;
    float y0 = area.y;
    scroll_area(area, &M.home_scroll, 0, false);
    float sc = M.home_scroll;
    gfx_clip_push(area);
    float y = y0 - sc;
    y = home_header(in, y + 8, "Sector Forge", true);
    if (M.proj_filter[0]) {
        char t[96];
        snprintf(t, sizeof t, "Filter: \"%s\"", M.proj_filter);
        float cw = chip_w(IC_close, t, false);
        if (chip(ID("h_clear"), rect(20, y - 6, cw, 36), IC_close, t, true, false)) M.proj_filter[0] = 0;
        y += 40;
    }
    section_label(20, y, "START FROM A TEMPLATE");
    y += 24;
    /* template carousel */
    Rect car = rect(0, y, W, 136);
    float cw = 150, gap = 12;
    float content = 20 + TPL_COUNT * (cw + gap) + 8;
    ui_push_block(car, ui.layer);
    {
        float s = M.tpl_scroll;
        float maxs = SF_MAX(0, content - W);
        if (s < 0) M.tpl_scroll += (0 - s) * 0.35f;
        if (s > maxs) M.tpl_scroll += (maxs - s) * 0.35f;
        if (M.nsreg_next < 16 && car.y + car.h > area.y && car.y < area.y + area.h) {
            ScrollReg *r = &M.sreg_next[M.nsreg_next++];
            r->r = car; r->scroll = &M.tpl_scroll; r->horiz = true; r->content = content;
        }
    }
    for (int t = 0; t < TPL_COUNT; t++) {
        Rect c = rect(20 + t * (cw + gap) - M.tpl_scroll, y, cw, 128);
        bool held;
        if (tap(IDI("tplc", t), c, &held)) ask_new_project(t);
        gfx_rrect(c, 16, C(MS_SURF));
        gfx_rrect_line(c, 16, 1, C(MS_OUTLINE));
        Rect th = rect(c.x + 1, c.y + 1, c.w - 2, 74);
        map_thumb(th, M.tpl_seg[t], M.tpl_nseg[t], false, 15);
        gfx_text(FONT_UI_BOLD, c.x + 14, c.y + 84, g_tpl_names[t], C(MS_ON));
        gfx_text(FONT_SMALL, c.x + 14, c.y + 104, g_tpl_sub[t], C(MS_ON2));
        ripple(c, 16, held);
    }
    y += 150;
    /* recent projects */
    section_label(20, y, "RECENT PROJECTS");
    y += 26;
    int shown = 0, last = -1;
    int64_t best = -1;
    for (int i = 0; i < M.projects.n; i++) if (!M.projects.a[i].missing && M.projects.a[i].mtime > best) { best = M.projects.a[i].mtime; last = i; }
    float gw = (W - 40 - 14) * 0.5f, gh = 196;
    for (int i = 0; i < M.projects.n; i++) {
        ProjectInfo *p = &M.projects.a[i];
        if (M.proj_filter[0] && !strstr(p->name, M.proj_filter)) {
            char a[64], b[64];
            sf_strcpy(a, sizeof a, p->name); sf_strcpy(b, sizeof b, M.proj_filter);
            for (char *q = a; *q; q++) *q = (char)tolower(*q);
            for (char *q = b; *q; q++) *q = (char)tolower(*q);
            if (!strstr(a, b)) continue;
        }
        Rect c = rect(20 + (shown % 2) * (gw + 14), y + (shown / 2) * (gh + 14), gw, gh);
        shown++;
        if (c.y > area.y + area.h || c.y + c.h < area.y) continue;
        bool held;
        if (tap(IDI("pc", i), c, &held)) open_project_idx(i);
        if (long_pressed(c)) { M.proj_sel = i; open_bsheet(BS_PROJECT); }
        bool lst = i == last;
        gfx_rrect(c, 18, C(MS_SURF));
        gfx_rrect_line(c, 18, lst ? 2 : 1, lst ? C(MS_ACC) : C(MS_OUTLINE));
        Rect th = rect(c.x + 2, c.y + 2, c.w - 4, 104);
        map_thumb(th, p->seg, p->nseg, lst, 16);
        float tx = c.x + 16, ty = c.y + 116;
        float bw = lst ? gfx_text_w(FONT_SMALL, "LAST") + 16 : 0;
        gfx_text_fit(FONT_UI_BOLD, tx, ty, c.w - 32 - bw, p->name, p->missing ? C(MS_ON3) : C(MS_ON));
        if (lst) {
            Rect b = rect(c.x + c.w - 14 - bw, ty - 1, bw, 22);
            gfx_rrect(b, 6, C(MS_ACC));
            gfx_text_center(FONT_SMALL, b, "LAST", C(0x1A0D02));
        }
        char l[64];
        if (p->missing) snprintf(l, sizeof l, "Missing");
        else snprintf(l, sizeof l, "%d map%s \xC2\xB7 %d texture%s", p->nmaps, p->nmaps == 1 ? "" : "s", p->ntex, p->ntex == 1 ? "" : "s");
        gfx_text_fit(FONT_SMALL, tx, ty + 26, c.w - 32, l, C(MS_ON2));
        char age[32];
        fmt_age(p->mtime, age, sizeof age);
        gfx_icon(IC_clock, tx + 7, ty + 58, 14, C(MS_ON3));
        gfx_text(FONT_SMALL, tx + 20, ty + 50, age, C(MS_ON3));
        ripple(c, 18, held);
    }
    if (!shown) {
        Rect e = rect(20, y, W - 40, 150);
        gfx_rrect(e, 18, C(MS_SURF));
        gfx_rrect_line(e, 18, 1, C(MS_OUTLINE));
        gfx_icon(IC_open, e.x + e.w * 0.5f, e.y + 42, 34, C(MS_ON3));
        gfx_text_center(FONT_UI_BOLD, rect(e.x, e.y + 72, e.w, 24), M.proj_filter[0] ? "No matching projects" : "No projects yet", C(MS_ON));
        gfx_text_center(FONT_SMALL, rect(e.x, e.y + 98, e.w, 20), "Pick a template above or tap New project", C(MS_ON2));
        shown = 2;
    }
    y += ((shown + 1) / 2) * (gh + 14) + 90;
    gfx_clip_pop();
    /* content height for the scroll area (recorded next frame) */
    if (M.nsreg_next) {
        for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == &M.home_scroll) M.sreg_next[s].content = y + sc - y0;
    }
    float maxs = SF_MAX(0, y + sc - y0 - area.h);
    if (M.home_scroll > maxs) M.home_scroll += (maxs - M.home_scroll) * 0.35f;
    /* FAB */
    const char *fl = "New project";
    float fw = gfx_text_w(FONT_UI_BOLD, fl) + 76;
    Rect fab = rect(W - 20 - fw, area.y + area.h - 76, fw, 60);
    region(fab);
    gfx_shadow(fab, 18, 14, col(0, 0, 0, 150));
    if (mbutton(ID("fab"), fab, IC_plus, fl, MB_FILLED)) ask_new_project(TPL_STARTER);
}

static void home_templates(const Input *in, Rect area) {
    float W = in->win_w;
    scroll_area(area, &M.tpls_scroll, 0, false);
    gfx_clip_push(area);
    float y = area.y + 8 - M.tpls_scroll;
    y = home_header(in, y, "Templates", false);
    gfx_text(FONT_UI, 20, y - 8, "Every new project starts from one of these.", C(MS_ON2));
    y += 24;
    for (int t = 0; t < TPL_COUNT; t++) {
        Rect c = rect(20, y, W - 40, 248);
        gfx_rrect(c, 18, C(MS_SURF));
        gfx_rrect_line(c, 18, 1, C(MS_OUTLINE));
        map_thumb(rect(c.x + 2, c.y + 2, c.w - 4, 124), M.tpl_seg[t], M.tpl_nseg[t], false, 16);
        gfx_text(FONT_UI_BOLD, c.x + 16, c.y + 138, g_tpl_names[t], C(MS_ON));
        gfx_text_fit(FONT_SMALL, c.x + 16, c.y + 162, c.w - 32, g_tpl_desc[t], C(MS_ON2));
        if (mbutton(IDI("tplb", t), rect(c.x + 16, c.y + c.h - 56, c.w - 32, 44), IC_plus, "Create project", t == TPL_STARTER ? MB_FILLED : MB_TONAL))
            ask_new_project(t);
        y += c.h + 14;
    }
    for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == &M.tpls_scroll) M.sreg_next[s].content = y + M.tpls_scroll - area.y + 20;
    gfx_clip_pop();
}

typedef struct { int icon; const char *title, *body; } GuideItem;
static const GuideItem g_guide[3][9] = {
    { { IC_select, "Tap", "Select \xC2\xB7 place a point \xC2\xB7 place a thing" },
      { IC_move, "Drag", "Move the selection \xC2\xB7 drag empty space to box-select" },
      { IC_hand, "Two-finger drag", "Pan the map" },
      { IC_zoom_in, "Pinch", "Zoom the map around your fingers" },
      { IC_menu, "Long-press", "Actions: copy, delete, join, mirror, rotate\xE2\x80\xA6" },
      { IC_check, "Double-tap", "Close the shape you are drawing" },
      { IC_crosshair, "Loupe", "A magnifier above your finger shows the exact snap point" },
      { IC_undo, "Three-finger tap", "Undo" },
      { IC_grid, "Two-finger double-tap", "Next grid size" } },
    { { IC_eye, "Drag", "Look around" },
      { IC_select, "Tap", "Select a floor, ceiling, wall or thing" },
      { IC_height, "Height dial", "Swipe to raise or lower the selected floors and ceilings" },
      { IC_move, "Joystick", "Fly \xC2\xB7 the arrow buttons move up and down" },
      { IC_zoom_in, "Pinch", "Fly forward and back \xC2\xB7 two fingers strafe" },
      { IC_bucket, "Paint", "Tap surfaces to paint them with the current texture" },
      { IC_eyedropper, "Pick", "Tap a surface to copy its texture" },
      { IC_texture, "Recent textures", "One tap applies a texture to the selection" },
      { IC_undo, "Three-finger tap", "Undo" } },
    { { IC_move, "Left thumb", "Move \xC2\xB7 the stick appears where you touch" },
      { IC_eye, "Right side", "Drag to look" },
      { IC_crosshair, "Fire", "Hold to keep firing" },
      { IC_door, "Use", "Open doors and ride lifts" },
      { IC_chev_up, "Jump", "Hop onto ledges" },
      { IC_chev_down, "Crouch", "Hold to duck under low ceilings" },
      { IC_stop, "Pause", "Resume, restart or go back to the editor" },
      { IC_keyboard, "Keyboard & mouse", "Desktop shortcuts work with DeX or Bluetooth" },
      { IC_play, "Gamepad", "Sticks move and look \xC2\xB7 A jump \xC2\xB7 X use \xC2\xB7 RT fire" } },
};

static float guide_cards(Rect area, int tab, int cols, float y) {
    float gw = (area.w - (cols - 1) * 12) / cols, gh = 84;
    for (int i = 0; i < 9; i++) {
        const GuideItem *g = &g_guide[tab][i];
        Rect c = rect(area.x + (i % cols) * (gw + 12), y + (i / cols) * (gh + 12), gw, gh);
        gfx_rrect(c, 16, C(MS_SURF));
        gfx_rrect_line(c, 16, 1, C(MS_OUTLINE));
        Rect ic = rect(c.x + 14, c.y + 16, 44, 44);
        gfx_rrect(ic, 12, C(MS_ACC_CT));
        gfx_icon(g->icon, ic.x + 22, ic.y + 22, 22, C(MS_ACC_ON));
        gfx_text_fit(FONT_UI_BOLD, c.x + 72, c.y + 16, c.w - 84, g->title, C(MS_ON));
        /* two-line wrap */
        const char *b = g->body;
        float maxw = c.w - 84;
        int cut = (int)strlen(b);
        if (gfx_text_w(FONT_SMALL, b) > maxw) {
            int best = 0;
            for (int k = 0; b[k]; k++) if (b[k] == ' ' && gfx_text_wn(FONT_SMALL, b, k) <= maxw) best = k;
            cut = best ? best : cut;
        }
        gfx_text_n(FONT_SMALL, c.x + 72, c.y + 40, b, cut, C(MS_ON2));
        if (b[cut]) gfx_text_fit(FONT_SMALL, c.x + 72, c.y + 58, maxw, b + cut + 1, C(MS_ON2));
    }
    return y + ((9 + cols - 1) / cols) * (gh + 12);
}

static void seg_tabs(uint32_t id, Rect r, const char **labels, int n, int *sel) {
    gfx_rrect(r, r.h * 0.5f, C(MS_SURF));
    gfx_rrect_line(r, r.h * 0.5f, 1, C(MS_OUTLINE));
    float w = r.w / n;
    for (int i = 0; i < n; i++) {
        Rect b = rect(r.x + i * w, r.y, w, r.h);
        bool held;
        if (tap(id + (uint32_t)i * 7919u, b, &held)) *sel = i;
        if (*sel == i) gfx_rrect(rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4), (r.h - 4) * 0.5f, C(MS_ACC_CT));
        ripple(b, r.h * 0.5f, held);
        gfx_text_center(FONT_UI_BOLD, b, labels[i], *sel == i ? C(MS_ACC_ON) : C(MS_ON2));
    }
}

static void home_learn(const Input *in, Rect area) {
    float W = in->win_w;
    scroll_area(area, &M.learn_scroll, 0, false);
    gfx_clip_push(area);
    float y = area.y + 8 - M.learn_scroll;
    y = home_header(in, y, "Touch controls", false);
    static const char *tabs[3] = { "Editor", "3D", "Play" };
    seg_tabs(ID("learn_tabs"), rect(20, y - 6, W - 40, 44), tabs, 3, &M.guide_tab);
    y += 54;
    y = guide_cards(rect(20, 0, W - 40, 0), M.guide_tab, 1, y);
    for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == &M.learn_scroll) M.sreg_next[s].content = y + M.learn_scroll - area.y + 20;
    gfx_clip_pop();
}

static void home_settings(const Input *in, Rect area) {
    float W = in->win_w;
    scroll_area(area, &M.set_scroll, 0, false);
    gfx_clip_push(area);
    float y = area.y + 8 - M.set_scroll, x = 20, w = W - 40;
    y = home_header(in, y, "Settings", false);
    bool ch = false;
    section_label(x, y, "PLAY & 3D VIEW");
    y += 26;
    Rect c = rect(x, y, w, 300);
    gfx_rrect(c, 18, C(MS_SURF));
    float cy = y + 14;
    char t[32];
    gfx_text(FONT_UI, x + 16, cy, "Look sensitivity", C(MS_ON));
    snprintf(t, sizeof t, "%.2f", g_settings.mouse_sens);
    gfx_text_right(FONT_MONO, x + w - 16, cy, t, C(MS_ACC_ON));
    if (slider(ID("s_sens"), rect(x + 4, cy + 24, w - 8, 36), &g_settings.mouse_sens, 0.2f, 3.0f)) ch = true;
    cy += 72;
    gfx_text(FONT_UI, x + 16, cy, "Field of view", C(MS_ON));
    snprintf(t, sizeof t, "%.0f\xC2\xB0", g_settings.fov);
    gfx_text_right(FONT_MONO, x + w - 16, cy, t, C(MS_ACC_ON));
    if (slider(ID("s_fov"), rect(x + 4, cy + 24, w - 8, 36), &g_settings.fov, 60, 120)) { g_settings.fov = roundf(g_settings.fov); ch = true; }
    cy += 72;
    if (toggle_switch(ID("s_inv"), rect(x + 12, cy, w - 28, 44), "Invert look", &g_settings.invert_y)) ch = true;
    cy += 50;
    if (toggle_switch(ID("s_fps"), rect(x + 12, cy, w - 28, 44), "Show frame rate", &g_settings.show_fps)) ch = true;
    cy += 50;
    c.h = cy - y + 4;
    y = cy + 24;
    section_label(x, y, "EDITOR");
    y += 26;
    c = rect(x, y, w, 0);
    float c0 = y;
    cy = y + 8;
    gfx_rrect(rect(x, y, w, 5 * 50 + 90), 18, C(MS_SURF));
    if (toggle_switch(ID("s_auto"), rect(x + 12, cy, w - 28, 44), "Autosave", &g_settings.autosave)) ch = true;
    cy += 50;
    if (toggle_switch(ID("s_tex"), rect(x + 12, cy, w - 28, 44), "Textured floors in 2D", &g_settings.textured_2d)) { ch = true; ed.textured_2d = g_settings.textured_2d; }
    cy += 50;
    if (toggle_switch(ID("s_hap"), rect(x + 12, cy, w - 28, 44), "Haptic feedback", &M.haptics)) ch = true;
    cy += 56;
    gfx_text(FONT_UI, x + 16, cy, "Default grid", C(MS_ON));
    cy += 28;
    static const int gs[] = { 8, 16, 32, 64, 128 };
    float bw = (w - 32 - 4 * 8) / 5;
    for (int i = 0; i < 5; i++) {
        char l[8];
        snprintf(l, sizeof l, "%d", gs[i]);
        Rect b = rect(x + 16 + i * (bw + 8), cy, bw, 44);
        bool on = g_settings.default_grid == gs[i];
        bool held;
        if (tap(IDI("s_g", i), b, &held)) { g_settings.default_grid = gs[i]; ch = true; }
        gfx_rrect(b, 12, on ? C(MS_ACC_CT) : C(MS_SURF2));
        gfx_rrect_line(b, 12, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        gfx_text_center(FONT_UI_BOLD, b, l, on ? C(MS_ACC_ON) : C(MS_ON));
        ripple(b, 12, held);
    }
    cy += 64;
    (void)c0;
    y = cy + 16;
    section_label(x, y, "STORAGE");
    y += 26;
    gfx_rrect(rect(x, y, w, 92), 18, C(MS_SURF));
    gfx_text(FONT_UI, x + 16, y + 14, "Projects folder", C(MS_ON));
    gfx_text_fit(FONT_MONO_SMALL, x + 16, y + 40, w - 32, g_settings.projects_dir, C(MS_ON2));
    gfx_text_fit(FONT_SMALL, x + 16, y + 62, w - 32, "Reachable over USB under Android/data", C(MS_ON3));
    y += 110;
    section_label(x, y, "ABOUT");
    y += 26;
    gfx_rrect(rect(x, y, w, 96), 18, C(MS_SURF));
    logo_tile(rect(x + 16, y + 16, 64, 64));
    gfx_text(FONT_UI_BOLD, x + 96, y + 22, "Sector Forge " SF_VERSION, C(MS_ON));
    gfx_text_fit(FONT_SMALL, x + 96, y + 46, w - 112, "Sector map editor & game engine in C", C(MS_ON2));
    gfx_text_fit(FONT_SMALL, x + 96, y + 66, w - 112, "Fonts: Inter, DejaVu Sans Mono (OFL)", C(MS_ON3));
    y += 120;
    if (ch) settings_save();
    for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == &M.set_scroll) M.sreg_next[s].content = y + M.set_scroll - area.y;
    gfx_clip_pop();
}

static void bottom_nav(const Input *in) {
    float W = in->win_w, H = in->win_h, h = 76 + in->inset_b;
    Rect r = rect(0, H - h, W, h);
    region(r);
    gfx_rect(r, C(0x15171B));
    gfx_rect(rect(0, r.y, W, 1), C(MS_OUTLINE));
    static const char *labels[4] = { "Projects", "Templates", "Learn", "Settings" };
    static const int icons[4] = { IC_open, IC_template, IC_book, IC_settings };
    float w = W / 4;
    for (int i = 0; i < 4; i++) {
        Rect b = rect(i * w, r.y, w, 76);
        bool held;
        if (tap(IDI("nav", i), b, &held)) M.home_tab = i;
        bool on = M.home_tab == i;
        Rect pill = rect(b.x + w * 0.5f - 32, b.y + 10, 64, 32);
        if (on) gfx_rrect(pill, 16, C(0x4A2A12));
        else if (held) gfx_rrect(pill, 16, col(255, 255, 255, 18));
        gfx_icon(icons[i], pill.x + 32, pill.y + 16, 22, on ? C(0xFFD2B0) : C(MS_ON2));
        gfx_text_center(on ? FONT_UI_BOLD : FONT_SMALL, rect(b.x, b.y + 46, w, 20), labels[i], on ? C(MS_ON) : C(MS_ON2));
    }
}

static void home(const Input *in) {
    float H = in->win_h;
    gfx_rect(rect(0, 0, in->win_w, H), C(MS_BG));
    Rect area = rect(0, in->inset_t, in->win_w, H - in->inset_t - 76 - in->inset_b);
    switch (M.home_tab) {
    case HOME_TEMPLATES: home_templates(in, area); break;
    case HOME_LEARN: home_learn(in, area); break;
    case HOME_SETTINGS: home_settings(in, area); break;
    default: home_projects(in, area); break;
    }
    /* status bar scrim */
    gfx_rect(rect(0, 0, in->win_w, in->inset_t), C(MS_BG));
    bottom_nav(in);
    toast(in->win_w * 0.5f, H - 76 - in->inset_b - 84, in);
}

/* ================================================================== loading */
static void loading(const Input *in) {
    Rect full = rect(0, 0, in->win_w, in->win_h);
    splash_backdrop(full, plat_time());
    double el = plat_time() - M.state_time;
    /* wait for the rotation to landscape before doing the heavy work */
    bool landscape = in->win_w > in->win_h;
    if (M.load_step == 0 && el > 0.1 && (landscape || el > 1.2)) {
        sf_strcpy(M.status, sizeof M.status, "Loading textures and maps");
        M.load_step = 1;
    } else if (M.load_step == 1 && el > 0.25) {
        char err[256];
        if (!ed_open_project(M.load_file, err, sizeof err)) {
            plat_set_orientation(ORIENT_PORTRAIT);
            set_state(ST_HOME);
            message("Could not open project", err);
            return;
        }
        ed.textured_2d = g_settings.textured_2d;
        sf_strcpy(M.status, sizeof M.status, "Triangulating sectors");
        M.load_step = 2;
    } else if (M.load_step == 2 && el > 0.45) {
        for (int s = 0; s < ed.map.sectors.n; s++) { int n; r3d_sector_tris(&ed.map, s, &n); }
        sf_strcpy(M.status, sizeof M.status, "Starting editor");
        M.load_step = 3;
    } else if (M.load_step == 3 && el > 0.75) {
        set_state(ST_EDITOR);
        M.last_autosave = plat_time();
        M.sheet_open = false;
        M.nrecent = 0;
        push_recent(ed.cur_wall);
        push_recent(ed.cur_flat);
        for (int s = 0; s < ed.map.sectors.n && M.nrecent < 6; s++) { push_recent(ed.map.sectors.a[s].floor_tex); push_recent(ed.map.sectors.a[s].ceil_tex); }
        if (M.first_run_guide) { M.first_run_guide = false; M.guide_tab = 0; open_bsheet(BS_GUIDE); }
        return;
    }
    M.progress = M.load_step / 3.0f;
    M.shown += (M.progress - M.shown) * SF_MIN(1.0f, (float)in->dt * 8);
    float cw = SF_MIN(in->win_w - 48, 460), ch = 200;
    Rect card = rect((in->win_w - cw) * 0.5f, (in->win_h - ch) * 0.5f, cw, ch);
    gfx_shadow(card, 24, 24, col(0, 0, 0, 170));
    gfx_rrect(card, 24, col(20, 22, 26, 248));
    gfx_rrect_line(card, 24, 1, C(MS_OUTLINE));
    logo_tile(rect(card.x + 28, card.y + 28, 56, 56));
    gfx_text(FONT_SMALL, card.x + 100, card.y + 32, "OPENING PROJECT", C(MS_ON3));
    gfx_text_fit(FONT_HEAD, card.x + 100, card.y + 50, card.w - 128, M.load_name, C(MS_ON));
    float by = card.y + card.h - 62;
    gfx_text(FONT_UI, card.x + 28, by, M.status, C(MS_ON));
    char pct[16];
    snprintf(pct, sizeof pct, "%d%%", (int)(M.shown * 100 + 0.5f));
    gfx_text_right(FONT_MONO, card.x + card.w - 28, by, pct, C(TH_ACCENT_HI));
    gfx_rrect(rect(card.x + 28, by + 28, card.w - 56, 5), 2.5f, C(MS_SURF3));
    gfx_rrect(rect(card.x + 28, by + 28, (card.w - 56) * M.shown, 5), 2.5f, C(MS_ACC));
}

/* ================================================================== editor: selection model shared by 2D and 3D */
typedef struct { IntArr sec, lin, thg, vtx; int side_hint; } SelSet;

static void sel_gather(SelSet *s) {
    arr_clear(s->sec); arr_clear(s->lin); arr_clear(s->thg); arr_clear(s->vtx);
    s->side_hint = 0;
    Map *m = &ed.map;
    if (ed.mode == MODE_3D) {
        for (int i = 0; i < ed.sel3d.n; i++) {
            int k = SURF_KIND(ed.sel3d.a[i]), idx = SURF_IDX(ed.sel3d.a[i]);
            if ((k == SURF_FLOOR || k == SURF_CEIL) && idx < m->sectors.n) { bool dup = false; for (int j = 0; j < s->sec.n; j++) if (s->sec.a[j] == idx) dup = true; if (!dup) arr_push(s->sec, idx); }
            else if (k >= SURF_UPPER && k <= SURF_LOWER && idx / 2 < m->lines.n) { arr_push(s->lin, idx / 2); s->side_hint = idx & 1; }
            else if (k == SURF_THING && idx < m->things.n) arr_push(s->thg, idx);
        }
        return;
    }
    switch (ed.selmode) {
    case SM_SECTORS: for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) arr_push(s->sec, i); break;
    case SM_LINES: for (int i = 0; i < m->lines.n; i++) if (m->lines.a[i].sel) arr_push(s->lin, i); break;
    case SM_THINGS: for (int i = 0; i < m->things.n; i++) if (m->things.a[i].sel) arr_push(s->thg, i); break;
    default: for (int i = 0; i < m->verts.n; i++) if (m->verts.a[i].sel) arr_push(s->vtx, i); break;
    }
}
static SelSet g_sel;
static int sel_total(void) { return g_sel.sec.n + g_sel.lin.n + g_sel.thg.n + g_sel.vtx.n; }

static int thing_icon(int type) {
    switch (type) {
    case TH_PLAYER: return IC_player;
    case TH_IMP: case TH_SOLDIER: return IC_enemy;
    case TH_HEALTH: case TH_ARMOR: case TH_AMMO: return IC_item;
    case TH_LIGHT: case TH_LAMP: return IC_light;
    default: return IC_thing;
    }
}

/* ================================================================== bottom sheets (modal) */
static Rect bsheet_begin(const Input *in, float h, int icon, const char *title, bool done_btn) {
    ui.layer = LAYER_MODAL;
    Rect full = rect(0, 0, in->win_w, in->win_h);
    region(full);
    float t = (float)SF_CLAMP((plat_time() - M.bsheet_t) / 0.18, 0, 1);
    float e = 1 - (1 - t) * (1 - t);
    gfx_rect(full, col(0, 0, 0, (int)(140 * e)));
    float w = SF_MIN(in->win_w - in->inset_l - in->inset_r, 720);
    h = SF_MIN(h, in->win_h - in->inset_t - 12);
    Rect r = rect((in->win_w - w) * 0.5f, in->win_h - h + (1 - e) * h, w, h + 30);
    /* tap on the scrim closes */
    bool held;
    if (tap(ID("bs_scrim"), rect(0, 0, in->win_w, r.y), &held)) close_bsheet();
    gfx_shadow(r, 28, 20, col(0, 0, 0, 160));
    gfx_rrect(r, 28, C(0x1B1D22));
    gfx_rrect(rect(r.x + r.w * 0.5f - 18, r.y + 10, 36, 4), 2, C(0x4A505B));
    float x = r.x + 24, y = r.y + 26;
    if (title) {
        if (icon >= 0) { gfx_icon(icon, x + 12, y + 16, 24, C(MS_ACC)); x += 36; }
        gfx_text(FONT_HEAD, x, y + 16 - gfx_font_h(FONT_HEAD) * 0.5f, title, C(MS_ON));
        if (done_btn) {
            float dw = gfx_text_w(FONT_UI_BOLD, "Done") + 32;
            if (mbutton(ID("bs_done"), rect(r.x + r.w - 20 - dw, y - 2, dw, 38), -1, "Done", MB_TEXT)) close_bsheet();
        }
        y += 50;
    }
    return rect(r.x + 24, y, r.w - 48, r.y + h - y - in->inset_b - 12);
}

static bool sheet_item(uint32_t id, Rect r, int icon, const char *label, const char *sub, bool danger) {
    bool held;
    bool c = tap(id, r, &held);
    ripple(r, 14, held);
    Col fg = danger ? C(0xFF8A7A) : C(MS_ON);
    gfx_icon(icon, r.x + 22, r.y + r.h * 0.5f, 22, danger ? fg : C(MS_ON2));
    if (sub) {
        gfx_text(FONT_UI, r.x + 52, r.y + r.h * 0.5f - 20, label, fg);
        gfx_text_fit(FONT_SMALL, r.x + 52, r.y + r.h * 0.5f + 2, r.w - 60, sub, C(MS_ON3));
    } else gfx_text(FONT_UI, r.x + 52, r.y + (r.h - gfx_font_h(FONT_UI)) * 0.5f, label, fg);
    return c;
}

/* grid of icon+label tiles; returns index tapped or -1 */
static int tile_grid(uint32_t id, Rect area, float *y, const int *icons, const char **labels, const bool *on, int n, int cols) {
    float gw = (area.w - (cols - 1) * 10) / cols, gh = 76;
    int res = -1;
    for (int i = 0; i < n; i++) {
        Rect b = rect(area.x + (i % cols) * (gw + 10), *y + (i / cols) * (gh + 10), gw, gh);
        bool held;
        if (tap(id + (uint32_t)i * 104729u, b, &held)) res = i;
        bool o = on && on[i];
        gfx_rrect(b, 16, o ? C(MS_ACC_CT) : C(MS_SURF2));
        gfx_rrect_line(b, 16, 1, o ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 16, held);
        gfx_icon(icons[i], b.x + b.w * 0.5f, b.y + 26, 24, o ? C(MS_ACC_ON) : C(MS_ON));
        gfx_text_center(FONT_SMALL, rect(b.x + 4, b.y + 46, b.w - 8, 20), labels[i], o ? C(MS_ACC_ON) : C(MS_ON2));
    }
    *y += ((n + cols - 1) / cols) * (gh + 10);
    return res;
}

static void leave_editor(void) {
    if (ed.mode == MODE_PLAY) play_stop();
    if (ed.dirty) ed_save();
    ed_close_project();
    ed.toast[0] = 0;
    close_bsheet();
    M.paused = false;
    projects_scan(&M.projects);
    plat_set_orientation(ORIENT_PORTRAIT);
    set_state(ST_HOME);
}

static void open_actions(float x, float y) { M.act_x = x; M.act_y = y; open_bsheet(BS_ACTIONS); }

static void bs_grid(const Input *in) {
    Rect a = bsheet_begin(in, 400, IC_grid, "Grid", true);
    bool wide = a.w > 560;
    float lw = wide ? a.w * 0.52f : a.w;
    float y = a.y;
    section_label(a.x, y, "Grid size (map units)");
    y += 24;
    static const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    float bw = (lw - 4 * 10) / 5;
    for (int i = 0; i < 10; i++) {
        Rect b = rect(a.x + (i % 5) * (bw + 10), y + (i / 5) * 56, bw, 48);
        char t[8];
        snprintf(t, sizeof t, "%d", sizes[i]);
        bool held, on = ed.grid == sizes[i];
        if (tap(IDI("g_sz", i), b, &held)) ed.grid = sizes[i];
        gfx_rrect(b, 12, on ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 12, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 12, held);
        gfx_text_center(FONT_UI_BOLD, b, t, on ? C(0xFFD2B0) : C(MS_ON));
    }
    y += 120;
    float hw = (lw - 12) * 0.5f;
    section_label(a.x, y, "Custom");
    section_label(a.x + hw + 12, y, "Major line every");
    y += 22;
    char v1[16], v2[16];
    snprintf(v1, sizeof v1, "%d", ed.grid);
    snprintf(v2, sizeof v2, "%d", ed.major);
    int r1 = stepper(ID("g_cust"), rect(a.x, y, hw, 56), v1);
    if (r1 == -1) ed.grid = SF_MAX(1, ed.grid - 1);
    if (r1 == 1) ed.grid = SF_MIN(4096, ed.grid + 1);
    if (r1 == 2) { M.txt_num = NUM_GRID; text_request(TXT_NUMBER, "Grid size", v1, TEXTIN_NUMBER); }
    int r2 = stepper(ID("g_maj"), rect(a.x + hw + 12, y, hw, 56), v2);
    if (r2 == -1) ed.major = SF_MAX(1, ed.major - 1);
    if (r2 == 1) ed.major = SF_MIN(64, ed.major + 1);
    float tx = wide ? a.x + lw + 32 : a.x, ty = wide ? a.y : y + 76, tw = wide ? a.w - lw - 32 : a.w;
    toggle_switch(ID("g_t1"), rect(tx, ty, tw, 48), "Show grid", &ed.show_grid); ty += 52;
    toggle_switch(ID("g_t2"), rect(tx, ty, tw, 48), "Snap to grid", &ed.snap); ty += 52;
    toggle_switch(ID("g_t3"), rect(tx, ty, tw, 48), "Snap to vertices & lines", &ed.snap_geo); ty += 52;
    toggle_switch(ID("g_t4"), rect(tx, ty, tw, 48), "Show line lengths", &ed.show_lengths); ty += 52;
    toggle_switch(ID("g_t5"), rect(tx, ty, tw, 48), "Sector heights", &ed.show_heights);
}

static void bs_more(const Input *in) {
    if (ed.mode == MODE_3D) {
        Rect a = bsheet_begin(in, 330, IC_cube, "3D tools", true);
        float y = a.y;
        static const int ic[] = { IC_eye, IC_light, IC_wireframe, IC_mode2d, IC_texture, IC_book };
        static const char *lb[] = { "Fly camera", "Fullbright", "Wireframe", "Camera from 2D", "Textures", "Controls" };
        bool on[6] = { ed.tool3d == T3_FLY, ed.fullbright, ed.wireframe, false, false, false };
        int r = tile_grid(ID("more3"), a, &y, ic, lb, on, 6, a.w > 560 ? 6 : 3);
        switch (r) {
        case 0: ed.tool3d = T3_FLY; close_bsheet(); break;
        case 1: ed.fullbright = !ed.fullbright; break;
        case 2: ed.wireframe = !ed.wireframe; break;
        case 3: ed.cam_valid = false; ed3d_sync_camera_from_2d(); close_bsheet(); break;
        case 4: M.sheet_open = true; M.sheet_tab = SHEET_TEX; close_bsheet(); break;
        case 5: M.guide_tab = 1; open_bsheet(BS_GUIDE); break;
        default: break;
        }
        return;
    }
    Rect a = bsheet_begin(in, 420, IC_menu, "Tools & actions", true);
    float y = a.y;
    int cols = a.w > 560 ? 7 : 4;
    section_label(a.x, y, "MORE TOOLS");
    y += 22;
    static const int ti[] = { T_VERTEX, T_SPLIT, T_MOVE, T_ROTATE, T_SCALE, T_MEASURE, T_PAN };
    static const int tic[] = { IC_vertex, IC_split, IC_move, IC_rotate, IC_scale, IC_measure, IC_hand };
    static const char *tl[] = { "Vertex", "Split", "Move", "Rotate", "Scale", "Measure", "Pan" };
    bool ton[7];
    for (int i = 0; i < 7; i++) ton[i] = ed.tool == ti[i];
    int r = tile_grid(ID("moret"), a, &y, tic, tl, ton, 7, cols);
    if (r >= 0) { ed_set_tool(ti[r]); close_bsheet(); }
    y += 6;
    section_label(a.x, y, "SELECTION & VIEW");
    y += 22;
    static const int aic[] = { IC_select, IC_close, IC_layers, IC_fit, IC_texture, IC_thing, IC_info };
    static const char *al[] = { "Select all", "Select none", "Invert", "Fit view", "Textured", "Things", "Heights" };
    bool aon[7] = { false, false, false, false, ed.textured_2d, ed.show_things, ed.show_heights };
    r = tile_grid(ID("morea"), a, &y, aic, al, aon, 7, cols);
    switch (r) {
    case 0: ed_select_all(); close_bsheet(); break;
    case 1: ed_select_none(); close_bsheet(); break;
    case 2: ed_invert_selection(); close_bsheet(); break;
    case 3: ed_fit_view(); close_bsheet(); break;
    case 4: ed.textured_2d = !ed.textured_2d; break;
    case 5: ed.show_things = !ed.show_things; break;
    case 6: ed.show_heights = !ed.show_heights; break;
    default: break;
    }
}

static void bs_actions(const Input *in) {
    Rect a = bsheet_begin(in, 330, IC_menu, "Actions", true);
    float y = a.y;
    int cols = a.w > 560 ? 6 : 4;
    static const int ic[] = { IC_copy, IC_cut, IC_paste, IC_layers, IC_trash, IC_join, IC_sector, IC_flip, IC_flip_v, IC_rotate, IC_settings, IC_cube };
    static const char *lb[] = { "Copy", "Cut", "Paste", "Duplicate", "Delete", "Join", "Merge", "Mirror H", "Mirror V", "Rotate 90\xC2\xB0", "Properties", "View in 3D" };
    int r = tile_grid(ID("acts"), a, &y, ic, lb, NULL, 12, cols);
    switch (r) {
    case 0: ed_copy(false); break;
    case 1: ed_copy(true); break;
    case 2: ed_paste(); break;
    case 3: ed_duplicate(); break;
    case 4: ed_delete(); break;
    case 5: ed_join(false); break;
    case 6: ed_join(true); break;
    case 7: ed_mirror(false); break;
    case 8: ed_mirror(true); break;
    case 9: ed_rotate90(true); break;
    case 10: M.sheet_open = true; M.sheet_tab = SHEET_PROPS; break;
    case 11: ed_set_mode(MODE_3D); break;
    default: break;
    }
    if (r >= 0) close_bsheet();
}

static void bs_menu(const Input *in) {
    bool two = in->win_w - in->inset_l - in->inset_r > 610;
    Rect a = bsheet_begin(in, two ? 280 : 440, IC_menu, "Menu", false);
    float cw = two ? (a.w - 12) * 0.5f : a.w, h = 56;
    static const int ic[6] = { IC_layers, IC_save, IC_fit, IC_grid, IC_book, IC_close };
    static const char *lb[6] = { "Maps", "Save", "Fit map to screen", "Grid", "Touch controls", "Close project" };
    const char *sub[6] = { ed.proj.name, ed.dirty ? "Unsaved changes" : "All changes saved", NULL, NULL, NULL, "Saves and returns to your projects" };
    for (int i = 0; i < 6; i++) {
        Rect r = rect(a.x + (two ? (i % 2) * (cw + 12) : 0), a.y - 8 + (two ? i / 2 : i) * h, cw, h - 4);
        if (!sheet_item(IDI("mm", i), r, ic[i], lb[i], sub[i], false)) continue;
        switch (i) {
        case 0: M.sheet_open = true; M.sheet_tab = SHEET_MAP; close_bsheet(); break;
        case 1: if (ed_save()) ed_toast("Saved", 1); close_bsheet(); break;
        case 2: ed_set_mode(MODE_2D); ed_fit_view(); close_bsheet(); break;
        case 3: open_bsheet(BS_GRID); break;
        case 4: M.guide_tab = ed.mode == MODE_3D ? 1 : 0; open_bsheet(BS_GUIDE); break;
        case 5: leave_editor(); break;
        }
    }
}

static void bs_project(const Input *in) {
    if (M.proj_sel < 0 || M.proj_sel >= M.projects.n) { close_bsheet(); return; }
    ProjectInfo *p = &M.projects.a[M.proj_sel];
    Rect a = bsheet_begin(in, 330, IC_open, p->name, false);
    float y = a.y - 6;
    if (sheet_item(ID("pj_open"), rect(a.x, y, a.w, 52), IC_open, "Open", NULL, false)) { close_bsheet(); open_project_idx(M.proj_sel); return; }
    y += 54;
    if (sheet_item(ID("pj_dup"), rect(a.x, y, a.w, 52), IC_copy, "Duplicate", NULL, false)) {
        char nf[600];
        if (project_duplicate(p->file, nf, sizeof nf)) { projects_scan(&M.projects); ed_toast("Project duplicated", 1); }
        else ed_toast("Could not duplicate the project", 2);
        close_bsheet();
        return;
    }
    y += 54;
    if (sheet_item(ID("pj_forget"), rect(a.x, y, a.w, 52), IC_close, "Remove from list", "The files stay on the device", false)) {
        projects_forget(p->file); projects_scan(&M.projects); close_bsheet(); return;
    }
    y += 54;
    if (sheet_item(ID("pj_del"), rect(a.x, y, a.w, 52), IC_trash, "Delete project", NULL, true)) { M.confirm_kind = CF_DELETE_PROJECT; open_bsheet(BS_CONFIRM); }
}

static void bs_new_map(const Input *in) {
    Rect a = bsheet_begin(in, 300, IC_new, "New map", false);
    float y = a.y;
    float cw = (a.w - 3 * 12) / 4;
    for (int t = 0; t < TPL_COUNT; t++) {
        Rect c = rect(a.x + t * (cw + 12), y, cw, 150);
        bool held, on = M.new_tpl == t;
        if (tap(IDI("nm_t", t), c, &held)) M.new_tpl = t;
        gfx_rrect(c, 16, C(MS_SURF2));
        gfx_rrect_line(c, 16, on ? 2 : 1, on ? C(MS_ACC) : C(MS_OUTLINE));
        map_thumb(rect(c.x + 2, c.y + 2, c.w - 4, 96), M.tpl_seg[t], M.tpl_nseg[t], on, 14);
        gfx_text_fit(FONT_UI_BOLD, c.x + 12, c.y + 108, c.w - 24, g_tpl_names[t], C(MS_ON));
        ripple(c, 16, held);
    }
    y += 166;
    if (mbutton(ID("nm_go"), rect(a.x + a.w - 200, y, 200, 48), IC_check, "Name it & create", MB_FILLED)) {
        char def[64];
        snprintf(def, sizeof def, "Map %d", ed.proj.nmaps + 1);
        text_request(TXT_MAP_NAME, "Map name", def, TEXTIN_NAME);
    }
    if (mbutton(ID("nm_cancel"), rect(a.x + a.w - 320, y, 110, 48), -1, "Cancel", MB_TEXT)) close_bsheet();
}

static void bs_confirm(const Input *in) {
    const char *title = "Are you sure?", *body = "", *ok = "Delete";
    char b[200];
    if (M.confirm_kind == CF_DELETE_PROJECT && M.proj_sel >= 0 && M.proj_sel < M.projects.n) {
        title = "Delete project?";
        snprintf(b, sizeof b, "\"%s\" and all of its maps and textures will be moved to the trash folder.", M.projects.a[M.proj_sel].name);
        body = b;
    } else if (M.confirm_kind == CF_DELETE_MAP && M.confirm_idx >= 0 && M.confirm_idx < ed.proj.nmaps) {
        title = "Delete map?";
        snprintf(b, sizeof b, "\"%s\" will be removed from the project. This can't be undone.", ed.proj.maps[M.confirm_idx].title);
        body = b;
    }
    Rect a = bsheet_begin(in, 230, IC_warn, title, false);
    gfx_text_fit(FONT_UI, a.x, a.y, a.w, body, C(MS_ON2));
    float y = a.y + 52;
    if (mbutton(ID("cf_ok"), rect(a.x + a.w - 160, y, 160, 48), IC_trash, ok, MB_DANGER)) {
        if (M.confirm_kind == CF_DELETE_PROJECT && M.proj_sel >= 0 && M.proj_sel < M.projects.n) {
            ProjectInfo *p = &M.projects.a[M.proj_sel];
            if (plat_recycle(p->dir)) { projects_forget(p->file); ed_toast("Project deleted", 1); }
            else ed_toast("Could not delete the project", 2);
            projects_scan(&M.projects);
        } else if (M.confirm_kind == CF_DELETE_MAP && M.confirm_idx >= 0 && M.confirm_idx < ed.proj.nmaps && ed.proj.nmaps > 1) {
            int idx = M.confirm_idx;
            bool cur = idx == ed.map_idx;
            if (project_delete_map(&ed.proj, idx)) {
                project_save(&ed.proj);
                if (cur) { ed.dirty = false; ed_load_map(0); }
                else if (idx < ed.map_idx) ed.map_idx--;
                ed_toast("Map deleted", 1);
            }
        }
        close_bsheet();
    }
    if (mbutton(ID("cf_cancel"), rect(a.x + a.w - 280, y, 110, 48), -1, "Cancel", MB_TEXT)) close_bsheet();
}

static void bs_message(const Input *in) {
    Rect a = bsheet_begin(in, 220, IC_info, M.msg_title, false);
    gfx_text_fit(FONT_UI, a.x, a.y, a.w, M.msg_body, C(MS_ON2));
    if (mbutton(ID("msg_ok"), rect(a.x + a.w - 120, a.y + 52, 120, 48), -1, "OK", MB_FILLED)) close_bsheet();
}

static void bs_guide(const Input *in) {
    ui.layer = LAYER_MODAL;
    Rect full = rect(0, 0, in->win_w, in->win_h);
    region(full);
    gfx_rect(full, C(0x101114));
    float x = in->inset_l + 12, y = in->inset_t + 8, W = in->win_w - in->inset_r;
    if (icon_button(ID("gd_close"), rect(x, y, 48, 48), IC_close, false, 24)) close_bsheet();
    gfx_text(FONT_HEAD, x + 60, y + 24 - gfx_font_h(FONT_HEAD) * 0.5f, "Touch controls", C(MS_ON));
    static const char *tabs[3] = { "Editor", "3D", "Play" };
    bool wide = in->win_w > in->win_h;
    float tw = SF_MIN(300, W - x - 24);
    if (wide) seg_tabs(ID("gd_tabs"), rect(W - 16 - tw, y + 2, tw, 44), tabs, 3, &M.guide_tab);
    else { y += 56; seg_tabs(ID("gd_tabs"), rect(x + 8, y, W - x - 24, 44), tabs, 3, &M.guide_tab); }
    y += 60;
    gfx_rect(rect(0, y - 6, in->win_w, 1), C(MS_OUTLINE));
    Rect area = rect(0, y, in->win_w, in->win_h - y);
    static float gs;
    scroll_area(area, &gs, 0, false);
    gfx_clip_push(area);
    int cols = wide ? 3 : 1;
    float end = guide_cards(rect(x + 8, 0, W - x - 24, 0), M.guide_tab, cols, y + 8 - gs);
    for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == &gs) M.sreg_next[s].content = end + gs - y + 20 + in->inset_b;
    gfx_clip_pop();
}

static void bs_pause(const Input *in) {
    ui.layer = LAYER_MODAL;
    Rect full = rect(0, 0, in->win_w, in->win_h);
    region(full);
    gfx_rect(full, col(0, 0, 0, 150));
    float w = 320, h = 300;
    Rect r = rect((in->win_w - w) * 0.5f, (in->win_h - h) * 0.5f, w, h);
    gfx_rrect(r, 28, C(0x1B1D22));
    gfx_text_center(FONT_HEAD, rect(r.x, r.y + 20, w, 34), "Paused", C(MS_ON));
    float y = r.y + 76;
    if (mbutton(ID("ps_resume"), rect(r.x + 24, y, w - 48, 52), IC_play, "Resume", MB_FILLED)) { M.paused = false; close_bsheet(); }
    y += 62;
    if (mbutton(ID("ps_restart"), rect(r.x + 24, y, w - 48, 52), IC_redo, "Restart level", MB_TONAL)) { M.paused = false; close_bsheet(); play_start(false); }
    y += 62;
    if (mbutton(ID("ps_exit"), rect(r.x + 24, y, w - 48, 52), IC_mode2d, "Back to the editor", MB_OUTLINE)) { M.paused = false; close_bsheet(); play_stop(); }
}

static void bottom_sheets(const Input *in) {
    switch (M.bsheet) {
    case BS_GRID: bs_grid(in); break;
    case BS_MORE: bs_more(in); break;
    case BS_ACTIONS: bs_actions(in); break;
    case BS_MENU: bs_menu(in); break;
    case BS_PROJECT: bs_project(in); break;
    case BS_NEW_MAP: bs_new_map(in); break;
    case BS_CONFIRM: bs_confirm(in); break;
    case BS_MESSAGE: bs_message(in); break;
    case BS_GUIDE: bs_guide(in); break;
    case BS_PAUSE: bs_pause(in); break;
    default: break;
    }
}

/* ================================================================== side sheet: properties */
static float g_sy;   /* layout cursor inside the side sheet */

static float sec_val(const IntArr *s, size_t off, bool *mixed) {
    float v = *(float *)((char *)&ed.map.sectors.a[s->a[0]] + off);
    *mixed = false;
    for (int i = 1; i < s->n; i++) if (*(float *)((char *)&ed.map.sectors.a[s->a[i]] + off) != v) *mixed = true;
    return v;
}

static void apply_number(int which, float v) {
    Map *m = &ed.map;
    SelSet *S = &g_sel;
    switch (which) {
    case NUM_FLOOR: case NUM_CEIL:
        if (!S->sec.n) return;
        ed_snapshot(which == NUM_FLOOR ? "Floor height" : "Ceiling height");
        for (int i = 0; i < S->sec.n; i++) {
            MSector *x = &m->sectors.a[S->sec.a[i]];
            if (which == NUM_FLOOR) x->floor_h = SF_MIN(v, x->ceil_h); else x->ceil_h = SF_MAX(v, x->floor_h);
        }
        break;
    case NUM_LIGHT:
        ed_snapshot("Light level");
        for (int i = 0; i < S->sec.n; i++) m->sectors.a[S->sec.a[i]].light = SF_CLAMP((int)v, 0, 255);
        break;
    case NUM_TAG:
        ed_snapshot("Sector tag");
        for (int i = 0; i < S->sec.n; i++) m->sectors.a[S->sec.a[i]].tag = SF_CLAMP((int)v, 0, 65535);
        break;
    case NUM_GRID: ed.grid = SF_CLAMP((int)v, 1, 4096); break;
    case NUM_OFFX: case NUM_OFFY:
        ed_snapshot("Texture offset");
        for (int i = 0; i < S->lin.n; i++) for (int k = 0; k < 2; k++) {
            MSide *sd = &m->lines.a[S->lin.a[i]].side[k];
            if (which == NUM_OFFX) sd->offx = v; else sd->offy = v;
        }
        break;
    case NUM_VX: case NUM_VY:
        if (S->vtx.n != 1) return;
        ed_snapshot("Move vertex");
        if (which == NUM_VX) m->verts.a[S->vtx.a[0]].x = v; else m->verts.a[S->vtx.a[0]].y = v;
        {
            uint8_t *mask = calloc((size_t)m->verts.n, 1);
            mask[S->vtx.a[0]] = 1;
            map_finalize_move(m, mask);
            free(mask);
        }
        r3d_invalidate();
        break;
    case NUM_ANGLE:
        ed_snapshot("Thing angle");
        for (int i = 0; i < S->thg.n; i++) m->things.a[S->thg.a[i]].angle = fmodf(v + 360 * 4, 360);
        break;
    }
    ed_mark_dirty();
}

static void open_tex_target(int target) { M.sheet_tab = SHEET_TEX; M.tex_target = target; ed.tex_target = target; }

static bool tex_card(uint32_t id, Rect r, const char *label, const char *tex) {
    bool held;
    bool c = tap(id, r, &held);
    gfx_rrect(r, 14, C(MS_SURF2));
    gfx_rrect_line(r, 14, 1, C(MS_OUTLINE));
    ripple(r, 14, held);
    float ts = r.h - 16;
    Rect t = rect(r.x + 8, r.y + 8, ts, ts);
    if (tex && tex[0] && strcmp(tex, "-")) tex_tile(tex, t); else gfx_rrect(t, 8, C(MS_SURF3));
    float tx = t.x + ts + 10, tw = r.w - ts - 22;
    if (label) gfx_text(FONT_SMALL, tx, r.y + 10, label, C(MS_ON3));
    const char *nm = tex && tex[0] ? tex : "\xE2\x80\x94";
    const char *us = strchr(nm, '_');
    float ty = label ? r.y + 30 : r.y + r.h * 0.5f - 8;
    if (gfx_text_w(FONT_MONO_SMALL, nm) > tw && us && tw > 30) {   /* FLOOR_ / STONE on two lines */
        if (!label) ty -= 8;
        gfx_text_n(FONT_MONO_SMALL, tx, ty, nm, (int)(us - nm) + 1, C(MS_ON));
        gfx_text_fit(FONT_MONO_SMALL, tx, ty + 17, tw, us + 1, C(MS_ON));
    } else if (tw > 20) gfx_text_fit(FONT_MONO_SMALL, tx, ty, tw, nm, C(MS_ON));
    return c;
}

static void step_chips(Rect a) {
    static const int st[] = { 1, 4, 8, 16, 32 };
    gfx_text(FONT_UI, a.x, g_sy + 12, "Step", C(MS_ON2));
    float bw = (a.w - 56 - 4 * 8) / 5;
    for (int i = 0; i < 5; i++) {
        Rect b = rect(a.x + 56 + i * (bw + 8), g_sy, bw, 42);
        char t[8];
        snprintf(t, sizeof t, "%d", st[i]);
        bool held, on = ed.hstep == st[i];
        if (tap(IDI("stp", i), b, &held)) ed.hstep = st[i];
        gfx_rrect(b, 12, on ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 12, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 12, held);
        gfx_text_center(FONT_UI_BOLD, b, t, on ? C(0xFFD2B0) : C(MS_ON));
    }
    g_sy += 56;
}

static void props_header(Rect a, int icon, const char *title, const char *sub, bool renamable) {
    Rect ic = rect(a.x, g_sy, 52, 52);
    gfx_rrect(ic, 14, C(0x4A2A12));
    gfx_icon(icon, ic.x + 26, ic.y + 26, 26, C(MS_ACC_ON));
    Rect tr = rect(a.x + 64, g_sy, a.w - 64, 52);
    bool held = false;
    if (renamable && tap(ID("pr_name"), tr, &held)) {
        const char *cur = ed.map.sectors.a[g_sel.sec.a[0]].name;
        text_request(TXT_SECTOR_NAME, "Sector name", cur, TEXTIN_NAME);
    }
    ripple(tr, 10, held);
    gfx_text_fit(FONT_HEAD, tr.x, g_sy + 2, tr.w - 4, title, C(MS_ON));
    gfx_text_fit(FONT_MONO_SMALL, tr.x, g_sy + 32, tr.w - 4, sub, C(MS_ON3));
    g_sy += 68;
}

static void props_sectors(Rect a) {
    Map *m = &ed.map;
    IntArr *S = &g_sel.sec;
    MSector *s0 = &m->sectors.a[S->a[0]];
    char t[96], sub[64];
    if (S->n == 1) {
        snprintf(t, sizeof t, "%s", s0->name[0] ? s0->name : "Unnamed sector");
        snprintf(sub, sizeof sub, "Sector %d \xC2\xB7 %d lines", S->a[0], map_sector_line_count(m, S->a[0]));
    } else {
        snprintf(t, sizeof t, "%d sectors", S->n);
        snprintf(sub, sizeof sub, "Edits apply to all of them");
    }
    props_header(a, IC_sector, t, sub, S->n == 1);
    float hw = (a.w - 12) * 0.5f;
    gfx_text(FONT_UI, a.x, g_sy, "Floor", C(MS_ON2));
    gfx_text(FONT_UI, a.x + hw + 12, g_sy, "Ceiling", C(MS_ON2));
    g_sy += 24;
    bool mf, mc;
    float fl = sec_val(S, offsetof(MSector, floor_h), &mf), cl = sec_val(S, offsetof(MSector, ceil_h), &mc);
    char v1[24], v2[24];
    snprintf(v1, sizeof v1, mf ? "\xE2\x80\x94" : "%g", fl);
    snprintf(v2, sizeof v2, mc ? "\xE2\x80\x94" : "%g", cl);
    int r1 = stepper(ID("p_fl"), rect(a.x, g_sy, hw, 56), v1), r2 = stepper(ID("p_cl"), rect(a.x + hw + 12, g_sy, hw, 56), v2);
    if (r1 == 2) { M.txt_num = NUM_FLOOR; text_request(TXT_NUMBER, "Floor height", v1, TEXTIN_NUMBER); }
    if (r2 == 2) { M.txt_num = NUM_CEIL; text_request(TXT_NUMBER, "Ceiling height", v2, TEXTIN_NUMBER); }
    if (r1 == 1 || r1 == -1 || r2 == 1 || r2 == -1) {
        ed_snapshot_merge(r1 ? "Floor height" : "Ceiling height");
        for (int i = 0; i < S->n; i++) {
            MSector *x = &m->sectors.a[S->a[i]];
            if (r1) x->floor_h = SF_MIN(x->floor_h + r1 * ed.hstep, x->ceil_h);
            if (r2 == 1 || r2 == -1) x->ceil_h = SF_MAX(x->ceil_h + r2 * ed.hstep, x->floor_h);
        }
    }
    g_sy += 70;
    step_chips(a);
    gfx_text(FONT_UI, a.x, g_sy, "Floor texture", C(MS_ON2));
    gfx_text(FONT_UI, a.x + hw + 12, g_sy, "Ceiling texture", C(MS_ON2));
    g_sy += 24;
    if (tex_card(ID("p_ft"), rect(a.x, g_sy, hw, 76), NULL, s0->floor_tex)) open_tex_target(TEXT_FLOOR);
    if (tex_card(ID("p_ct"), rect(a.x + hw + 12, g_sy, hw, 76), NULL, s0->ceil_tex)) open_tex_target(TEXT_CEIL);
    g_sy += 90;
    char lt[16];
    snprintf(lt, sizeof lt, "%d", s0->light);
    gfx_text(FONT_UI, a.x, g_sy, "Light level", C(MS_ON2));
    gfx_text_right(FONT_MONO, a.x + a.w, g_sy, lt, C(MS_ON));
    g_sy += 22;
    float lv = (float)s0->light;
    if (slider(ID("p_light"), rect(a.x - 8, g_sy, a.w + 16, 44), &lv, 0, 255)) {
        ed_snapshot_merge("Light level");
        for (int i = 0; i < S->n; i++) m->sectors.a[S->a[i]].light = (int)lv;
        ed_mark_dirty();
    }
    g_sy += 56;
    gfx_text(FONT_UI, a.x, g_sy, "Sector type", C(MS_ON2));
    g_sy += 26;
    float x = a.x;
    for (int k = 0; k < ST_COUNT; k++) {
        float w = gfx_text_w(FONT_UI_BOLD, g_sector_type_names[k]) + 30;
        if (x + w > a.x + a.w) { x = a.x; g_sy += 48; }
        Rect b = rect(x, g_sy, w, 40);
        bool held, on = s0->type == k;
        if (tap(IDI("p_ty", k), b, &held)) {
            ed_snapshot("Sector type");
            for (int i = 0; i < S->n; i++) m->sectors.a[S->a[i]].type = k;
        }
        gfx_rrect(b, 10, on ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 10, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 10, held);
        gfx_text_center(FONT_UI_BOLD, b, g_sector_type_names[k], on ? C(0xFFD2B0) : C(MS_ON));
        x += w + 8;
    }
    g_sy += 58;
    gfx_text(FONT_UI, a.x, g_sy + 18, "Tag", C(MS_ON2));
    char tg[16];
    snprintf(tg, sizeof tg, "%d", s0->tag);
    int rt = stepper(ID("p_tag"), rect(a.x + a.w - 180, g_sy, 180, 52), tg);
    if (rt == -1 || rt == 1) { ed_snapshot_merge("Sector tag"); for (int i = 0; i < S->n; i++) m->sectors.a[S->a[i]].tag = SF_MAX(0, m->sectors.a[S->a[i]].tag + rt); }
    if (rt == 2) { M.txt_num = NUM_TAG; text_request(TXT_NUMBER, "Sector tag", tg, TEXTIN_NUMBER); }
    g_sy += 66;
    if (S->n >= 2 && ed.mode == MODE_2D) {
        if (mbutton(ID("p_join"), rect(a.x, g_sy, hw, 48), IC_join, "Join", MB_TONAL)) ed_join(false);
        if (mbutton(ID("p_merge"), rect(a.x + hw + 12, g_sy, hw, 48), IC_sector, "Merge", MB_TONAL)) ed_join(true);
        g_sy += 60;
    }
}

static void props_lines(Rect a) {
    Map *m = &ed.map;
    IntArr *L = &g_sel.lin;
    MLine *l0 = &m->lines.a[L->a[0]];
    char t[64], sub[64];
    const MVert *v1 = &m->verts.a[l0->v1], *v2 = &m->verts.a[l0->v2];
    if (L->n == 1) {
        snprintf(t, sizeof t, "Line %d", L->a[0]);
        snprintf(sub, sizeof sub, "length %.0f \xC2\xB7 %s", hypot(v2->x - v1->x, v2->y - v1->y), l0->side[1].sector >= 0 ? "two-sided" : "one-sided");
    } else { snprintf(t, sizeof t, "%d lines", L->n); snprintf(sub, sizeof sub, "Edits apply to all of them"); }
    props_header(a, IC_wall, t, sub, false);
    int side = SF_CLAMP(g_sel.side_hint, 0, 1);
    if (l0->side[side].sector < 0) side = 0;
    for (int sd = 0; sd < 2; sd++) {
        MSide *S = &l0->side[sd];
        if (S->sector < 0) continue;
        gfx_text(FONT_UI, a.x, g_sy, sd == 0 ? "Front side" : "Back side", C(MS_ON2));
        g_sy += 24;
        float tw = (a.w - 16) / 3;
        if (tex_card(IDI("p_up", sd), rect(a.x, g_sy, tw, 92), "Upper", S->upper)) open_tex_target(TEXT_WALL);
        if (tex_card(IDI("p_mid", sd), rect(a.x + tw + 8, g_sy, tw, 92), "Middle", S->middle)) open_tex_target(TEXT_WALL);
        if (tex_card(IDI("p_lo", sd), rect(a.x + (tw + 8) * 2, g_sy, tw, 92), "Lower", S->lower)) open_tex_target(TEXT_WALL);
        g_sy += 104;
    }
    static const char *fl[3] = { "Impassable", "Upper unpegged", "Lower unpegged" };
    static const int fv[3] = { LF_BLOCK, LF_UPPER_UNPEG, LF_LOWER_UNPEG };
    for (int k = 0; k < 3; k++) {
        bool on = (l0->flags & fv[k]) != 0, was = on;
        toggle_switch(IDI("p_fl", k), rect(a.x, g_sy, a.w, 48), fl[k], &on);
        if (on != was) {
            ed_snapshot("Line flags");
            for (int i = 0; i < L->n; i++) { if (on) m->lines.a[L->a[i]].flags |= fv[k]; else m->lines.a[L->a[i]].flags &= ~fv[k]; }
        }
        g_sy += 52;
    }
    g_sy += 6;
    float hw = (a.w - 12) * 0.5f;
    gfx_text(FONT_UI, a.x, g_sy, "Offset X", C(MS_ON2));
    gfx_text(FONT_UI, a.x + hw + 12, g_sy, "Offset Y", C(MS_ON2));
    g_sy += 24;
    char ox[16], oy[16];
    snprintf(ox, sizeof ox, "%g", l0->side[side].offx);
    snprintf(oy, sizeof oy, "%g", l0->side[side].offy);
    int rx = stepper(ID("p_ox"), rect(a.x, g_sy, hw, 52), ox), ry = stepper(ID("p_oy"), rect(a.x + hw + 12, g_sy, hw, 52), oy);
    if (rx == 1 || rx == -1 || ry == 1 || ry == -1) {
        ed_snapshot_merge("Texture offset");
        for (int i = 0; i < L->n; i++) for (int k = 0; k < 2; k++) {
            MSide *sd = &m->lines.a[L->a[i]].side[k];
            if (rx == 1 || rx == -1) sd->offx += rx * 8;
            if (ry == 1 || ry == -1) sd->offy += ry * 8;
        }
    }
    if (rx == 2) { M.txt_num = NUM_OFFX; text_request(TXT_NUMBER, "Texture offset X", ox, TEXTIN_NUMBER); }
    if (ry == 2) { M.txt_num = NUM_OFFY; text_request(TXT_NUMBER, "Texture offset Y", oy, TEXTIN_NUMBER); }
    g_sy += 66;
    if (ed.mode == MODE_2D) {
        if (mbutton(ID("p_flip"), rect(a.x, g_sy, a.w, 48), IC_flip, "Flip line direction", MB_TONAL)) ed_flip_lines();
        g_sy += 60;
    }
}

static void props_things(Rect a) {
    Map *m = &ed.map;
    IntArr *T = &g_sel.thg;
    MThing *t0 = &m->things.a[T->a[0]];
    char t[64], sub[64];
    if (T->n == 1) { snprintf(t, sizeof t, "%s", g_thing_info[t0->type].name); snprintf(sub, sizeof sub, "Thing %d \xC2\xB7 %.0f, %.0f", T->a[0], t0->x, t0->y); }
    else { snprintf(t, sizeof t, "%d things", T->n); snprintf(sub, sizeof sub, "Edits apply to all of them"); }
    props_header(a, thing_icon(t0->type), t, sub, false);
    gfx_text(FONT_UI, a.x, g_sy, "Facing", C(MS_ON2));
    g_sy += 26;
    /* compass of eight directions */
    float R = 74, cx = a.x + a.w * 0.5f, cy = g_sy + R + 22;
    gfx_circle_line(cx, cy, R, 1, C(MS_OUTLINE));
    for (int k = 0; k < 8; k++) {
        float ang = k * 45.0f, rad = ang * (float)SF_DEG2RAD;
        float bx = cx + cosf(rad) * R, by = cy - sinf(rad) * R;
        Rect b = rect(bx - 22, by - 22, 44, 44);
        bool held, on = fabsf(fmodf(t0->angle - ang + 720, 360)) < 1;
        if (tap(IDI("p_ang", k), b, &held)) {
            ed_snapshot("Thing angle");
            for (int i = 0; i < T->n; i++) m->things.a[T->a[i]].angle = ang;
        }
        gfx_circle(bx, by, 21, on ? C(MS_ACC) : C(MS_SURF2));
        if (held) gfx_circle(bx, by, 21, col(255, 255, 255, 24));
        char l[8];
        snprintf(l, sizeof l, "%d", (int)ang);
        gfx_text_center(FONT_SMALL, b, l, on ? C(0x1A0D02) : C(MS_ON));
    }
    float ar = t0->angle * (float)SF_DEG2RAD;
    gfx_line(cx, cy, cx + cosf(ar) * (R - 30), cy - sinf(ar) * (R - 30), 3, C(MS_ACC));
    gfx_circle(cx, cy, 6, C(MS_ACC));
    g_sy = cy + R + 34;
    gfx_text(FONT_UI, a.x, g_sy, "Type", C(MS_ON2));
    g_sy += 26;
    float bw = (a.w - 8) * 0.5f;
    for (int k = 0; k < TH_COUNT; k++) {
        Rect b = rect(a.x + (k % 2) * (bw + 8), g_sy + (k / 2) * 52, bw, 46);
        bool held, on = t0->type == k;
        if (tap(IDI("p_tt", k), b, &held)) ed_apply_thing_type(k);
        gfx_rrect(b, 12, on ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 12, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 12, held);
        gfx_circle(b.x + 22, b.y + 23, 11, col_hex(g_thing_info[k].color));
        gfx_icon(thing_icon(k), b.x + 22, b.y + 23, 14, C(0x101114));
        gfx_text_fit(FONT_UI, b.x + 42, b.y + 23 - gfx_font_h(FONT_UI) * 0.5f, b.w - 50, g_thing_info[k].name, on ? C(0xFFD2B0) : C(MS_ON));
    }
    g_sy += ((TH_COUNT + 1) / 2) * 52 + 12;
}

static void props_verts(Rect a) {
    IntArr *V = &g_sel.vtx;
    char t[48], sub[64];
    MVert *v = &ed.map.verts.a[V->a[0]];
    if (V->n == 1) { snprintf(t, sizeof t, "Vertex %d", V->a[0]); snprintf(sub, sizeof sub, "drag it on the map to move"); }
    else { snprintf(t, sizeof t, "%d vertices", V->n); snprintf(sub, sizeof sub, "drag them on the map to move"); }
    props_header(a, IC_vertex, t, sub, false);
    if (V->n != 1) return;
    float hw = (a.w - 12) * 0.5f;
    gfx_text(FONT_UI, a.x, g_sy, "X", C(MS_ON2));
    gfx_text(FONT_UI, a.x + hw + 12, g_sy, "Y", C(MS_ON2));
    g_sy += 24;
    char vx[24], vy[24];
    snprintf(vx, sizeof vx, "%g", v->x);
    snprintf(vy, sizeof vy, "%g", v->y);
    int rx = stepper(ID("p_vx"), rect(a.x, g_sy, hw, 56), vx), ry = stepper(ID("p_vy"), rect(a.x + hw + 12, g_sy, hw, 56), vy);
    if (rx == 1 || rx == -1) apply_number(NUM_VX, (float)(v->x + rx * ed.grid));
    if (ry == 1 || ry == -1) apply_number(NUM_VY, (float)(v->y + ry * ed.grid));
    if (rx == 2) { M.txt_num = NUM_VX; text_request(TXT_NUMBER, "Vertex X", vx, TEXTIN_NUMBER); }
    if (ry == 2) { M.txt_num = NUM_VY; text_request(TXT_NUMBER, "Vertex Y", vy, TEXTIN_NUMBER); }
    g_sy += 70;
}

static void props_none(Rect a) {
    Map *m = &ed.map;
    gfx_icon(IC_select, a.x + a.w * 0.5f, g_sy + 40, 34, C(MS_ON3));
    gfx_text_center(FONT_UI_BOLD, rect(a.x, g_sy + 70, a.w, 24), "Nothing selected", C(MS_ON));
    gfx_text_center(FONT_SMALL, rect(a.x, g_sy + 96, a.w, 20), ed.mode == MODE_3D ? "Tap a floor, ceiling, wall or thing" : "Tap a sector, line or thing on the map", C(MS_ON2));
    g_sy += 140;
    char st[128];
    snprintf(st, sizeof st, "%d sectors \xC2\xB7 %d lines \xC2\xB7 %d things", m->sectors.n, m->lines.n, m->things.n);
    gfx_text_center(FONT_MONO_SMALL, rect(a.x, g_sy, a.w, 20), st, C(MS_ON3));
    g_sy += 40;
}

/* ================================================================== side sheet: textures / things / map */
static void tex_tab(Rect a, const Input *in) {
    float y = g_sy;
    float sw = a.w - 120;
    char ph[48];
    snprintf(ph, sizeof ph, "%s", M.tex_search[0] ? M.tex_search : "Search");
    Rect sr = rect(a.x, y, sw, 48);
    bool held;
    if (tap(ID("tx_search"), sr, &held)) text_request(TXT_TEX_SEARCH, "Search textures", M.tex_search, TEXTIN_NAME);
    gfx_rrect(sr, 24, C(MS_SURF2));
    ripple(sr, 24, held);
    gfx_icon(IC_search, sr.x + 24, sr.y + 24, 20, C(MS_ON2));
    gfx_text_fit(FONT_UI, sr.x + 44, sr.y + 24 - gfx_font_h(FONT_UI) * 0.5f, sw - 80, ph, M.tex_search[0] ? C(MS_ON) : C(MS_ON3));
    if (M.tex_search[0] && icon_button(ID("tx_clr"), rect(sr.x + sw - 44, y + 2, 44, 44), IC_close, false, 18)) M.tex_search[0] = 0;
    if (mbutton(ID("tx_gal"), rect(a.x + sw + 8, y, 112, 48), IC_import, "Gallery", MB_OUTLINE)) {
        if (!plat_pick_image_begin()) ed_toast("The gallery is not available", 2);
    }
    y += 60;
    static const char *flt[5] = { "All", "Walls", "Flats", "Liquids", "Mine" };
    float x = a.x;
    for (int i = 0; i < 5; i++) {
        float w = gfx_text_w(FONT_UI_BOLD, flt[i]) + 24;
        if (x + w > a.x + a.w) { x = a.x; y += 46; }
        Rect b = rect(x, y, w, 40);
        bool on = M.tex_filter == i;
        if (tap(IDI("tx_f", i), b, &held)) M.tex_filter = i;
        gfx_rrect(b, 10, on ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 10, 1, on ? C(0x7A4116) : C(MS_OUTLINE));
        gfx_text_center(FONT_UI_BOLD, b, flt[i], on ? C(0xFFD2B0) : C(MS_ON));
        x += w + 6;
    }
    y += 50;
    if (ed.mode == MODE_2D) {
        static const char *tg[4] = { "Auto", "Floor", "Ceiling", "Walls" };
        int sel = ed.tex_target;
        seg_tabs(ID("tx_tg"), rect(a.x, y, a.w, 40), tg, 4, &sel);
        ed.tex_target = sel;
        y += 50;
    }
    gfx_rect(rect(a.x, y, a.w, 1), C(MS_OUTLINE));
    y += 12;
    int cols = a.w > 230 ? 3 : 2;
    float tw = (a.w - (cols - 1) * 10) / cols, th = tw + 26;
    int k = 0;
    const char *cur = ed.mode == MODE_3D && ed.sel3d.n ? ed3d_surface_texture(ed.sel3d.a[0]) : NULL;
    for (int i = 0; i < g_assets.tex.n; i++) {
        TexEntry *e = &g_assets.tex.a[i];
        if (M.tex_filter == 1 && e->cat != TEXCAT_WALL) continue;
        if (M.tex_filter == 2 && e->cat != TEXCAT_FLAT && e->cat != TEXCAT_SKY) continue;
        if (M.tex_filter == 3 && e->cat != TEXCAT_LIQUID) continue;
        if (M.tex_filter == 4 && e->builtin) continue;
        if (M.tex_search[0]) {
            char n1[TEXNAME], n2[32];
            sf_strcpy(n1, sizeof n1, e->name); sf_strcpy(n2, sizeof n2, M.tex_search);
            sf_upper(n1); sf_upper(n2);
            if (!strstr(n1, n2)) continue;
        }
        Rect b = rect(a.x + (k % cols) * (tw + 10), y + (k / cols) * (th + 10), tw, tw);
        k++;
        if (b.y > in->win_h || b.y + th < 0) continue;
        bool on = cur && !strcmp(cur, e->name);
        if (tap(IDI("tx_t", i), rect(b.x, b.y, b.w, th), &held)) {
            ed_apply_texture(e->name);
            push_recent(e->name);
        }
        gfx_quad_tex(&e->tex, b, 0, 0, 1, 1, col(255, 255, 255, 255));
        if (on) gfx_rrect_line(rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4), 10, 3, C(MS_ACC));
        if (held) gfx_rect(b, col(255, 255, 255, 30));
        gfx_text_fit(FONT_MONO_SMALL, b.x, b.y + b.h + 6, b.w, e->name, on ? C(MS_ACC_ON) : C(MS_ON2));
    }
    if (!k) gfx_text(FONT_UI, a.x, y + 10, "No textures match", C(MS_ON3));
    g_sy = y + ((k + cols - 1) / cols) * (th + 10) + 10;
}

static void things_tab(Rect a) {
    gfx_text_fit(FONT_SMALL, a.x, g_sy, a.w, ed.mode == MODE_2D ? "Tap a type, then tap the map to place it" : "Changes the selected things", C(MS_ON2));
    g_sy += 28;
    static const char *cats[] = { "Player start", "Lights", "Enemies", "Pickups", "Props" };
    int lastcat = -1;
    for (int k = 0; k < TH_COUNT; k++) {
        const ThingInfo *ti = &g_thing_info[k];
        if (ti->category != lastcat) {
            lastcat = ti->category;
            section_label(a.x, g_sy + 6, cats[SF_CLAMP(ti->category, 0, 4)]);
            g_sy += 30;
        }
        Rect b = rect(a.x, g_sy, a.w, 56);
        bool held, on = ed.thing_type == k;
        if (tap(IDI("th_t", k), b, &held)) {
            ed.thing_type = k;
            if (map_sel_count(&ed.map, SM_THINGS) && ed.selmode == SM_THINGS) ed_apply_thing_type(k);
            else if (ed.mode == MODE_2D) { ed_set_tool(T_THING); M.sheet_open = false; }
        }
        gfx_rrect(b, 14, on ? C(0x4A2A12) : C(MS_SURF2));
        if (on) gfx_rrect_line(b, 14, 1, C(0x7A4116));
        ripple(b, 14, held);
        gfx_circle(b.x + 28, b.y + 28, 16, col_hex(ti->color));
        gfx_icon(thing_icon(k), b.x + 28, b.y + 28, 18, C(0x101114));
        gfx_text(FONT_UI_BOLD, b.x + 56, b.y + 10, ti->name, on ? C(0xFFD2B0) : C(MS_ON));
        char d[48];
        snprintf(d, sizeof d, "radius %.0f \xC2\xB7 height %.0f", ti->radius, ti->height);
        gfx_text(FONT_SMALL, b.x + 56, b.y + 32, d, C(MS_ON3));
        g_sy += 62;
    }
}

static void map_tab(Rect a) {
    section_label(a.x, g_sy, "MAPS IN THIS PROJECT");
    g_sy += 26;
    for (int i = 0; i < ed.proj.nmaps; i++) {
        Rect b = rect(a.x, g_sy, a.w, 60);
        bool held, cur = i == ed.map_idx;
        if (tap(IDI("mp_m", i), rect(b.x, b.y, b.w - 52, b.h), &held) && !cur) {
            if (ed.dirty) ed_save();
            if (ed.mode == MODE_PLAY) play_stop();
            if (ed_load_map(i)) { ed_toast("Map opened", 1); M.nrecent = 0; push_recent(ed.cur_wall); push_recent(ed.cur_flat); }
        }
        gfx_rrect(b, 14, cur ? C(0x4A2A12) : C(MS_SURF2));
        gfx_rrect_line(b, 14, 1, cur ? C(0x7A4116) : C(MS_OUTLINE));
        ripple(b, 14, held);
        gfx_icon(IC_layers, b.x + 26, b.y + 30, 22, cur ? C(MS_ACC_ON) : C(MS_ON2));
        gfx_text_fit(FONT_UI_BOLD, b.x + 52, b.y + 10, b.w - 112, ed.proj.maps[i].title, cur ? C(0xFFD2B0) : C(MS_ON));
        gfx_text_fit(FONT_MONO_SMALL, b.x + 52, b.y + 34, b.w - 112, ed.proj.maps[i].file, C(MS_ON3));
        if (ed.proj.nmaps > 1 && icon_button(IDI("mp_del", i), rect(b.x + b.w - 50, b.y + 8, 44, 44), IC_trash, false, 20)) {
            M.confirm_kind = CF_DELETE_MAP; M.confirm_idx = i; open_bsheet(BS_CONFIRM);
        }
        g_sy += 68;
    }
    if (mbutton(ID("mp_new"), rect(a.x, g_sy, a.w, 52), IC_plus, "New map", MB_TONAL)) { M.new_tpl = TPL_STARTER; open_bsheet(BS_NEW_MAP); }
    g_sy += 70;
    section_label(a.x, g_sy, "THIS MAP");
    g_sy += 26;
    char st[160];
    snprintf(st, sizeof st, "%d sectors \xC2\xB7 %d lines \xC2\xB7 %d things", ed.map.sectors.n, ed.map.lines.n, ed.map.things.n);
    gfx_text_fit(FONT_UI, a.x, g_sy, a.w, st, C(MS_ON));
    g_sy += 28;
    gfx_text_fit(FONT_SMALL, a.x, g_sy, a.w, ed.dirty ? "Unsaved changes \xC2\xB7 autosave is on" : "All changes saved", C(MS_ON3));
    g_sy += 40;
}

static void side_sheet(Rect r, const Input *in) {
    ui.layer = LAYER_POPUP;
    region(r);
    gfx_rect(r, C(0x15171B));
    gfx_rect(rect(r.x, r.y, 1, r.h), C(MS_OUTLINE));
    static const char *tabs[4] = { "Properties", "Textures", "Things", "Map" };
    float tw = r.w / 4;
    for (int i = 0; i < 4; i++) {
        Rect b = rect(r.x + i * tw, r.y, tw, 52);
        bool held;
        if (tap(IDI("sh_tab", i), b, &held)) M.sheet_tab = i;
        ripple(b, 8, held);
        bool on = M.sheet_tab == i;
        int font = FONT_UI_BOLD;
        gfx_text_center(font, b, tabs[i], on ? C(MS_ACC_ON) : C(MS_ON2));
        if (on) { float w = gfx_text_w(font, tabs[i]) + 8; gfx_rrect(rect(b.x + (tw - w) * 0.5f, b.y + 49, w, 3), 1.5f, C(MS_ACC)); }
    }
    gfx_rect(rect(r.x, r.y + 52, r.w, 1), C(MS_OUTLINE));
    Rect body = rect(r.x, r.y + 53, r.w, r.h - 53);
    float *sc = &M.sheet_scroll[M.sheet_tab];
    scroll_area(body, sc, 0, false);
    gfx_clip_push(body);
    Rect a = rect(r.x + 18, 0, r.w - 36 - in->inset_r * 0, 0);
    g_sy = body.y + 16 - *sc;
    float y0 = g_sy;
    switch (M.sheet_tab) {
    case SHEET_PROPS:
        if (g_sel.sec.n) props_sectors(a);
        else if (g_sel.lin.n) props_lines(a);
        else if (g_sel.thg.n) props_things(a);
        else if (g_sel.vtx.n) props_verts(a);
        else props_none(a);
        break;
    case SHEET_TEX: tex_tab(a, in); break;
    case SHEET_THINGS: things_tab(a); break;
    default: map_tab(a); break;
    }
    for (int s = 0; s < M.nsreg_next; s++) if (M.sreg_next[s].scroll == sc) M.sreg_next[s].content = g_sy - y0 + 24 + in->inset_b;
    gfx_clip_pop();
}

/* ================================================================== editor chrome */
static void top_bar(Rect r, const Input *in) {
    ui.layer = LAYER_POPUP;
    region(r);
    gfx_rect(r, C(0x111216));
    gfx_rect(rect(r.x, r.y + r.h - 1, r.w, 1), C(MS_OUTLINE));
    float x = r.x + 8 + in->inset_l, cy = r.y + r.h * 0.5f;
    if (icon_button(ID("tb_back"), rect(x, cy - 22, 44, 44), IC_chev_left, false, 24)) leave_editor();
    x += 52;
    float segw = SF_MIN(300, r.w * 0.34f), segx = r.x + (r.w - segw) * 0.5f;
    float right = r.x + r.w - in->inset_r - 8;
    /* title -> maps */
    Rect tr = rect(x, r.y + 4, segx - x - 10, r.h - 8);
    bool held;
    if (tap(ID("tb_title"), tr, &held)) { M.sheet_open = true; M.sheet_tab = SHEET_MAP; }
    ripple(tr, 10, held);
    gfx_text_fit(FONT_UI_BOLD, x + 4, cy - 19, tr.w - 8, ed.proj.maps[ed.map_idx].title, C(MS_ON));
    gfx_text_fit(FONT_SMALL, x + 4, cy + 2, tr.w - 8, ed.proj.name, C(MS_ON2));
    /* 2D | 3D | Play */
    Rect sr = rect(segx, cy - 21, segw, 42);
    gfx_rrect(sr, 21, C(MS_SURF));
    gfx_rrect_line(sr, 21, 1, C(MS_OUTLINE));
    static const char *ml[3] = { "2D", "3D", "Play" };
    static const int mi[3] = { IC_mode2d, IC_cube, IC_play };
    float w = segw / 3;
    for (int i = 0; i < 3; i++) {
        Rect b = rect(sr.x + i * w, sr.y, w, sr.h);
        bool on = (int)ed.mode == i;
        if (tap(IDI("tb_mode", i), b, &held) && !on) {
            if (i == MODE_PLAY) { M.paused = false; M.sheet_open = false; }
            ed_set_mode((EdMode)i);
        }
        if (on) gfx_rrect(rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4), 19, C(0x4A2A12));
        ripple(b, 19, held);
        float tw = gfx_text_w(FONT_UI_BOLD, ml[i]) + 26;
        float bx = b.x + (b.w - tw) * 0.5f;
        gfx_icon(mi[i], bx + 9, b.y + b.h * 0.5f, 18, on ? C(0xFFD2B0) : C(MS_ON2));
        gfx_text(FONT_UI_BOLD, bx + 26, b.y + (b.h - gfx_font_h(FONT_UI_BOLD)) * 0.5f, ml[i], on ? C(0xFFD2B0) : C(MS_ON2));
    }
    /* right actions */
    float bx = right - 44;
    Rect kb = rect(bx, cy - 22, 44, 44);
    if (tap(ID("tb_menu"), kb, &held)) open_bsheet(BS_MENU);
    ripple(kb, 22, held);
    kebab(kb.x + 22, kb.y + 22, C(MS_ON));
    bx -= 50;
    if (icon_button(ID("tb_save"), rect(bx, cy - 22, 44, 44), IC_save, false, 22)) { if (ed_save()) ed_toast("Saved", 1); }
    if (ed.dirty) gfx_circle(bx + 34, cy - 12, 5, C(MS_ACC));
    bx -= 50;
    if (icon_button(ID("tb_redo"), rect(bx, cy - 22, 44, 44), IC_redo, false, 22)) ed_redo();
    bx -= 50;
    if (icon_button(ID("tb_undo"), rect(bx, cy - 22, 44, 44), IC_undo, false, 22)) ed_undo();
}

typedef struct { int icon; const char *label; int tool; } RailItem;

static void rail(Rect r, const Input *in) {
    ui.layer = LAYER_POPUP;
    region(r);
    gfx_rect(r, C(0x111216));
    gfx_rect(rect(r.x + r.w - 1, r.y, 1, r.h), C(MS_OUTLINE));
    static const RailItem r2[8] = { { IC_select, "Select", T_SELECT }, { IC_draw_sector, "Draw", T_DRAW }, { IC_rect, "Rect", T_RECT },
                                    { IC_polygon, "Shape", T_POLY }, { IC_line, "Lines", T_LINES }, { IC_thing, "Thing", T_THING },
                                    { IC_bucket, "Paint", T_PAINT }, { IC_grid, "More", -1 } };
    static const RailItem r3[8] = { { IC_select, "Select", T3_SELECT }, { IC_height, "Height", T3_HEIGHT }, { IC_bucket, "Paint", T3_PAINT },
                                    { IC_eyedropper, "Pick", T3_PICK }, { IC_move, "Align", T3_ALIGN }, { IC_light, "Light", T3_LIGHT },
                                    { IC_texture, "Textures", -2 }, { IC_grid, "More", -1 } };
    const RailItem *items = ed.mode == MODE_3D ? r3 : r2;
    float ih = SF_MIN(56, (r.h - 8 - in->inset_b) / 8);
    for (int i = 0; i < 8; i++) {
        Rect b = rect(r.x + 4, r.y + 4 + i * ih, r.w - 8, ih);
        int cur = ed.mode == MODE_3D ? ed.tool3d : ed.tool;
        bool on = items[i].tool >= 0 && cur == items[i].tool;
        if (items[i].tool == -1) on = M.bsheet == BS_MORE || (ed.mode == MODE_2D && (ed.tool == T_VERTEX || ed.tool == T_SPLIT || ed.tool == T_MOVE || ed.tool == T_ROTATE || ed.tool == T_SCALE || ed.tool == T_MEASURE || ed.tool == T_PAN));
        if (items[i].tool == -2) on = M.sheet_open && M.sheet_tab == SHEET_TEX;
        bool held;
        if (tap(IDI("rail", i + ed.mode * 10), b, &held)) {
            if (items[i].tool == -1) open_bsheet(BS_MORE);
            else if (items[i].tool == -2) { if (on) M.sheet_open = false; else { M.sheet_open = true; M.sheet_tab = SHEET_TEX; } }
            else if (ed.mode == MODE_3D) ed.tool3d = items[i].tool;
            else ed_set_tool(items[i].tool);
        }
        Rect pill = rect(b.x + (b.w - 52) * 0.5f, b.y + 2, 52, ih - 22 > 24 ? 30 : 26);
        if (on) gfx_rrect(pill, pill.h * 0.5f, C(0x4A2A12));
        else if (held) gfx_rrect(pill, pill.h * 0.5f, col(255, 255, 255, 20));
        gfx_icon(items[i].icon, pill.x + 26, pill.y + pill.h * 0.5f, 20, on ? C(0xFFD2B0) : C(MS_ON));
        gfx_text_center(on ? FONT_UI_BOLD : FONT_SMALL, rect(b.x, pill.y + pill.h + 1, b.w, 16), items[i].label, on ? C(MS_ON) : C(MS_ON2));
    }
}

static const char *tool_hint(char *buf, size_t cap) {
    switch (ed.tool) {
    case T_DRAW:
        if (ed.draw.n) snprintf(buf, cap, "Draw sector \xC2\xB7 %d point%s", ed.draw.n, ed.draw.n == 1 ? "" : "s");
        else snprintf(buf, cap, "Tap to place points");
        return buf;
    case T_LINES: snprintf(buf, cap, ed.draw.n ? "Lines \xC2\xB7 %d points" : "Tap to place points", ed.draw.n); return buf;
    case T_RECT: return "Drag to draw a rectangle";
    case T_POLY: snprintf(buf, cap, "Drag out a %d-sided shape", ed.poly_sides); return buf;
    case T_THING: snprintf(buf, cap, "Tap to place: %s", g_thing_info[ed.thing_type].name); return buf;
    case T_PAINT: return "Tap floors & walls to paint";
    case T_VERTEX: return "Tap a line to add a vertex";
    case T_SPLIT: return "Tap a line to split it";
    case T_MOVE: return "Drag to move the selection";
    case T_ROTATE: return "Drag to rotate the selection";
    case T_SCALE: return "Drag to scale the selection";
    case T_MEASURE: return "Drag to measure";
    case T_PAN: return "Drag to pan";
    default: return NULL;
    }
}

static void loupe(const Input *in) {
    if (!M.loupe || ed.mode != MODE_2D) return;
    float R = 62, cx = M.loupe_x, cy = M.loupe_y - 118;
    if (cy - R < g_vp.y + 4) cy = M.loupe_y + 118;        /* flip below near the top edge */
    cx = SF_CLAMP(cx, g_vp.x + R + 4, g_vp.x + g_vp.w - R - 4);
    Rect box = rect(cx - R, cy - R, R * 2, R * 2);
    double zoom = SF_MIN(ed.zoom * 2.6, 32.0);
    sfgl_flush();
    /* circular mask in the stencil buffer */
    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gfx_circle(cx, cy, R, col(255, 255, 255, 255));
    sfgl_flush();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    gfx_rect(box, C(TH_VIEW));
    ed2d_render_view(box, in, ed.snap_pt.x, ed.snap_pt.y, zoom);
    /* crosshair on the snap point */
    Col cc = ed.snap_kind == 2 ? C(TH_GREEN) : ed.snap_kind == 3 ? C(TH_BLUE) : C(0x3DDC84);
    gfx_line(cx - R, cy, cx - 9, cy, 1.5f, cc); gfx_line(cx + 9, cy, cx + R, cy, 1.5f, cc);
    gfx_line(cx, cy - R, cx, cy - 9, 1.5f, cc); gfx_line(cx, cy + 9, cx, cy + R, 1.5f, cc);
    gfx_rect_line(rect(cx - 7, cy - 7, 14, 14), 2, cc);
    char t[48];
    snprintf(t, sizeof t, "%.0f, %.0f", ed.snap_pt.x, ed.snap_pt.y);
    float tw = gfx_text_w(FONT_MONO_SMALL, t) + 12;
    gfx_rrect(rect(cx - tw * 0.5f, cy + R * 0.45f, tw, 20), 6, col(0, 0, 0, 170));
    gfx_text_center(FONT_MONO_SMALL, rect(cx - tw * 0.5f, cy + R * 0.45f, tw, 20), t, C(0xF1F3F6));
    sfgl_flush();
    glDisable(GL_STENCIL_TEST);
    gfx_circle_line(cx, cy, R, 3, C(MS_ACC));
    gfx_circle_line(cx, cy, R + 1.5f, 1, col(0, 0, 0, 120));
}

static void overlays_2d(Rect vp, const Input *in) {
    ui.layer = LAYER_POPUP;
    float x = vp.x + 12, y = vp.y + 10;
    char t[64];
    snprintf(t, sizeof t, "Grid %d", ed.grid);
    float w = chip_w(IC_grid, t, true);
    region(rect(x, y, w, 44));
    if (chip(ID("c_grid"), rect(x, y, w, 44), IC_grid, t, false, true)) open_bsheet(BS_GRID);
    x += w + 8;
    w = chip_w(IC_magnet, "Snap", false);
    region(rect(x, y, w, 44));
    if (chip(ID("c_snap"), rect(x, y, w, 44), IC_magnet, "Snap", ed.snap, false)) ed.snap = !ed.snap;
    x += w + 8;
    snprintf(t, sizeof t, "%.0f%%", ed.zoom * 100);
    w = chip_w(IC_zoom_in, t, false);
    region(rect(x, y, w, 44));
    if (chip(ID("c_zoom"), rect(x, y, w, 44), IC_zoom_in, t, false, false)) ed_fit_view();
    /* tool hint */
    char hb[96];
    const char *hint = tool_hint(hb, sizeof hb);
    if (hint && !sel_total()) {
        float hw = gfx_text_w(FONT_UI_BOLD, hint) + 30;
        Rect hr = rect(vp.x + 12, vp.y + 62, hw, 38);
        gfx_rrect(hr, 10, CA(0x1A1206, 235));
        gfx_rrect_line(hr, 10, 1, C(0x7A4116));
        gfx_text(FONT_UI_BOLD, hr.x + 15, hr.y + (hr.h - gfx_font_h(FONT_UI_BOLD)) * 0.5f, hint, C(MS_ACC_ON));
    }
    /* selection mode (bottom left) */
    float by = vp.y + vp.h - 60 - in->inset_b;
    Rect sm = rect(vp.x + 12, by, 4 * 52 + 4, 52);
    region(sm);
    gfx_rrect(sm, 26, CA(0x101114, 240));
    gfx_rrect_line(sm, 26, 1, C(MS_OUTLINE));
    static const int smi[4] = { IC_vertex, IC_line, IC_sector, IC_thing };
    for (int i = 0; i < 4; i++) {
        Rect b = rect(sm.x + 2 + i * 52, sm.y + 2, 52, 48);
        bool held, on = ed.selmode == i;
        if (tap(IDI("selm", i), b, &held)) ed_set_selmode(i);
        if (on) gfx_rrect(b, 24, C(0x4A2A12));
        ripple(b, 24, held);
        gfx_icon(smi[i], b.x + 26, b.y + 24, 20, on ? C(0xFFD2B0) : C(MS_ON2));
    }
    /* drawing bar */
    bool drawing = (ed.tool == T_DRAW || ed.tool == T_LINES) && ed.draw.n > 0;
    bool rect2 = ed.tool == T_RECT && ed.drawing;
    float cx = vp.x + vp.w * 0.5f;
    if (drawing || rect2) {
        bool lines = ed.tool == T_LINES;
        const char *fin = lines ? "Finish" : "Close shape";
        float fw = gfx_text_w(FONT_UI_BOLD, fin) + 70;
        float bw = 156 + 120 + fw + 16;
        if (rect2) bw = 136;
        Rect bar = rect(cx - bw * 0.5f, vp.y + vp.h - 70 - in->inset_b, bw, 62);
        if (bar.x < sm.x + sm.w + 8) bar.x = sm.x + sm.w + 8;
        region(bar);
        gfx_shadow(bar, 31, 14, col(0, 0, 0, 140));
        gfx_rrect(bar, 31, CA(0x1B1D22, 248));
        float bx = bar.x + 6;
        if (rect2) {
            if (mbutton(ID("dr_cancel2"), rect(bx, bar.y + 6, 124, 50), IC_close, "Cancel", MB_TEXT)) ed.drawing = false;
        } else {
            if (mbutton(ID("dr_undo"), rect(bx, bar.y + 6, 150, 50), IC_undo, "Undo point", MB_TEXT)) { if (ed.draw.n) ed.draw.n--; if (!ed.draw.n) ed.drawing = false; }
            bx += 154;
            if (mbutton(ID("dr_cancel"), rect(bx, bar.y + 6, 116, 50), IC_close, "Cancel", MB_TEXT)) { ed.drawing = false; arr_clear(ed.draw); }
            bx += 120;
            bool can = lines ? ed.draw.n >= 2 : ed.draw.n >= 3;
            if (mbutton(ID("dr_close"), rect(bx, bar.y + 6, fw, 50), IC_check, fin, can ? MB_FILLED : MB_TONAL) && can) {
                ed2d_finish_draw(!lines);
                haptic(HAPTIC_CONFIRM);
            }
        }
    } else if (sel_total() && ed.drag == DRAG_NONE) {
        /* floating actions for the selection */
        static const int ic[7] = { IC_copy, IC_cut, IC_trash, IC_join, IC_flip, IC_rotate, IC_settings };
        int n = 7;
        float bw = n * 52 + 52 + 12;
        Rect bar = rect(cx - bw * 0.5f, vp.y + 10, bw, 56);
        if (bar.x < x + w + 8) bar.x = x + w + 8;
        if (bar.x + bar.w > vp.x + vp.w - 8) {   /* no room beside the chips: second row */
            bar.y = vp.y + 62;
            bar.x = SF_CLAMP(cx - bw * 0.5f, vp.x + 8, vp.x + vp.w - 8 - bw);
            if (bar.x < vp.x + 8) bar.x = vp.x + 8;
        }
        region(bar);
        gfx_shadow(bar, 28, 12, col(0, 0, 0, 120));
        gfx_rrect(bar, 28, CA(0x1B1D22, 245));
        for (int i = 0; i < n; i++) {
            Rect b = rect(bar.x + 6 + i * 52, bar.y + 4, 48, 48);
            bool on = i == 6 && M.sheet_open && M.sheet_tab == SHEET_PROPS;
            if (!icon_button(IDI("fa", i), b, ic[i], on, 22)) continue;
            switch (i) {
            case 0: ed_duplicate(); break;
            case 1: ed_copy(true); break;
            case 2: ed_delete(); break;
            case 3: if (ed.selmode == SM_LINES) ed_flip_lines(); else ed_join(false); break;
            case 4: ed_mirror(false); break;
            case 5: ed_rotate90(true); break;
            case 6: if (on) M.sheet_open = false; else { M.sheet_open = true; M.sheet_tab = SHEET_PROPS; } break;
            }
        }
        Rect kb = rect(bar.x + 6 + n * 52, bar.y + 4, 48, 48);
        bool held;
        if (tap(ID("fa_more"), kb, &held)) open_actions(cx, vp.y + 40);
        ripple(kb, 24, held);
        kebab(kb.x + 24, kb.y + 24, C(MS_ON));
    }
    /* sheet handle */
    if (!M.sheet_open) {
        Rect h = rect(vp.x + vp.w - 26, vp.y + vp.h * 0.5f - 44, 26, 88);
        region(h);
        bool held;
        if (tap(ID("sh_handle"), h, &held)) { M.sheet_open = true; }
        gfx_rrect(rect(h.x, h.y, h.w + 14, h.h), 14, held ? C(MS_SURF3) : CA(0x1B1D22, 235));
        gfx_icon(IC_chev_left, h.x + 13, h.y + 44, 18, C(MS_ON));
    }
}

static void overlays_3d(Rect vp, const Input *in) {
    ui.layer = LAYER_POPUP;
    Map *m = &ed.map;
    float x = vp.x + 12, y = vp.y + 10;
    /* selection label */
    char t[96];
    int nf = 0, nc = 0, nw = 0, nt = 0;
    for (int i = 0; i < ed.sel3d.n; i++) {
        int k = SURF_KIND(ed.sel3d.a[i]);
        if (k == SURF_FLOOR) nf++; else if (k == SURF_CEIL) nc++; else if (k == SURF_THING) nt++; else nw++;
    }
    int icon = IC_select;
    if (!ed.sel3d.n) snprintf(t, sizeof t, ed.tool3d == T3_PAINT ? "Tap surfaces to paint" : ed.tool3d == T3_PICK ? "Tap a surface to copy its texture" : "Tap a surface");
    else if (ed.sel3d.n == 1) {
        uint32_t id = ed.sel3d.a[0];
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        if (k == SURF_FLOOR || k == SURF_CEIL) {
            const char *nm = m->sectors.a[idx].name;
            char sn[40];
            if (nm[0]) snprintf(sn, sizeof sn, "%s", nm); else snprintf(sn, sizeof sn, "Sector %d", idx);
            snprintf(t, sizeof t, "%s \xC2\xB7 %s", k == SURF_FLOOR ? "Floor" : "Ceiling", sn);
            icon = k == SURF_FLOOR ? IC_floor : IC_ceiling;
        } else if (k == SURF_THING) { snprintf(t, sizeof t, "%s", g_thing_info[m->things.a[idx].type].name); icon = IC_thing; }
        else { snprintf(t, sizeof t, "Wall \xC2\xB7 line %d", idx / 2); icon = IC_wall; }
    } else snprintf(t, sizeof t, "%d surfaces", ed.sel3d.n);
    float w = chip_w(icon, t, false);
    region(rect(x, y, w, 44));
    if (chip(ID("c3_sel"), rect(x, y, w, 44), icon, t, ed.sel3d.n > 0, false) && ed.sel3d.n) { M.sheet_open = true; M.sheet_tab = SHEET_PROPS; }
    x += w + 8;
    const char *lit = ed.fullbright ? "Fullbright" : "Lit";
    w = chip_w(IC_light, lit, false);
    region(rect(x, y, w, 44));
    if (chip(ID("c3_lit"), rect(x, y, w, 44), IC_light, lit, ed.fullbright, false)) ed.fullbright = !ed.fullbright;
    bool flats = nf + nc > 0;
    bool dial = flats || (ed.tool3d == T3_LIGHT && ed.sel3d.n) || (nw > 0 && ed.tool3d == T3_HEIGHT);
    float dial_w = 70;
    float right = vp.x + vp.w - 10;
    /* height card */
    if (dial) {
        uint32_t id = ed.sel3d.a[0];
        for (int i = 0; i < ed.sel3d.n; i++) { int k = SURF_KIND(ed.sel3d.a[i]); if (k == SURF_FLOOR || k == SURF_CEIL) { id = ed.sel3d.a[i]; break; } }
        int k = SURF_KIND(id), idx = SURF_IDX(id);
        float cur = 0;
        const char *what = "HEIGHT";
        if (ed.tool3d == T3_LIGHT) {
            int s = k == SURF_FLOOR || k == SURF_CEIL ? idx : (k >= SURF_UPPER && k <= SURF_LOWER ? m->lines.a[idx / 2].side[idx & 1].sector : -1);
            cur = s >= 0 ? (float)m->sectors.a[s].light : 0; what = "LIGHT LEVEL";
        } else if (k == SURF_FLOOR) { cur = m->sectors.a[idx].floor_h; what = "FLOOR HEIGHT"; }
        else if (k == SURF_CEIL) { cur = m->sectors.a[idx].ceil_h; what = "CEILING HEIGHT"; }
        else if (k >= SURF_UPPER && k <= SURF_LOWER) { cur = m->lines.a[idx / 2].side[idx & 1].offy; what = "TEXTURE OFFSET Y"; }
        bool recent = in->time - M.h_change_t < 2.5;
        Rect hc = rect(vp.x + 12, vp.y + 64, 200, 92);
        gfx_rrect(hc, 16, CA(0x140D07, 235));
        gfx_rrect_line(hc, 16, 1, C(0x7A4116));
        gfx_text(FONT_SMALL, hc.x + 16, hc.y + 12, what, C(MS_ACC_ON));
        char v[48];
        if (recent && M.h_from != cur) snprintf(v, sizeof v, "%g \xE2\x86\x92 %g", M.h_from, cur);
        else snprintf(v, sizeof v, "%g", cur);
        gfx_text(FONT_HEAD, hc.x + 16, hc.y + 32, v, C(0xF1F3F6));
        char s2[48];
        snprintf(s2, sizeof s2, "swipe the dial \xC2\xB7 step %d", ed.tool3d == T3_LIGHT ? 16 : ed.hstep);
        gfx_text(FONT_SMALL, hc.x + 16, hc.y + 66, s2, C(0xC9A98F));
        /* step chips */
        static const int st[] = { 1, 4, 8, 16, 32 };
        float sw = 5 * 44 + 64;
        Rect sr = rect(right - dial_w - 10 - sw, vp.y + 10, sw, 48);
        if (sr.x < x + w + 8) sr = rect(vp.x + 12, vp.y + 164, sw, 48);   /* narrow view: under the height card */
        region(sr);
        gfx_rrect(sr, 24, CA(0x101114, 240));
        gfx_rrect_line(sr, 24, 1, C(MS_OUTLINE));
        gfx_text(FONT_UI, sr.x + 16, sr.y + 24 - gfx_font_h(FONT_UI) * 0.5f, "Step", C(MS_ON2));
        for (int i = 0; i < 5; i++) {
            Rect b = rect(sr.x + 60 + i * 44, sr.y + 4, 40, 40);
            bool held, on = ed.hstep == st[i];
            char l[8];
            snprintf(l, sizeof l, "%d", st[i]);
            if (tap(IDI("hs", i), b, &held)) ed.hstep = st[i];
            if (on) gfx_circle(b.x + 20, b.y + 20, 19, C(MS_ACC));
            else if (held) gfx_circle(b.x + 20, b.y + 20, 19, col(255, 255, 255, 24));
            gfx_text_center(FONT_UI_BOLD, b, l, on ? C(0x1A0D02) : C(MS_ON));
        }
        /* the dial */
        Rect d = rect(right - dial_w, vp.y + 10, dial_w, vp.h - 20 - in->inset_b);
        region(d);
        gfx_rrect(d, dial_w * 0.5f, CA(0x0E0F12, 235));
        gfx_rrect_line(d, dial_w * 0.5f, 1, C(MS_OUTLINE));
        Rect up = rect(d.x, d.y + 4, d.w, 52), dn = rect(d.x, d.y + d.h - 56, d.w, 52);
        bool hu, hd;
        float before = cur;
        if (tap(ID("dial_up"), up, &hu)) { ed3d_adjust(1, false); if (!recent) M.h_from = before; M.h_change_t = in->time; }
        if (tap(ID("dial_dn"), dn, &hd)) { ed3d_adjust(-1, false); if (!recent) M.h_from = before; M.h_change_t = in->time; }
        ripple(up, 26, hu); ripple(dn, 26, hd);
        gfx_icon(IC_chev_up, up.x + up.w * 0.5f, up.y + 26, 22, C(MS_ON));
        gfx_icon(IC_chev_down, dn.x + dn.w * 0.5f, dn.y + 26, 22, C(MS_ON));
        Rect track = rect(d.x, d.y + 58, d.w, d.h - 116);
        bool held;
        uint32_t did = ID("dial");
        tap(did, track, &held);
        float stepv = ed.tool3d == T3_LIGHT ? 16.0f : (float)ed.hstep;
        if (ui.active == did && ui.in->mdown[0]) {
            if (!M.dial_drag) { M.dial_drag = true; M.dial_acc = 0; M.dial_prev = ui.in->my; if (!recent) M.h_from = cur; }
            M.dial_acc += SF_CLAMP(M.dial_prev - ui.in->my, -80, 80);
            while (M.dial_acc >= 16) { ed3d_adjust(1, false); M.dial_acc -= 16; M.h_change_t = in->time; haptic(HAPTIC_TICK); }
            while (M.dial_acc <= -16) { ed3d_adjust(-1, false); M.dial_acc += 16; M.h_change_t = in->time; haptic(HAPTIC_TICK); }
        } else M.dial_drag = false;
        M.dial_prev = ui.in->my;
        /* ticks scroll with the value */
        gfx_clip_push(track);
        float mid = track.y + track.h * 0.5f, px_per = 16.0f / stepv;
        float base = floorf(cur / stepv) * stepv;
        for (int k2 = -30; k2 <= 30; k2++) {
            float val = base + k2 * stepv;
            float ty = mid - (val - cur) * px_per;
            if (ty < track.y - 10 || ty > track.y + track.h + 10) continue;
            bool major = fmodf(fabsf(val), stepv * 4) < 0.01f;
            gfx_rect(rect(d.x + d.w - (major ? 34 : 24), ty, major ? 22 : 12, 1.5f), major ? C(0xC9CFD8) : C(0x5A616D));
            if (major) {
                char l[16];
                snprintf(l, sizeof l, "%g", val);
                gfx_text(FONT_MONO_SMALL, d.x + 8, ty - 8, l, C(MS_ON3));
            }
        }
        gfx_clip_pop();
        Rect bub = rect(d.x - 6, mid - 26, d.w + 12, 52);
        gfx_rrect(bub, 26, M.dial_drag ? C(0xFF9A4D) : C(MS_ACC));
        char cv[16];
        snprintf(cv, sizeof cv, "%g", cur);
        gfx_text_center(FONT_HEAD, bub, cv, C(0x1A0D02));
        right = d.x - 10;
    }
    /* joystick + up/down */
    Rect bu, bd;
    float sbx, sby, srad;
    ed3d_buttons(vp, in, &bu, &bd, &sbx, &sby, &srad);
    gfx_circle(sbx, sby, srad + 6, col(10, 11, 13, 110));
    gfx_circle_line(sbx, sby, srad + 6, 1.5f, col(255, 255, 255, 70));
    float kx = sbx + (M.stick_on ? M.stick_x : 0), ky = sby + (M.stick_on ? M.stick_y : 0);
    gfx_circle(kx, ky, 24, col(220, 224, 232, M.stick_on ? 150 : 90));
    gfx_circle_line(kx, ky, 24, 1.5f, col(255, 255, 255, 160));
    gfx_text_center(FONT_SMALL, rect(sbx - 40, sby + srad + 8, 80, 16), "Move", col(255, 255, 255, 150));
    bool lift_up = false, lift_dn = false;
    for (int i = 0; i < 10; i++) if (M.F[i].used && M.F[i].owner == OWN_BTN) { if (M.F[i].btn == PB_UP) lift_up = true; if (M.F[i].btn == PB_DOWN) lift_dn = true; }
    gfx_circle(bu.x + 24, bu.y + 24, 24, lift_up ? C(MS_ACC_CT) : col(10, 11, 13, 200));
    gfx_icon(IC_chev_up, bu.x + 24, bu.y + 24, 22, C(MS_ON));
    gfx_circle(bd.x + 24, bd.y + 24, 24, lift_dn ? C(MS_ACC_CT) : col(10, 11, 13, 200));
    gfx_icon(IC_chev_down, bd.x + 24, bd.y + 24, 22, C(MS_ON));
    /* recent textures strip */
    float lw = gfx_text_w(FONT_UI_BOLD, "Library") + 64;
    float avail = right - (bd.x + bd.w + 14);
    int nshow = SF_CLAMP((int)((avail - lw - 16) / 56), 0, SF_MIN(M.nrecent, 6));
    float sw = nshow * 56 + lw + 16;
    float sx = SF_MAX(bd.x + bd.w + 14, vp.x + (vp.w - sw) * 0.5f);
    if (sx + sw > right) sx = right - sw;
    Rect strip = rect(sx, vp.y + vp.h - 70 - in->inset_b, sw, 62);
    region(strip);
    gfx_rrect(strip, 31, CA(0x101114, 240));
    gfx_rrect_line(strip, 31, 1, C(MS_OUTLINE));
    const char *curt = ed.sel3d.n ? ed3d_surface_texture(ed.sel3d.a[0]) : NULL;
    for (int i = 0; i < nshow; i++) {
        Rect b = rect(strip.x + 10 + i * 56, strip.y + 7, 48, 48);
        bool held;
        if (tap(IDI("rt", i), b, &held)) {
            char nm[TEXNAME];
            sf_strcpy(nm, sizeof nm, M.recent[i]);
            if (ed.tool3d == T3_PICK || ed.tool3d == T3_PAINT || !ed.sel3d.n) {
                int cat = tex_find(nm) >= 0 ? g_assets.tex.a[tex_find(nm)].cat : TEXCAT_WALL;
                sf_strcpy(cat == TEXCAT_WALL ? ed.cur_wall : ed.cur_flat, TEXNAME, nm);
                ed_toast("Paint texture set", 0);
            } else ed_apply_texture(nm);
            push_recent(nm);
        }
        tex_tile(M.recent[i], b);
        bool on = curt && !strcmp(curt, M.recent[i]);
        if (on) gfx_rrect_line(rect(b.x - 3, b.y - 3, b.w + 6, b.h + 6), 9, 2.5f, C(MS_ACC));
        if (held) gfx_rect(b, col(255, 255, 255, 40));
    }
    if (mbutton(ID("rt_lib"), rect(strip.x + 10 + nshow * 56, strip.y + 7, lw, 48), IC_texture, "Library", MB_TEXT)) { M.sheet_open = true; M.sheet_tab = SHEET_TEX; }
}

static void overlays_play(Rect vp, const Input *in) {
    ui.layer = LAYER_POPUP;
    Rect fire, use, jump, crouch, pause;
    play_buttons(in->win_w, in->win_h, in, &fire, &use, &jump, &crouch, &pause);
    region(pause);
    bool held;
    if (tap(ID("pl_pause"), pause, &held)) { M.paused = true; open_bsheet(BS_PAUSE); }
    gfx_circle(pause.x + 26, pause.y + 26, 26, held ? C(MS_SURF3) : col(10, 11, 13, 200));
    gfx_rect(rect(pause.x + 19, pause.y + 17, 5, 18), C(MS_ON));
    gfx_rect(rect(pause.x + 28, pause.y + 17, 5, 18), C(MS_ON));
    if (ed.play.dead || ed.play.exited) {
        Rect full = rect(0, 0, in->win_w, in->win_h);
        region(full);
        if (tap(ID("pl_again"), full, &held)) play_start(false);
        return;
    }
    bool hf = false, hu = false, hj = false, hc = false;
    for (int i = 0; i < 10; i++) if (M.F[i].used && M.F[i].owner == OWN_BTN) {
        if (M.F[i].btn == PB_FIRE) hf = true;
        if (M.F[i].btn == PB_USE) hu = true;
        if (M.F[i].btn == PB_JUMP) hj = true;
        if (M.F[i].btn == PB_CROUCH) hc = true;
    }
    struct { Rect r; const char *l; int ic; bool held; bool accent; } b[4] = {
        { fire, "FIRE", IC_crosshair, hf, true }, { use, "USE", IC_door, hu, false }, { jump, "JUMP", IC_chev_up, hj, false }, { crouch, "CROUCH", IC_chev_down, hc, false } };
    for (int i = 0; i < 4; i++) {
        float cx = b[i].r.x + b[i].r.w * 0.5f, cy = b[i].r.y + b[i].r.h * 0.5f, rr = b[i].r.w * 0.5f;
        if (b[i].accent) gfx_circle(cx, cy, rr, b[i].held ? col(255, 154, 77, 235) : col(255, 122, 26, 200));
        else gfx_circle(cx, cy, rr, b[i].held ? col(255, 255, 255, 70) : col(10, 11, 13, 110));
        gfx_circle_line(cx, cy, rr, 2, col(255, 255, 255, b[i].accent ? 120 : 150));
        Col fg = b[i].accent ? C(0x1A0D02) : C(0xFFFFFF);
        gfx_icon(b[i].ic, cx, cy - (b[i].accent ? 10 : 8), b[i].accent ? 30 : 22, fg);
        gfx_text_center(FONT_SMALL, rect(cx - rr, cy + (b[i].accent ? 12 : 8), rr * 2, 16), b[i].l, fg);
    }
    /* floating stick */
    if (M.stick_on) {
        gfx_circle(M.stick_bx, M.stick_by, 66, col(10, 11, 13, 90));
        gfx_circle_line(M.stick_bx, M.stick_by, 66, 1.5f, col(255, 255, 255, 90));
        gfx_circle(M.stick_bx + M.stick_x, M.stick_by + M.stick_y, 26, col(220, 224, 232, 140));
    } else {
        float gx = in->inset_l + 110, gy = in->win_h - in->inset_b - 110;
        gfx_circle_line(gx, gy, 66, 1.5f, col(255, 255, 255, 45));
        gfx_circle(gx, gy, 26, col(220, 224, 232, 50));
        gfx_text_center(FONT_SMALL, rect(gx - 40, gy + 72, 80, 16), "Move", col(255, 255, 255, 110));
    }
}

/* ================================================================== editor screen */
static void handle_back(void) {
    if (M.bsheet == BS_PAUSE) { M.paused = false; close_bsheet(); return; }
    if (M.bsheet != BS_NONE) { close_bsheet(); return; }
    if (M.state == ST_HOME) {
        if (M.home_tab != HOME_PROJECTS) { M.home_tab = HOME_PROJECTS; return; }
        plat_quit();
        return;
    }
    if (M.state != ST_EDITOR) return;
    if (ed.mode == MODE_PLAY) { M.paused = true; open_bsheet(BS_PAUSE); return; }
    if (M.sheet_open) { M.sheet_open = false; return; }
    if (ed.drawing || ed.draw.n) { ed.drawing = false; arr_clear(ed.draw); return; }
    if (ed.mode == MODE_3D) { ed_set_mode(MODE_2D); return; }
    leave_editor();
}

/* Bluetooth / DeX keyboards: the common desktop shortcuts */
static void keyboard_shortcuts(const Input *in) {
    if (M.bsheet != BS_NONE) return;
    if (in->ctrl) {
        if (in->kpressed['Z']) { if (in->shift) ed_redo(); else ed_undo(); }
        if (in->kpressed['Y']) ed_redo();
        if (in->kpressed['S']) { if (ed_save()) ed_toast("Saved", 1); }
        if (ed.mode == MODE_2D) {
            if (in->kpressed['C']) ed_copy(false);
            if (in->kpressed['X']) ed_copy(true);
            if (in->kpressed['V']) ed_paste();
            if (in->kpressed['D']) ed_duplicate();
            if (in->kpressed['A']) ed_select_all();
        }
        return;
    }
    if (in->kpressed[KEY_F5]) { M.paused = false; ed_set_mode(ed.mode == MODE_PLAY ? MODE_2D : MODE_PLAY); }
    if (ed.mode == MODE_PLAY) return;
    if (in->kpressed[KEY_TAB]) ed_set_mode(ed.mode == MODE_2D ? MODE_3D : MODE_2D);
    if (ed.mode == MODE_2D) {
        if (in->kpressed[KEY_DELETE]) ed_delete();
        if (in->kpressed[KEY_HOME]) ed_fit_view();
        if (in->kpressed[KEY_LBRACKET]) ed_grid_step(-1);
        if (in->kpressed[KEY_RBRACKET]) ed_grid_step(1);
        if (in->kpressed['S'] && !in->shift) ed_set_tool(T_SELECT);
        if (in->kpressed['D']) ed_set_tool(T_DRAW);
        if (in->kpressed['R']) ed_set_tool(T_RECT);
        if (in->kpressed['L']) ed_set_tool(T_LINES);
        if (in->kpressed['T']) ed_set_tool(T_THING);
    }
}

static void editor_screen(const Input *in) {
    float W = in->win_w, H = in->win_h;
    sel_gather(&g_sel);
    bool play = ed.mode == MODE_PLAY;
    float sheet_w = SF_MIN(360, SF_MAX(300, W * 0.36f));
    Rect top = rect(0, 0, W, TOPBAR_H + in->inset_t);
    Rect rl = rect(in->inset_l, top.h, RAIL_W, H - top.h);
    Rect vp;
    if (play) vp = rect(0, 0, W, H);
    else {
        vp = rect(rl.x + rl.w, top.h, W - rl.x - rl.w - in->inset_r, H - top.h);
        if (M.sheet_open) vp.w -= sheet_w;
    }
    g_vp = vp;
    keyboard_shortcuts(in);
    /* the map engine draws first, at the base layer, using the routed input */
    ui.layer = LAYER_BASE;
    Input *v = &M.vin;
    if (M.paused || M.bsheet != BS_NONE) {
        v->vmove_x = v->vmove_y = v->vlook_x = v->vlook_y = v->vlift = 0;
        v->vfire = v->vjump = v->vuse = v->vcrouch = false;
    }
    switch (ed.mode) {
    case MODE_2D: ed2d_frame(vp, v); break;
    case MODE_3D: ed3d_frame(vp, v); break;
    default: if (M.paused) { Input z = *v; z.dt = 0; memset(z.kpressed, 0, sizeof z.kpressed); play_frame(vp, &z); } else play_frame(vp, v); break;
    }
    gfx_begin_2d(W, H, in->scale);
    if (ed.mode == MODE_PLAY) {
        overlays_play(vp, in);
    } else {
        if (ed.mode == MODE_2D) { overlays_2d(vp, in); loupe(v); }
        else overlays_3d(vp, in);
        if (M.sheet_open) side_sheet(rect(vp.x + vp.w, top.h, W - vp.x - vp.w - in->inset_r + in->inset_r, H - top.h), in);
        rail(rl, in);
        if (in->inset_l > 0) { gfx_rect(rect(0, top.h, in->inset_l, H - top.h), C(0x0B0C0E)); }
        top_bar(top, in);
        toast(vp.x + vp.w * 0.5f, H - in->inset_b - (ed.mode == MODE_3D ? 84 : 80), in);
    }
    if (ed.mode == MODE_PLAY) toast(W * 0.5f, H * 0.32f, in);
    /* autosave */
    if (g_settings.autosave && ed.dirty && ed.mode != MODE_PLAY && in->time - M.last_autosave > 45 && ed.drag == DRAG_NONE) {
        ed_save();
        M.last_autosave = in->time;
    }
    if (!ed.dirty) M.last_autosave = in->time;
}

/* ================================================================== async results */
static void poll_async(void) {
    int req;
    char buf[128];
    while (plat_text_input_poll(&req, buf, sizeof buf)) {
        M.txt_req = TXT_NONE;
        switch (req) {
        case TXT_PROJECT_NAME: {
            char clean[64];
            sf_strcpy(clean, sizeof clean, buf);
            sf_sanitize_name(clean);
            if (!clean[0]) { ed_toast("The project needs a name", 2); break; }
            create_project(clean, M.new_tpl);
        } break;
        case TXT_MAP_NAME:
            if (!buf[0]) break;
            close_bsheet();
            ed_new_map(buf, M.new_tpl);
            break;
        case TXT_SECTOR_NAME:
            if (ed.open && g_sel.sec.n == 1 && g_sel.sec.a[0] < ed.map.sectors.n) {
                ed_snapshot("Rename sector");
                sf_strcpy(ed.map.sectors.a[g_sel.sec.a[0]].name, sizeof ed.map.sectors.a[0].name, buf);
            }
            break;
        case TXT_TEX_SEARCH: sf_strcpy(M.tex_search, sizeof M.tex_search, buf); M.sheet_scroll[SHEET_TEX] = 0; break;
        case TXT_PROJ_SEARCH: sf_strcpy(M.proj_filter, sizeof M.proj_filter, buf); break;
        case TXT_NUMBER: {
            char *end = NULL;
            float v = strtof(buf, &end);
            if (end && end != buf && ed.open) apply_number(M.txt_num, v);
        } break;
        default: break;
        }
    }
    char path[600];
    while (plat_pick_image_poll(path, sizeof path)) {
        if (!ed.open) continue;
        char td[1024], name[TEXNAME];
        project_tex_dir(&ed.proj, td, sizeof td);
        plat_mkdirs(td);
        if (assets_import_texture(path, td, name)) {
            char msg[96];
            snprintf(msg, sizeof msg, "Imported %s", name);
            ed_toast(msg, 1);
            push_recent(name);
            M.tex_filter = 4;
        } else ed_toast("That image could not be imported", 2);
    }
}

/* ================================================================== platform callbacks */
bool app_init(void) {
    settings_load();
    gfx_init();
    map_init(&ed.map);
    map_init(&ed.clip);
    M.haptics = true;
    M.proj_sel = -1;
    char flag[600], ad[512];
    plat_appdata_dir(ad, sizeof ad);
    sf_path_join(flag, sizeof flag, ad, "guide_seen");
    if (!plat_exists(flag)) {
        M.first_run_guide = true;
        plat_mkdirs(ad);
        FILE *f = plat_fopen(flag, "wb");
        if (f) fclose(f);
    }
    plat_set_orientation(ORIENT_PORTRAIT);
    set_state(ST_SPLASH);
    sf_strcpy(M.status, sizeof M.status, "Starting");
    return true;
}

void app_frame(const Input *in) {
    if (fabsf(in->scale - M.scale) > 0.01f) build_fonts(in->scale);
    /* swap region / scroll lists (built last frame) */
    memcpy(M.reg, M.reg_next, sizeof M.reg);
    M.nreg = M.nreg_next;
    M.nreg_next = 0;
    memcpy(M.sreg, M.sreg_next, sizeof M.sreg);
    M.nsreg = M.nsreg_next;
    M.nsreg_next = 0;
    route(in);
    if (in->back) handle_back();
    poll_async();
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, (GLsizei)(in->win_w * in->scale + 0.5f), (GLsizei)(in->win_h * in->scale + 0.5f));
    glClearColor(0.055f, 0.06f, 0.07f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gfx_begin_2d(in->win_w, in->win_h, in->scale);
    ui_begin(&M.vin);
    M.fps_frames++;
    if (in->time - M.fps_t > 0.5) { ed.fps = (float)(M.fps_frames / (in->time - M.fps_t)); M.fps_frames = 0; M.fps_t = in->time; }
    switch (M.state) {
    case ST_SPLASH: splash(in); break;
    case ST_HOME: home(in); break;
    case ST_LOADING: loading(in); break;
    default: editor_screen(in); break;
    }
    bottom_sheets(in);
    ui.layer = LAYER_BASE;
    ui_end();
    sfgl_flush();
}

void app_suspend(void) {
    if (M.state == ST_EDITOR && ed.open && ed.dirty) ed_save();
    settings_save();
}

bool app_on_close(void) { app_suspend(); return true; }
int app_hit_test(float x, float y) { (void)x; (void)y; return 0; }
void app_shutdown(void) { app_suspend(); }
