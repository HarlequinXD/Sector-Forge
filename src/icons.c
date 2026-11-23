/* Sector Forge - icon definitions in a tiny path language and an anti-aliased rasteriser.
 * 24x24 units, y down.  Commands:
 *   M x y / L x y   polyline,  Z close      C cx cy r  circle
 *   A cx cy r a0 a1 arc (deg)              R x y w h  rect      Q x y w h r  rounded rect
 *   P cx cy r       filled dot             F ... S    fill following closed paths until S
 *   W w             stroke width */
#include "icons.h"
#include "map.h"

static const char *g_icon_src[IC_COUNT] = {
    [IC_logo] = "W 2 M12 2.5 L20.5 7.25 L20.5 16.75 L12 21.5 L3.5 16.75 L3.5 7.25 Z M7.5 9.5 L13 9.5 L16.5 12.5 L14 16.5 L7.5 16.5 Z",
    [IC_minimize] = "M6 12 L18 12",
    [IC_maximize] = "Q 6 6 12 12 1",
    [IC_restore] = "Q 5 9 10 10 1 M9 9 L9 5 L19 5 L19 15 L15 15",
    [IC_close] = "M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5",
    [IC_new] = "M14 3 L7 3 L5 5 L5 19 L7 21 L17 21 L19 19 L19 8 Z M14 3 L14 8 L19 8 M12 11 L12 17 M9 14 L15 14",
    [IC_open] = "M3 7 L3 17 L5 19 L19 19 L21 17 L21 9 L19 7 L11 7 L9 5 L5 5 Z",
    [IC_save] = "M5 3 L16 3 L21 8 L21 19 L19 21 L5 21 L3 19 L3 5 Z M7 3 L7 8 L15 8 L15 3 M7 21 L7 14 L17 14 L17 21",
    [IC_export] = "M12 3 L12 15 M7 8 L12 3 L17 8 M5 15 L5 19.5 L6.5 21 L17.5 21 L19 19.5 L19 15",
    [IC_import] = "M12 15 L12 3 M7 10 L12 15 L17 10 M5 15 L5 19.5 L6.5 21 L17.5 21 L19 19.5 L19 15",
    [IC_undo] = "M9 14 L4 9 L9 4 M4 9 L14.5 9 A 14.5 14.5 5.5 -90 90 M14.5 20 L11 20",
    [IC_redo] = "M15 14 L20 9 L15 4 M20 9 L9.5 9 A 9.5 14.5 5.5 270 90 M9.5 20 L13 20",
    [IC_cut] = "C 6 18 3 C 18 18 3 M8.1 15.9 L19 4 M15.9 15.9 L5 4",
    [IC_copy] = "Q 9 9 12 12 2 M5 15 L4 15 L3 14 L3 4 L4 3 L14 3 L15 4 L15 5",
    [IC_paste] = "Q 8 2 8 4 1 M16 4 L18 4 L20 6 L20 20 L18 22 L6 22 L4 20 L4 6 L6 4 L8 4",
    [IC_trash] = "M3 6 L21 6 M8 6 L8 4 L16 4 L16 6 M6 6 L7 21 L17 21 L18 6 M10 11 L10 17 M14 11 L14 17",
    [IC_select] = "M5 3 L19 10 L12.8 11.8 L11 18 Z M13 12 L19 18",
    [IC_hand] = "M8 13 L8 6 M11 12 L11 4 M14 12 L14 4.5 M17 13 L17 7 M8 13 L6 11 L4.6 12.4 L8.5 18.5 L10.5 20.5 L15.5 20.5 L17 19 L17 13",
    [IC_draw_sector] = "M4 18 L7 6 L16 8 L20 17 Z P 4 18 1.9 P 7 6 1.9 P 16 8 1.9 P 20 17 1.9",
    [IC_rect] = "R 4.5 5.5 15 13 R 3 4 3 3 R 18 17 3 3",
    [IC_polygon] = "M12 3 L19.8 7.5 L19.8 16.5 L12 21 L4.2 16.5 L4.2 7.5 Z",
    [IC_line] = "M6 18 L18 6 R 3 17 4 4 R 17 3 4 4",
    [IC_vertex] = "R 8 8 8 8 M12 2 L12 6 M12 18 L12 22 M2 12 L6 12 M18 12 L22 12",
    [IC_split] = "M4 20 L10 14 M14 10 L20 4 R 10 10 4 4 M18 14 L18 20 M15 17 L21 17",
    [IC_join] = "R 3 5 7 14 R 14 5 7 14 M8 12 L16 12 M14 10 L16 12 L14 14",
    [IC_move] = "M12 3 L12 21 M3 12 L21 12 M9 6 L12 3 L15 6 M9 18 L12 21 L15 18 M6 9 L3 12 L6 15 M18 9 L21 12 L18 15",
    [IC_rotate] = "A 12 12 8 0 315 M20 4 L20 9 L15 9",
    [IC_scale] = "R 3 11 10 10 M14 3 L21 3 L21 10 M21 3 L13 11",
    [IC_flip] = "M12 3 L12 5 M12 8 L12 10 M12 13 L12 15 M12 18 L12 21 M9 7 L3 12 L9 17 Z M15 7 L21 12 L15 17 Z",
    [IC_flip_v] = "M3 12 L5 12 M8 12 L10 12 M13 12 L15 12 M18 12 L21 12 M7 9 L12 3 L17 9 Z M7 15 L12 21 L17 15 Z",
    [IC_measure] = "M3 17 L17 3 L21 7 L7 21 Z M7 13 L9 15 M10 10 L12 12 M13 7 L15 9",
    [IC_grid] = "R 3 3 18 18 M9 3 L9 21 M15 3 L15 21 M3 9 L21 9 M3 15 L21 15",
    [IC_magnet] = "M6 3 L10 3 L10 11 A 12 11 2 180 0 M14 11 L14 3 L18 3 L18 11 A 12 11 6 0 180 M6 11 L6 3 M6 7 L10 7 M14 7 L18 7",
    [IC_floor] = "M2 14 L17 14 L22 20 L7 20 Z M12 2 L12 10 M9.5 7.5 L12 10 L14.5 7.5",
    [IC_ceiling] = "M2 4 L17 4 L22 10 L7 10 Z M12 22 L12 14 M9.5 16.5 L12 14 L14.5 16.5",
    [IC_wall] = "R 3 4 18 16 M3 9.3 L21 9.3 M3 14.7 L21 14.7 M9 4 L9 9.3 M15 4 L15 9.3 M12 9.3 L12 14.7 M6 9.3 L6 14.7 M18 9.3 L18 14.7 M9 14.7 L9 20 M15 14.7 L15 20",
    [IC_sector] = "M4 6 L14 4 L20 11 L16 20 L5 17 Z P 11 12 1.8",
    [IC_height] = "M12 3 L12 21 M8 7 L12 3 L16 7 M8 17 L12 21 L16 17 M3 12 L7 12 M17 12 L21 12",
    [IC_wheel] = "Q 6 2 12 20 6 M12 6 L12 10",
    [IC_texture] = "Q 3 3 18 18 2 C 9 9 2 M21 15 L16 10 L5 21",
    [IC_bucket] = "M11 3 L19 11 L11 19 L3 11 Z M3 11 L19 11 P 20 18 2",
    [IC_eyedropper] = "M3 21 L4 17 L13 8 L16 11 L7 20 Z M12 5 L19 12 C 18 6 2.6",
    [IC_wireframe] = "M12 2 L21 7 L21 17 L12 22 L3 17 L3 7 Z M3 7 L12 12 L21 7 M12 12 L12 22",
    [IC_light] = "C 12 12 4 M12 2 L12 4 M12 20 L12 22 M4.9 4.9 L6.3 6.3 M17.7 17.7 L19.1 19.1 M2 12 L4 12 M20 12 L22 12 M4.9 19.1 L6.3 17.7 M17.7 6.3 L19.1 4.9",
    [IC_player] = "C 12 7 4 A 12 21 8 180 360",
    [IC_enemy] = "A 12 11 8 180 360 M4 11 L4 13 L7 16.6 L7 20 L17 20 L17 16.6 L20 13 L20 11 P 9 11 1.7 P 15 11 1.7 M10 20 L10 18 M14 20 L14 18",
    [IC_item] = "M12 2 L20 12 L12 22 L4 12 Z M4 12 L20 12",
    [IC_door] = "M5 21 L5 3 L19 3 L19 21 M3 21 L21 21 P 15 12 1.1",
    [IC_lift] = "R 4 3 16 18 M9 9 L12 6 L15 9 M9 15 L12 18 L15 15",
    [IC_thing] = "M12 21 L7.2 14 L6 10 A 12 10 6 180 360 M18 10 L16.8 14 L12 21 C 12 10 2.2",
    [IC_tag] = "M3 12 L3 3 L12 3 L21 12 L12 21 Z P 8 8 1.6",
    [IC_mode2d] = "M3 6 L9 3 L15 6 L21 3 L21 18 L15 21 L9 18 L3 21 Z M9 3 L9 18 M15 6 L15 21",
    [IC_cube] = "M12 2 L21 7 L21 17 L12 22 L3 17 L3 7 Z M3 7 L12 12 L21 7 M12 12 L12 22",
    [IC_play] = "F M7 4 L7 20 L20 12 Z S",
    [IC_stop] = "F M6 6 L18 6 L18 18 L6 18 Z S",
    [IC_eye] = "M2 12 L5 8 L9 5.6 L12 5 L15 5.6 L19 8 L22 12 L19 16 L15 18.4 L12 19 L9 18.4 L5 16 Z C 12 12 3",
    [IC_layers] = "M12 2 L22 7 L12 12 L2 7 Z M2 12 L12 17 L22 12 M2 17 L12 22 L22 17",
    [IC_zoom_in] = "C 11 11 7 M21 21 L16 16 M11 8 L11 14 M8 11 L14 11",
    [IC_zoom_out] = "C 11 11 7 M21 21 L16 16 M8 11 L14 11",
    [IC_fit] = "M3 8 L3 3 L8 3 M21 8 L21 3 L16 3 M3 16 L3 21 L8 21 M21 16 L21 21 L16 21",
    [IC_search] = "C 11 11 7 M21 21 L16 16",
    [IC_settings] = "M4 6 L14 6 M18 6 L20 6 M4 12 L8 12 M12 12 L20 12 M4 18 L16 18 M20 18 L20 18 C 16 6 2 C 10 12 2 C 18 18 2",
    [IC_console] = "Q 3 4 18 16 2 M7 9 L10 12 L7 15 M13 15 L17 15",
    [IC_chev_down] = "M6 9 L12 15 L18 9",
    [IC_chev_up] = "M6 15 L12 9 L18 15",
    [IC_chev_right] = "M9 6 L15 12 L9 18",
    [IC_chev_left] = "M15 6 L9 12 L15 18",
    [IC_plus] = "M12 5 L12 19 M5 12 L19 12",
    [IC_minus] = "M5 12 L19 12",
    [IC_check] = "M5 12 L10 17 L19 7",
    [IC_clock] = "C 12 12 9 M12 7 L12 12 L15 14",
    [IC_book] = "M4 19 L4 5 L6 3 L20 3 L20 19 L6 19 L4 21 L6 21 L20 21",
    [IC_template] = "Q 3 3 18 18 2 M3 9 L21 9 M9 21 L9 9",
    [IC_crosshair] = "C 12 12 8 M12 2 L12 7 M12 17 L12 22 M2 12 L7 12 M17 12 L22 12",
    [IC_info] = "C 12 12 9 M12 11 L12 17 P 12 7.5 1.3",
    [IC_warn] = "M12 3 L22 20 L2 20 Z M12 9 L12 14 P 12 17 1.2",
    [IC_keyboard] = "Q 2 6 20 12 2 M6 10 L6 10 M10 10 L10 10 M14 10 L14 10 M18 10 L18 10 M8 14 L16 14",
    [IC_menu] = "M4 6 L20 6 M4 12 L20 12 M4 18 L20 18",
    [IC_sky] = "A 9 14 4 90 270 A 13 11 5 180 330 A 17 15 3 270 450 M9 18 L17 18",
    [IC_dot] = "P 12 12 4",
};

const char *icon_name(int id) {
    static const char *names[] = {
#define ICON_NAME(n) #n,
        ICON_LIST(ICON_NAME)
#undef ICON_NAME
    };
    return id >= 0 && id < IC_COUNT ? names[id] : "?";
}

typedef struct { float ax, ay, bx, by, w; } Seg;
typedef struct { ARR(Seg) segs; ARR(float) poly; IntArr poly_start; } Shape;

static void add_seg(Shape *s, float ax, float ay, float bx, float by, float w) {
    Seg g = { ax, ay, bx, by, w };
    arr_push(s->segs, g);
}

static void parse_icon(const char *src, Shape *sh) {
    const char *p = src;
    float w = 1.75f;
    bool fill = false;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    bool have = false;
    int fill_start = -1;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char c = *p++;
        float a[6] = { 0 };
        int na = c == 'M' || c == 'L' ? 2 : c == 'C' || c == 'P' ? 3 : c == 'A' ? 5 : c == 'R' ? 4 : c == 'Q' ? 5 : c == 'W' ? 1 : 0;
        for (int i = 0; i < na; i++) a[i] = strtof(p, (char **)&p);
        switch (c) {
        case 'W': w = a[0]; break;
        case 'F': fill = true; break;
        case 'S': fill = false; break;
        case 'M':
            cx = sx = a[0]; cy = sy = a[1]; have = true;
            if (fill) { arr_push(sh->poly_start, sh->poly.n); fill_start = sh->poly.n; arr_push(sh->poly, cx); arr_push(sh->poly, cy); }
            break;
        case 'L':
            if (fill) { arr_push(sh->poly, a[0]); arr_push(sh->poly, a[1]); }
            else if (have) add_seg(sh, cx, cy, a[0], a[1], w);
            cx = a[0]; cy = a[1];
            break;
        case 'Z':
            if (!fill && have) add_seg(sh, cx, cy, sx, sy, w);
            cx = sx; cy = sy;
            (void)fill_start;
            break;
        case 'C': case 'A': {
            float r = a[2], a0 = c == 'C' ? 0 : a[3], a1 = c == 'C' ? 360 : a[4];
            int steps = (int)(fabsf(a1 - a0) / 8) + 4;
            for (int i = 0; i < steps; i++) {
                float t0 = (float)((a0 + (a1 - a0) * i / steps) * SF_DEG2RAD), t1 = (float)((a0 + (a1 - a0) * (i + 1) / steps) * SF_DEG2RAD);
                add_seg(sh, a[0] + r * cosf(t0), a[1] + r * sinf(t0), a[0] + r * cosf(t1), a[1] + r * sinf(t1), w);
            }
        } break;
        case 'P': {
            arr_push(sh->poly_start, sh->poly.n);
            for (int i = 0; i < 20; i++) {
                float t = (float)(i * 2 * SF_PI / 20);
                arr_push(sh->poly, a[0] + a[2] * cosf(t));
                arr_push(sh->poly, a[1] + a[2] * sinf(t));
            }
        } break;
        case 'R':
            add_seg(sh, a[0], a[1], a[0] + a[2], a[1], w); add_seg(sh, a[0] + a[2], a[1], a[0] + a[2], a[1] + a[3], w);
            add_seg(sh, a[0] + a[2], a[1] + a[3], a[0], a[1] + a[3], w); add_seg(sh, a[0], a[1] + a[3], a[0], a[1], w);
            break;
        case 'Q': {
            float x = a[0], y = a[1], ww = a[2], hh = a[3], r = SF_MIN(a[4], SF_MIN(ww, hh) * 0.5f);
            float px[4] = { x + r, x + ww - r, x + ww - r, x + r }, py[4] = { y + r, y + r, y + hh - r, y + hh - r };
            float st[4] = { 180, 270, 0, 90 };
            float lx = 0, ly = 0;
            bool first = true;
            float fx = 0, fy = 0;
            for (int k = 0; k < 4; k++)
                for (int i = 0; i <= 4; i++) {
                    float t = (float)((st[k] + 90.0f * i / 4) * SF_DEG2RAD);
                    float qx = px[k] + r * cosf(t), qy = py[k] + r * sinf(t);
                    if (!first) add_seg(sh, lx, ly, qx, qy, w);
                    else { fx = qx; fy = qy; }
                    first = false;
                    lx = qx; ly = qy;
                }
            add_seg(sh, lx, ly, fx, fy, w);
        } break;
        default: break;
        }
    }
    arr_push(sh->poly_start, sh->poly.n);
}

static float cover_px(const Shape *sh, float x, float y, float scale) {
    float best = 0;
    for (int i = 0; i < sh->segs.n; i++) {
        const Seg *s = &sh->segs.a[i];
        double t;
        float d = (float)seg_dist(x, y, s->ax, s->ay, s->bx, s->by, &t);
        float c = (s->w * 0.5f - d) * scale + 0.5f;
        if (c > best) best = c;
        if (best >= 1) return 1;
    }
    for (int k = 0; k + 1 < sh->poly_start.n; k++) {
        int a0 = sh->poly_start.a[k], a1 = sh->poly_start.a[k + 1];
        int n = (a1 - a0) / 2;
        if (n < 3) continue;
        const float *P = sh->poly.a + a0;
        bool in = false;
        float md = 1e9f;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float xi = P[i * 2], yi = P[i * 2 + 1], xj = P[j * 2], yj = P[j * 2 + 1];
            if ((yi > y) != (yj > y) && x < xj + (y - yj) * (xi - xj) / (yi - yj)) in = !in;
            double t;
            float d = (float)seg_dist(x, y, xi, yi, xj, yj, &t);
            if (d < md) md = d;
        }
        float c = in ? md * scale + 0.5f : 0.5f - md * scale;
        if (c > best) best = c;
    }
    return SF_CLAMP(best, 0.f, 1.f);
}

void icons_raster(uint8_t *rgba, int from, int to) {
    const float scale = ICON_CELL / 24.0f;
    for (int id = from; id < to && id < IC_COUNT; id++) {
        Shape sh;
        memset(&sh, 0, sizeof sh);
        if (g_icon_src[id]) parse_icon(g_icon_src[id], &sh);
        int ox = (id % ICON_COLS) * ICON_CELL, oy = (id / ICON_COLS) * ICON_CELL;
        for (int y = 0; y < ICON_CELL; y++)
            for (int x = 0; x < ICON_CELL; x++) {
                /* 2x2 supersampling on top of the analytic coverage */
                float c = 0;
                for (int sy = 0; sy < 2; sy++)
                    for (int sx = 0; sx < 2; sx++)
                        c += cover_px(&sh, (x + 0.25f + sx * 0.5f) / scale, (y + 0.25f + sy * 0.5f) / scale, scale);
                c *= 0.25f;
                uint8_t *p = rgba + ((size_t)(oy + y) * ICON_ATLAS_W + ox + x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)(c * 255 + 0.5f);
            }
        arr_free(sh.segs);
        arr_free(sh.poly);
        arr_free(sh.poly_start);
    }
}

void icons_raster_single(int id, int size, uint8_t *alpha) {
    Shape sh;
    memset(&sh, 0, sizeof sh);
    if (id >= 0 && id < IC_COUNT && g_icon_src[id]) parse_icon(g_icon_src[id], &sh);
    float scale = size / 24.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float c = 0;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) c += cover_px(&sh, (x + 0.25f + sx * 0.5f) / scale, (y + 0.25f + sy * 0.5f) / scale, scale);
            alpha[y * size + x] = (uint8_t)(c * 0.25f * 255 + 0.5f);
        }
    arr_free(sh.segs);
    arr_free(sh.poly);
    arr_free(sh.poly_start);
}
