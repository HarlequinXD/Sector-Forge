/* Sector Forge - immediate-mode UI toolkit */
#include "ui.h"

UI ui;
static bool g_focus_seen, g_wheel_used;
static uint32_t g_pending_id;
static int g_pending_res;

enum { POP_NONE, POP_DROPDOWN, POP_MENU };

static Col C(uint32_t h) { return col_hex(h); }

uint32_t ui_id(const char *s, int n) { return sf_hash(s, (uint32_t)n * 2654435761u + 7u) | 1u; }

void ui_begin(const Input *in) {
    ui.in = in;
    ui.mx = in->mx; ui.my = in->my;
    ui.time = in->time;
    memcpy(ui.blocks, ui.nblk, sizeof ui.blocks);
    memcpy(ui.block_layer, ui.nblk_layer, sizeof ui.block_layer);
    ui.nblocks = ui.nnblk;
    ui.nnblk = 0;
    ui.top_layer = 0;
    for (int i = 0; i < ui.nblocks; i++)
        if (rect_has(ui.blocks[i], ui.mx, ui.my) && ui.block_layer[i] > ui.top_layer) ui.top_layer = ui.block_layer[i];
    memcpy(ui.nocap, ui.nocap_next, sizeof ui.nocap);
    ui.nnocap = ui.nnocap_next;
    ui.nnocap_next = 0;
    ui.prev_hot = ui.hot;
    ui.hot = 0;
    ui.layer = 0;
    ui.tip = NULL;
    ui.cursor = CUR_ARROW;
    ui.result_id = g_pending_id;
    ui.result = g_pending_res;
    g_pending_id = 0;
    g_focus_seen = false;
    g_wheel_used = false;
    ui.any_hover_widget = false;
    ui.text_focus = ui.focus != 0;
}

void ui_push_block(Rect r, int layer) {
    if (ui.nnblk < 64) { ui.nblk[ui.nnblk] = r; ui.nblk_layer[ui.nnblk] = layer; ui.nnblk++; }
}

bool ui_mouse_free(void) { return ui.layer >= ui.top_layer; }

bool ui_hover(Rect r) {
    if (!rect_has(r, ui.mx, ui.my)) return false;
    if (ui.layer < ui.top_layer) return false;
    if (ui.popup_id && ui.layer < ui.popup_layer) {
        /* popup open this frame but not yet registered as a block */
        if (ui.popup_opened_now) return false;
    }
    return rect_has(gfx_clip_current(), ui.mx, ui.my);
}

void ui_tooltip(const char *text) {
    if (!text || !*text) return;
    sf_strcpy(ui.tipbuf, sizeof ui.tipbuf, text);
    ui.tip = ui.tipbuf;
}

void ui_nocaption(Rect r) { if (ui.nnocap_next < 96) ui.nocap_next[ui.nnocap_next++] = r; }
bool ui_is_caption_free(float x, float y) {
    for (int i = 0; i < ui.nnocap; i++) if (rect_has(ui.nocap[i], x, y)) return false;
    return true;
}

static bool behave(uint32_t id, Rect r, bool *hov, bool *held) {
    bool h = ui_hover(r);
    *hov = h;
    *held = false;
    if (h) { ui.hot = id; ui.any_hover_widget = true; }
    bool clicked = false;
    if (h && ui.in->mpressed[0] && !ui.active) ui.active = id;
    if (ui.active == id) {
        *held = true;
        if (ui.in->mreleased[0]) { clicked = h; ui.active = 0; }
    }
    return clicked;
}

static void draw_btn_content(Rect r, int icon, const char *label, Col fg, int font) {
    float isz = 16;
    float tw = label ? gfx_text_w(font, label) : 0;
    float total = (icon >= 0 ? isz : 0) + (label && icon >= 0 ? 6 : 0) + tw;
    float x = r.x + (r.w - total) * 0.5f;
    if (total > r.w - 12) x = r.x + 8;
    float cy = r.y + r.h * 0.5f;
    if (icon >= 0) { gfx_icon(icon, x + isz * 0.5f, cy, isz, fg); x += isz + 6; }
    if (label) gfx_text_fit(font, x, cy - gfx_font_h(font) * 0.5f, r.x + r.w - x - 6, label, fg);
}

bool ui_button_on(uint32_t id, Rect r, int icon, const char *label, int style, bool on) {
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    if (hov) ui.cursor = CUR_HAND;
    Col bg = col(0, 0, 0, 0), bd = col(0, 0, 0, 0), fg = C(TH_TEXT);
    int font = FONT_UI;
    switch (style) {
    case BTN_PRIMARY:
        bg = C(hov ? 0xFF8F3D : TH_ACCENT); if (held) bg = C(0xE86A10);
        fg = C(0x1A0D02); font = FONT_UI_BOLD; break;
    case BTN_GHOST:
        if (on) { bg = C(TH_ACCENT_DK); bd = C(TH_ACCENT_BD); fg = C(0xFF8A33); }
        else { if (hov) bg = C(TH_HOVER); fg = C(hov ? 0xFFFFFF : 0xB7BFCC); }
        break;
    case BTN_SEG:
        if (on) { bg = C(TH_ACCENT); fg = C(0x1A0D02); font = FONT_UI_BOLD; }
        else { if (hov) bg = C(TH_HOVER); fg = C(hov ? 0xFFFFFF : 0xB7BFCC); }
        break;
    case BTN_DANGER:
        bg = C(hov ? 0x3A1414 : TH_BTN); bd = C(hov ? 0x7A2A1E : TH_BORDER2); fg = C(0xFF8A7A); break;
    default:
        bg = C(hov ? 0x242830 : TH_BTN); bd = C(TH_BORDER2); if (held) bg = C(0x2B2F37);
        if (on) { bg = C(TH_ACCENT_DK); bd = C(TH_ACCENT_BD); fg = C(0xFFB27A); }
        break;
    }
    gfx_rrect(r, 6, bg);
    if (bd.a) gfx_rrect_line(r, 6, 1, bd);
    draw_btn_content(r, icon, label, fg, font);
    return click;
}

bool ui_button(uint32_t id, Rect r, int icon, const char *label, int style) { return ui_button_on(id, r, icon, label, style, false); }

bool ui_icon_btn(uint32_t id, Rect r, int icon, const char *tip, bool on) {
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    if (hov) { ui.cursor = CUR_HAND; ui_tooltip(tip); }
    Col fg = C(hov ? 0xFFFFFF : 0xB7BFCC);
    if (on) {
        gfx_rrect(r, 7, C(TH_ACCENT_DK));
        gfx_rrect_line(r, 7, 1, C(TH_ACCENT_BD));
        fg = C(0xFF8A33);
    } else if (hov) gfx_rrect(r, 7, C(held ? 0x2B2F37 : TH_HOVER));
    float sz = SF_MIN(r.w, r.h) * 0.52f;
    sz = SF_CLAMP(sz, 12, 22);
    gfx_icon(icon, r.x + r.w * 0.5f, r.y + r.h * 0.5f, sz, fg);
    return click;
}

bool ui_win_btn(uint32_t id, Rect r, int icon, bool danger) {
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    ui_nocaption(r);
    if (hov) gfx_rect(r, danger ? col_hex(0xC42B1C) : C(TH_HOVER));
    gfx_icon(icon, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 16, hov ? C(0xFFFFFF) : C(0xAEB5C1));
    return click;
}

/* ------------------------------------------------------------------ text editing */
static void edit_delete_sel(char *buf) {
    if (ui.edit_sel < 0) return;
    int a = SF_MIN(ui.edit_sel, ui.edit_cur), b = SF_MAX(ui.edit_sel, ui.edit_cur);
    memmove(buf + a, buf + b, strlen(buf + b) + 1);
    ui.edit_cur = a;
    ui.edit_sel = -1;
}

/* returns 1 changed, 2 enter, 3 escape */
static int edit_keys(char *buf, int cap) {
    const Input *in = ui.in;
    int len = (int)strlen(buf), r = 0;
    if (ui.edit_cur > len) ui.edit_cur = len;
    for (int i = 0; i < in->nchars; i++) {
        uint32_t c = in->chars[i];
        if (c < 32 || c == 127) continue;
        char enc[4];
        int n = 0;
        if (c < 0x80) enc[n++] = (char)c;
        else if (c < 0x800) { enc[n++] = (char)(0xC0 | (c >> 6)); enc[n++] = (char)(0x80 | (c & 63)); }
        else { enc[n++] = (char)(0xE0 | (c >> 12)); enc[n++] = (char)(0x80 | ((c >> 6) & 63)); enc[n++] = (char)(0x80 | (c & 63)); }
        edit_delete_sel(buf);
        len = (int)strlen(buf);
        if (len + n >= cap) continue;
        memmove(buf + ui.edit_cur + n, buf + ui.edit_cur, (size_t)(len - ui.edit_cur + 1));
        memcpy(buf + ui.edit_cur, enc, (size_t)n);
        ui.edit_cur += n;
        r = 1;
    }
    len = (int)strlen(buf);
    if (in->kpressed[KEY_BACK]) {
        if (ui.edit_sel >= 0) { edit_delete_sel(buf); r = 1; }
        else if (ui.edit_cur > 0) {
            int p = ui.edit_cur - 1;
            while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80) p--;
            memmove(buf + p, buf + ui.edit_cur, (size_t)(len - ui.edit_cur + 1));
            ui.edit_cur = p; r = 1;
        }
    }
    if (in->kpressed[KEY_DELETE]) {
        if (ui.edit_sel >= 0) { edit_delete_sel(buf); r = 1; }
        else if (ui.edit_cur < len) {
            int p = ui.edit_cur + 1;
            while (p < len && ((unsigned char)buf[p] & 0xC0) == 0x80) p++;
            memmove(buf + ui.edit_cur, buf + p, (size_t)(len - p + 1));
            r = 1;
        }
    }
    len = (int)strlen(buf);
    if (in->kpressed[KEY_LEFT]) { ui.edit_sel = -1; while (ui.edit_cur > 0) { ui.edit_cur--; if (((unsigned char)buf[ui.edit_cur] & 0xC0) != 0x80) break; } }
    if (in->kpressed[KEY_RIGHT]) { ui.edit_sel = -1; while (ui.edit_cur < len) { ui.edit_cur++; if (((unsigned char)buf[ui.edit_cur] & 0xC0) != 0x80) break; } }
    if (in->kpressed[KEY_HOME]) { ui.edit_sel = -1; ui.edit_cur = 0; }
    if (in->kpressed[KEY_END]) { ui.edit_sel = -1; ui.edit_cur = len; }
    if (in->ctrl && in->kpressed['A']) { ui.edit_sel = 0; ui.edit_cur = len; }
    if (in->kpressed[KEY_ENTER]) r = 2;
    if (in->kpressed[KEY_ESC]) r = 3;
    if (r == 1 || r == 2 || in->nchars) ui.caret_time = ui.time;
    return r;
}

static int caret_from_x(int font, const char *buf, float x) {
    int best = 0;
    float bd = 1e9f;
    const char *p = buf;
    while (1) {
        float w = gfx_text_wn(font, buf, (int)(p - buf));
        float d = fabsf(w - x);
        if (d < bd) { bd = d; best = (int)(p - buf); }
        if (!*p) break;
        utf8_next(&p);
    }
    return best;
}

static void draw_edit_text(int font, Rect r, float tx, const char *buf, bool focused) {
    float ty = r.y + (r.h - gfx_font_h(font)) * 0.5f;
    gfx_clip_push(rect(r.x + 2, r.y, r.w - 4, r.h));
    if (focused && ui.edit_sel >= 0) {
        int a = SF_MIN(ui.edit_sel, ui.edit_cur), b = SF_MAX(ui.edit_sel, ui.edit_cur);
        float x0 = tx + gfx_text_wn(font, buf, a), x1 = tx + gfx_text_wn(font, buf, b);
        gfx_rect(rect(x0, ty, x1 - x0, gfx_font_h(font)), col_hex(0x7A4116));
    }
    gfx_text(font, tx, ty, buf, C(TH_TEXT));
    if (focused && fmod(ui.time - ui.caret_time, 1.0) < 0.55) {
        float cx = tx + gfx_text_wn(font, buf, ui.edit_cur);
        gfx_rect(rect(cx, ty + 1, 1.5f, gfx_font_h(font) - 2), C(TH_ACCENT));
    }
    gfx_clip_pop();
}

int ui_text(uint32_t id, Rect r, char *buf, int cap, const char *ph, int icon) {
    bool hov = ui_hover(r);
    bool focused = ui.focus == id;
    int res = 0;
    if (hov) { ui.cursor = CUR_IBEAM; ui.hot = id; ui.any_hover_widget = true; }
    float tx = r.x + 8 + (icon >= 0 ? 20 : 0);
    if (ui.in->mpressed[0]) {
        if (hov && !ui.active) {
            if (!focused) { ui.focus = id; ui.edit_sel = 0; ui.edit_cur = (int)strlen(buf); focused = true; }
            else { ui.edit_sel = -1; ui.edit_cur = caret_from_x(FONT_UI, buf, ui.mx - tx); }
            ui.caret_time = ui.time;
        } else if (focused) { ui.focus = 0; focused = false; res = 2; }
    }
    if (focused) {
        g_focus_seen = true;
        int k = edit_keys(buf, cap);
        if (k == 1) res = 1;
        if (k == 2) { res = 2; ui.focus = 0; }
        if (k == 3) ui.focus = 0;
    }
    gfx_rrect(r, 6, C(TH_FIELD));
    gfx_rrect_line(r, 6, 1, focused ? C(TH_ACCENT_BD) : hov ? C(0x3A3F49) : C(TH_BORDER2));
    if (icon >= 0) gfx_icon(icon, r.x + 16, r.y + r.h * 0.5f, 14, C(TH_TEXT3));
    if (!buf[0] && !focused && ph) gfx_text_fit(FONT_UI, tx, r.y + (r.h - gfx_font_h(FONT_UI)) * 0.5f, r.w - (tx - r.x) - 6, ph, C(0x6F7783));
    else draw_edit_text(FONT_UI, r, tx, buf, focused);
    return res;
}

static void fmt_num(char *out, size_t cap, float v, int dec) {
    if (dec <= 0) snprintf(out, cap, "%d", (int)lroundf(v));
    else {
        snprintf(out, cap, "%.*f", dec, v);
        /* trim trailing zeros */
        char *dot = strchr(out, '.');
        if (dot) { char *e = out + strlen(out) - 1; while (e > dot && *e == '0') *e-- = 0; if (e == dot) *e = 0; }
    }
}

bool ui_number_mixed(uint32_t id, Rect r, float *v, float step, int dec, float vmin, float vmax, bool mixed) {
    bool changed = false;
    bool focused = ui.edit_id == id && ui.focus == id;
    Rect fr = r;
    float sw = 18;
    Rect up = rect(r.x + r.w - sw, r.y + 1, sw - 1, r.h * 0.5f - 1), dn = rect(r.x + r.w - sw, r.y + r.h * 0.5f, sw - 1, r.h * 0.5f - 1);
    fr.w -= sw;
    bool hov = ui_hover(fr);
    if (hov) { ui.hot = id; ui.any_hover_widget = true; ui.cursor = focused ? CUR_IBEAM : CUR_SIZEWE; ui_tooltip("Drag, scroll or click to type"); }
    if (!focused) {
        if (hov && ui.in->mpressed[0] && !ui.active) {
            ui.active = id; ui.drag_x = ui.mx; ui.drag_val = *v; ui.dragging = false;
        }
        if (ui.active == id) {
            ui.cursor = CUR_SIZEWE;
            if (fabsf(ui.mx - ui.drag_x) > 3) ui.dragging = true;
            if (ui.dragging) {
                float nv = ui.drag_val + roundf((ui.mx - ui.drag_x) / 4.0f) * step;
                nv = SF_CLAMP(nv, vmin, vmax);
                if (nv != *v) { *v = nv; changed = true; }
            }
            if (ui.in->mreleased[0]) {
                ui.active = 0;
                if (!ui.dragging) {
                    ui.focus = id; ui.edit_id = id;
                    if (mixed) ui.edit[0] = 0; else fmt_num(ui.edit, sizeof ui.edit, *v, dec);
                    ui.edit_sel = 0; ui.edit_cur = (int)strlen(ui.edit);
                    ui.caret_time = ui.time;
                    focused = true;
                }
                ui.dragging = false;
            }
        }
        if (hov && ui.in->wheel != 0 && !g_wheel_used) {
            float s = step * (ui.in->shift ? 8.0f : 1.0f) * (ui.in->wheel > 0 ? 1.0f : -1.0f);
            *v = SF_CLAMP(*v + s, vmin, vmax);
            changed = true;
            g_wheel_used = true;
        }
    } else {
        g_focus_seen = true;
        bool commit = false, cancel = false;
        if (ui.in->mpressed[0] && !hov) commit = true;
        int k = edit_keys(ui.edit, 32);
        if (k == 2) commit = true;
        if (k == 3) cancel = true;
        if (commit) {
            if (ui.edit[0]) {
                float nv = (float)atof(ui.edit);
                nv = SF_CLAMP(nv, vmin, vmax);
                *v = nv; changed = true;
            }
            ui.focus = 0; ui.edit_id = 0; focused = false;
        }
        if (cancel) { ui.focus = 0; ui.edit_id = 0; focused = false; }
    }
    /* stepper */
    bool hu, hu2, hd, hd2;
    if (behave(id ^ 0x55u, up, &hu, &hu2)) { *v = SF_CLAMP((mixed ? 0 : *v) + step, vmin, vmax); changed = true; }
    if (behave(id ^ 0xAAu, dn, &hd, &hd2)) { *v = SF_CLAMP((mixed ? 0 : *v) - step, vmin, vmax); changed = true; }

    gfx_rrect(r, 6, C(TH_FIELD));
    gfx_rrect_line(r, 6, 1, focused ? C(TH_ACCENT_BD) : (hov || ui.active == id) ? C(0x3A3F49) : C(TH_BORDER2));
    if (hu) gfx_rect(up, C(TH_HOVER));
    if (hd) gfx_rect(dn, C(TH_HOVER));
    gfx_icon(IC_chev_up, up.x + up.w * 0.5f, up.y + up.h * 0.5f + 1, 11, hu ? C(0xFFFFFF) : C(TH_TEXT3));
    gfx_icon(IC_chev_down, dn.x + dn.w * 0.5f, dn.y + dn.h * 0.5f - 1, 11, hd ? C(0xFFFFFF) : C(TH_TEXT3));
    if (focused) draw_edit_text(FONT_MONO, fr, fr.x + 8, ui.edit, true);
    else {
        char tmp[48];
        if (mixed) sf_strcpy(tmp, sizeof tmp, "\xE2\x80\x94");
        else fmt_num(tmp, sizeof tmp, *v, dec);
        gfx_text(FONT_MONO, fr.x + 8, r.y + (r.h - gfx_font_h(FONT_MONO)) * 0.5f, tmp, C(TH_TEXT));
    }
    return changed;
}

bool ui_number(uint32_t id, Rect r, float *v, float step, int dec, float vmin, float vmax, bool stepper) {
    (void)stepper;
    return ui_number_mixed(id, r, v, step, dec, vmin, vmax, false);
}

bool ui_toggle(uint32_t id, Rect r, const char *label, bool *v) {
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    if (hov) ui.cursor = CUR_HAND;
    if (click) *v = !*v;
    if (label) gfx_text_fit(FONT_UI, r.x, r.y + (r.h - gfx_font_h(FONT_UI)) * 0.5f, r.w - 44, label, C(hov ? TH_TEXT : TH_TEXT2));
    Rect sw = rect(r.x + r.w - 34, r.y + (r.h - 18) * 0.5f, 34, 18);
    gfx_rrect(sw, 9, *v ? C(TH_ACCENT) : C(TH_BORDER2));
    float kx = *v ? sw.x + sw.w - 9 : sw.x + 9;
    gfx_circle(kx, sw.y + 9, 7, *v ? C(0x1A0D02) : C(TH_TEXT3));
    return click;
}

bool ui_slider(uint32_t id, Rect r, float *v, float vmin, float vmax, int dec) {
    bool hov = ui_hover(r);
    bool changed = false;
    if (hov) { ui.hot = id; ui.any_hover_widget = true; ui.cursor = CUR_HAND; }
    if (hov && ui.in->mpressed[0] && !ui.active) ui.active = id;
    float t = (*v - vmin) / (vmax - vmin);
    if (ui.active == id) {
        float nt = SF_CLAMP((ui.mx - r.x) / r.w, 0, 1);
        float nv = vmin + nt * (vmax - vmin);
        if (dec == 0) nv = roundf(nv);
        if (nv != *v) { *v = nv; changed = true; }
        if (ui.in->mreleased[0]) ui.active = 0;
    }
    if (hov && ui.in->wheel != 0 && !g_wheel_used) {
        float s = (vmax - vmin) / 32.0f;
        if (dec == 0) s = roundf(s) < 1 ? 1 : roundf(s);
        *v = SF_CLAMP(*v + (ui.in->wheel > 0 ? s : -s), vmin, vmax);
        changed = true; g_wheel_used = true;
    }
    t = SF_CLAMP((*v - vmin) / (vmax - vmin), 0, 1);
    float cy = r.y + r.h * 0.5f;
    gfx_rrect(rect(r.x, cy - 2, r.w, 4), 2, C(TH_BORDER2));
    gfx_rrect(rect(r.x, cy - 2, r.w * t, 4), 2, C(TH_ACCENT));
    if (hov || ui.active == id) gfx_circle(r.x + r.w * t, cy, 10, col(255, 122, 26, 70));
    gfx_circle(r.x + r.w * t, cy, 7, C(0xFFFFFF));
    return changed;
}

bool ui_dropdown(uint32_t id, Rect r, const char **items, int n, int *sel) {
    bool changed = false;
    if (ui.result_id == id) { if (ui.result >= 0 && ui.result < n) { *sel = ui.result; changed = true; } }
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    if (hov) ui.cursor = CUR_HAND;
    bool open = ui.popup_id == id;
    if (click) {
        if (open) ui_close_popup();
        else {
            ui.popup_id = id; ui.popup_kind = POP_DROPDOWN; ui.popup_anchor = r; ui.popup_items = items; ui.popup_n = n;
            ui.popup_sel = *sel; ui.popup_scroll = 0; ui.popup_opened_now = true;
            ui.popup_layer = ui.layer >= LAYER_MODAL ? LAYER_MODAL_POPUP : LAYER_POPUP;
            open = true;
        }
    }
    gfx_rrect(r, 6, C(TH_FIELD));
    gfx_rrect_line(r, 6, 1, open ? C(TH_ACCENT_BD) : hov ? C(0x3A3F49) : C(TH_BORDER2));
    const char *txt = (*sel >= 0 && *sel < n) ? items[*sel] : "\xE2\x80\x94";
    gfx_text_fit(FONT_UI, r.x + 8, r.y + (r.h - gfx_font_h(FONT_UI)) * 0.5f, r.w - 30, txt, C(TH_TEXT));
    gfx_icon(open ? IC_chev_up : IC_chev_down, r.x + r.w - 14, r.y + r.h * 0.5f, 13, C(TH_TEXT3));
    return changed;
}

bool ui_tabs(uint32_t id, Rect r, const char **labels, int n, int *active) {
    bool changed = false;
    float x = r.x + 8;
    for (int i = 0; i < n; i++) {
        float w = gfx_text_w(FONT_UI, labels[i]) + 20;
        Rect tr = rect(x, r.y, w, r.h);
        bool hov, held;
        if (behave(id + (uint32_t)i * 31u, tr, &hov, &held)) { *active = i; changed = true; }
        if (hov) ui.cursor = CUR_HAND;
        bool on = *active == i;
        gfx_text_center(on ? FONT_UI_BOLD : FONT_UI, tr, labels[i], on ? C(0xFFFFFF) : hov ? C(TH_TEXT) : C(TH_TEXT2));
        if (on) gfx_rect(rect(tr.x + 4, r.y + r.h - 2, tr.w - 8, 2), C(TH_ACCENT));
        x += w + 2;
    }
    return changed;
}

bool ui_menu_button(uint32_t id, Rect r, const char *label, const MenuItem *items, int n) {
    bool hov, held;
    bool click = behave(id, r, &hov, &held);
    ui_nocaption(r);
    bool open = ui.popup_id == id;
    if (hov && ui.popup_id && ui.popup_kind == POP_MENU && !open && ui.popup_anchor.y == r.y) {
        /* slide between menu bar menus */
        click = true;
    }
    if (click) {
        if (open && !(hov && ui.popup_id != id)) ui_close_popup();
        else {
            ui.popup_id = id; ui.popup_kind = POP_MENU; ui.popup_anchor = r; ui.menu_items = items; ui.menu_n = n;
            ui.popup_sel = -1; ui.popup_opened_now = true; ui.popup_layer = LAYER_POPUP;
            open = true;
        }
    }
    if (open) gfx_rrect(r, 5, C(TH_HOVER));
    else if (hov) gfx_rrect(r, 5, C(TH_HOVER));
    gfx_text_center(FONT_UI, r, label, open || hov ? C(0xFFFFFF) : C(0xC4CAD4));
    return open;
}

void ui_open_menu_at(uint32_t id, float x, float y, const MenuItem *items, int n) {
    ui.popup_id = id; ui.popup_kind = POP_MENU; ui.popup_anchor = rect(x, y, 1, 0); ui.menu_items = items; ui.menu_n = n;
    ui.popup_sel = -1; ui.popup_opened_now = true; ui.popup_layer = LAYER_POPUP;
}

int ui_popup_result(uint32_t id) { return (id && ui.result_id == id) ? ui.result : -1; }
bool ui_popup_open(uint32_t id) { return ui.popup_id == id; }
void ui_close_popup(void) { ui.popup_id = 0; ui.popup_kind = POP_NONE; }

float ui_scroll_begin(uint32_t id, Rect r, float *scroll) {
    (void)id;
    gfx_clip_push(r);
    return r.y - *scroll;
}

void ui_scroll_end(uint32_t id, Rect r, float *scroll, float content_bottom) {
    gfx_clip_pop();
    float content_h = content_bottom - (r.y - *scroll);
    float maxs = SF_MAX(0, content_h - r.h);
    bool hov = rect_has(r, ui.mx, ui.my) && ui.layer >= ui.top_layer;
    if (hov && ui.in->wheel != 0 && !g_wheel_used) { *scroll -= ui.in->wheel * 56; g_wheel_used = true; }
    *scroll = SF_CLAMP(*scroll, 0, maxs);
    if (maxs > 0) {
        float th = SF_MAX(24, r.h * r.h / content_h);
        float ty = r.y + (r.h - th) * (*scroll / maxs);
        Rect bar = rect(r.x + r.w - 6, ty, 4, th);
        bool bh, held;
        behave(id ^ 0x5C5Cu, rect(r.x + r.w - 10, r.y, 10, r.h), &bh, &held);
        if (held) {
            float t = SF_CLAMP((ui.my - r.y - th * 0.5f) / (r.h - th), 0, 1);
            *scroll = t * maxs;
        }
        gfx_rrect(bar, 2, (bh || held) ? col_hex(0x4A505B) : col_hex(0x2F333B));
    }
}

void ui_label(int font, float x, float y, const char *s, uint32_t hex) { gfx_text(font, x, y, s, col_hex(hex)); }

void ui_section_title(Rect *area, int icon, const char *title) {
    float x = area->x;
    if (icon >= 0) { gfx_icon(icon, x + 7, area->y + 8, 14, col_hex(TH_TEXT3)); x += 20; }
    char up[64];
    sf_strcpy(up, sizeof up, title);
    sf_upper(up);
    gfx_text(FONT_SMALL, x, area->y + 8 - gfx_font_h(FONT_SMALL) * 0.5f, up, col_hex(0xA3ABB8));
    area->y += 24; area->h -= 24;
}

void ui_kbd(float x, float y, const char *key, float *outw) {
    float w = gfx_text_w(FONT_MONO_SMALL, key) + 10, h = 18;
    Rect r = rect(x, y, w, h);
    gfx_rrect(r, 4, col_hex(0x22252C));
    gfx_rrect_line(r, 4, 1, col_hex(0x343944));
    gfx_rect(rect(x + 2, y + h - 2, w - 4, 1), col_hex(0x343944));
    gfx_text_center(FONT_MONO_SMALL, rect(x, y - 1, w, h), key, col_hex(0xD8DDE5));
    if (outw) *outw = w;
}

void ui_chip(float x, float y, int icon, const char *text, bool accent, float *outw) {
    float w = gfx_text_w(FONT_SMALL, text) + 16 + (icon >= 0 ? 18 : 0), h = 24;
    Rect r = rect(x, y, w, h);
    gfx_rrect(r, 6, col(10, 11, 13, 220));
    gfx_rrect_line(r, 6, 1, accent ? col_hex(TH_ACCENT_BD) : col_hex(TH_BORDER2));
    Col fg = accent ? col_hex(TH_ACCENT_HI) : col_hex(0xCFD5DE);
    float tx = x + 8;
    if (icon >= 0) { gfx_icon(icon, tx + 7, y + h * 0.5f, 13, fg); tx += 18; }
    gfx_text(FONT_SMALL, tx, y + (h - gfx_font_h(FONT_SMALL)) * 0.5f, text, fg);
    if (outw) *outw = w;
}

void ui_panel(Rect r, uint32_t bg) { gfx_rect(r, col_hex(bg)); }

/* ------------------------------------------------------------------ end of frame: popups, tooltip */
static void draw_popup(void) {
    if (!ui.popup_id) return;
    int saved = ui.layer;
    ui.layer = ui.popup_layer;
    const Input *in = ui.in;
    float ih = ui.popup_kind == POP_MENU ? 28 : 26;
    int n = ui.popup_kind == POP_MENU ? ui.menu_n : ui.popup_n;
    float w = ui.popup_anchor.w, h = 8;
    for (int i = 0; i < n; i++) {
        if (ui.popup_kind == POP_MENU) {
            const MenuItem *mi = &ui.menu_items[i];
            if (mi->separator) { h += 9; continue; }
            float iw = 34 + gfx_text_w(FONT_UI, mi->label) + (mi->shortcut ? gfx_text_w(FONT_SMALL, mi->shortcut) + 36 : 16);
            w = SF_MAX(w, iw);
        } else w = SF_MAX(w, gfx_text_w(FONT_UI, ui.popup_items[i]) + 40);
        h += ih;
    }
    w = SF_MAX(w, 180);
    float maxh = SF_MIN(ih * 14 + 8, in->win_h - 20);
    float vh = SF_MIN(h, maxh);
    Rect r = rect(ui.popup_anchor.x, ui.popup_anchor.y + ui.popup_anchor.h + 4, w, vh);
    if (r.y + r.h > in->win_h - 6) r.y = SF_MAX(6, ui.popup_anchor.y - vh - 4);
    if (r.x + r.w > in->win_w - 6) r.x = in->win_w - 6 - r.w;
    ui_push_block(r, ui.popup_layer);
    gfx_shadow(r, 10, 14, col(0, 0, 0, 160));
    gfx_rrect(r, 9, col_hex(TH_PANEL));
    gfx_rrect_line(r, 9, 1, col_hex(TH_BORDER2));
    bool inside = rect_has(r, ui.mx, ui.my);
    if (inside && in->wheel != 0 && h > vh) { ui.popup_scroll -= in->wheel * 52; g_wheel_used = true; }
    ui.popup_scroll = SF_CLAMP(ui.popup_scroll, 0, SF_MAX(0, h - vh));
    gfx_clip_push(rect_inset(r, 1));
    float y = r.y + 4 - ui.popup_scroll;
    int picked = -1;
    for (int i = 0; i < n; i++) {
        if (ui.popup_kind == POP_MENU && ui.menu_items[i].separator) {
            gfx_rect(rect(r.x + 10, y + 4, r.w - 20, 1), col_hex(TH_BORDER2));
            y += 9;
            continue;
        }
        Rect ir = rect(r.x + 4, y, r.w - 8, ih);
        bool dis = ui.popup_kind == POP_MENU && ui.menu_items[i].disabled;
        bool hov = !dis && rect_has(ir, ui.mx, ui.my) && inside && !ui.popup_opened_now;
        if (hov) { gfx_rrect(ir, 6, col_hex(TH_HOVER)); ui.cursor = CUR_HAND; }
        if (ui.popup_kind == POP_MENU) {
            const MenuItem *mi = &ui.menu_items[i];
            Col fg = dis ? col_hex(0x5E6570) : hov ? col_hex(0xFFFFFF) : col_hex(TH_TEXT);
            if (mi->checked) gfx_icon(IC_check, ir.x + 14, ir.y + ih * 0.5f, 14, col_hex(TH_ACCENT));
            else if (mi->icon >= 0) gfx_icon(mi->icon, ir.x + 14, ir.y + ih * 0.5f, 14, dis ? fg : col_hex(TH_TEXT3));
            gfx_text(FONT_UI, ir.x + 30, ir.y + (ih - gfx_font_h(FONT_UI)) * 0.5f, mi->label, fg);
            if (mi->shortcut) gfx_text_right(FONT_SMALL, ir.x + ir.w - 10, ir.y + (ih - gfx_font_h(FONT_SMALL)) * 0.5f, mi->shortcut, col_hex(TH_TEXT3));
        } else {
            bool cur = i == ui.popup_sel;
            if (cur) gfx_icon(IC_check, ir.x + 14, ir.y + ih * 0.5f, 13, col_hex(TH_ACCENT));
            gfx_text(FONT_UI, ir.x + 28, ir.y + (ih - gfx_font_h(FONT_UI)) * 0.5f, ui.popup_items[i], cur ? col_hex(TH_ACCENT_HI) : hov ? col_hex(0xFFFFFF) : col_hex(TH_TEXT));
        }
        if (hov && in->mreleased[0]) picked = i;
        y += ih;
    }
    gfx_clip_pop();
    uint32_t pid = ui.popup_id;
    if (picked >= 0) { g_pending_id = pid; g_pending_res = picked; ui_close_popup(); }
    else if (!ui.popup_opened_now && (in->mpressed[0] || in->mpressed[1]) && !inside && !rect_has(ui.popup_anchor, ui.mx, ui.my)) ui_close_popup();
    else if (in->kpressed[KEY_ESC]) ui_close_popup();
    ui.popup_opened_now = false;
    ui.layer = saved;
}

void ui_end(void) {
    const Input *in = ui.in;
    draw_popup();
    if (!g_focus_seen && ui.focus) { ui.focus = 0; ui.edit_id = 0; }
    if (in->mreleased[0]) ui.active = 0;
    if (ui.hot != ui.prev_hot) ui.hot_since = ui.time;
    if (ui.tip && ui.hot && !ui.active && !ui.popup_id && ui.time - ui.hot_since > 0.45) {
        float w = gfx_text_w(FONT_SMALL, ui.tip) + 16, h = 24;
        Rect r = rect(ui.mx + 14, ui.my + 20, w, h);
        if (r.x + r.w > in->win_w - 4) r.x = in->win_w - 4 - r.w;
        if (r.y + r.h > in->win_h - 4) r.y = ui.my - h - 8;
        gfx_shadow(r, 6, 8, col(0, 0, 0, 140));
        gfx_rrect(r, 6, col_hex(0x1D2026));
        gfx_rrect_line(r, 6, 1, col_hex(TH_BORDER2));
        gfx_text(FONT_SMALL, r.x + 8, r.y + (h - gfx_font_h(FONT_SMALL)) * 0.5f, ui.tip, col_hex(TH_TEXT));
    }
    ui.text_focus = ui.focus != 0;
    plat_set_cursor(ui.cursor);
}
