/* Sector Forge - vector icon set rasterised into an atlas at startup */
#ifndef SF_ICONS_H
#define SF_ICONS_H
#include "base.h"

#define ICON_LIST(X) \
    X(logo) X(minimize) X(maximize) X(restore) X(close) \
    X(new) X(open) X(save) X(export) X(import) X(undo) X(redo) X(cut) X(copy) X(paste) X(trash) \
    X(select) X(hand) X(draw_sector) X(rect) X(polygon) X(line) X(vertex) X(split) X(join) \
    X(move) X(rotate) X(scale) X(flip) X(flip_v) X(measure) X(grid) X(magnet) \
    X(floor) X(ceiling) X(wall) X(sector) X(height) X(wheel) X(texture) X(bucket) X(eyedropper) X(wireframe) X(light) \
    X(player) X(enemy) X(item) X(door) X(lift) X(thing) X(tag) \
    X(mode2d) X(cube) X(play) X(stop) X(eye) X(layers) X(zoom_in) X(zoom_out) X(fit) X(search) X(settings) X(console) \
    X(chev_down) X(chev_up) X(chev_right) X(chev_left) X(plus) X(minus) X(check) X(clock) X(book) X(template) X(crosshair) \
    X(info) X(warn) X(keyboard) X(menu) X(sky) X(dot)

#define ICON_ENUM(n) IC_##n,
typedef enum { ICON_LIST(ICON_ENUM) IC_COUNT } IconId;
#undef ICON_ENUM

#define ICON_CELL 48
#define ICON_ATLAS_W 1024
#define ICON_ATLAS_H 512
#define ICON_COLS (ICON_ATLAS_W / ICON_CELL)

/* rasterise icons [from, to) into an RGBA atlas (white + alpha) */
void icons_raster(uint8_t *rgba, int from, int to);
const char *icon_name(int id);
void icons_raster_single(int id, int size, uint8_t *alpha);

#endif
