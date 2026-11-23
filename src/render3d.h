/* Sector Forge - 3D sector renderer (OpenGL 1.1 fixed function) with ID-buffer picking */
#ifndef SF_RENDER3D_H
#define SF_RENDER3D_H
#include "map.h"
#include "gfx.h"

typedef struct { float x, y, z, yaw, pitch, fov; } Cam3;

enum { SURF_NONE, SURF_FLOOR, SURF_CEIL, SURF_UPPER, SURF_MID, SURF_LOWER, SURF_THING };
#define SURF_ID(kind, idx) (((uint32_t)(kind) << 24) | ((uint32_t)(idx)&0xFFFFFFu))
#define SURF_KIND(id) ((int)((id) >> 24))
#define SURF_IDX(id) ((int)((id)&0xFFFFFFu))

typedef struct {
    const float *floor_h, *ceil_h;   /* per-sector overrides (play mode) */
    const float *light;              /* per-sector light override */
    bool fullbright, wireframe, editor, show_player_start;
    uint32_t hover;
    const uint32_t *sel;
    int nsel;
    const uint8_t *thing_hidden;     /* per thing */
    const uint8_t *thing_dead;
    double time;
} R3Opts;

void r3d_invalidate(void);
void r3d_draw(const Map *m, const Cam3 *c, Rect vp, float scale, float win_h, const R3Opts *o);
uint32_t r3d_pick(const Map *m, const Cam3 *c, Rect vp, float scale, float win_h, float mx, float my, const R3Opts *o);
bool r3d_project(V3 p, float *sx, float *sy);
void r3d_cam_vectors(const Cam3 *c, V3 *fwd, V3 *right, V3 *up);
float r3d_sector_floor(const Map *m, const R3Opts *o, int s);
float r3d_sector_ceil(const Map *m, const R3Opts *o, int s);
const float *r3d_sector_tris(const Map *m, int s, int *ntri);  /* xy pairs, 3 per triangle */

#endif
