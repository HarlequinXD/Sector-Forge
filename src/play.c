/* Sector Forge - play mode: first-person movement, collision, doors, lifts, pickups, hitscan */
#include "editor.h"
#include "assets.h"
#include "plat.h"
#include "glinc.h"

#define P_RADIUS 16.0f
#define P_HEIGHT 56.0f
#define P_CROUCH 34.0f
#define P_STEP 24.0f
#define P_EYE 0.73f
#define GRAVITY 900.0f
#define JUMP_V 300.0f

static PlayState *P = &ed.play;

static void play_msg(const char *m) { sf_strcpy(P->msg, sizeof P->msg, m); P->msg_time = plat_time(); }

static void free_play(void) {
    free(P->floor_h); free(P->ceil_h); free(P->light); free(P->door_state); free(P->door_timer);
    free(P->thing_hidden); free(P->thing_dead); free(P->thing_hp); free(P->secret_found);
    P->floor_h = P->ceil_h = P->light = NULL;
    P->door_state = NULL; P->door_timer = NULL;
    P->thing_hidden = P->thing_dead = NULL; P->thing_hp = NULL; P->secret_found = NULL;
}

void play_start(bool from_camera) {
    Map *m = &ed.map;
    free_play();
    memset(P, 0, sizeof *P);
    P->nsec = m->sectors.n;
    int ns = SF_MAX(P->nsec, 1), nt = SF_MAX(m->things.n, 1);
    P->floor_h = malloc(sizeof(float) * (size_t)ns);
    P->ceil_h = malloc(sizeof(float) * (size_t)ns);
    P->light = malloc(sizeof(float) * (size_t)ns);
    P->door_state = calloc((size_t)ns, sizeof(int));
    P->door_timer = calloc((size_t)ns, sizeof(float));
    P->secret_found = calloc((size_t)ns, 1);
    for (int s = 0; s < P->nsec; s++) {
        const MSector *S = &m->sectors.a[s];
        P->floor_h[s] = S->floor_h;
        P->ceil_h[s] = S->ceil_h;
        P->light[s] = (float)S->light;
        if (S->type == ST_DOOR) P->ceil_h[s] = S->floor_h;      /* doors start closed */
        if (S->type == ST_SECRET) P->secrets_total++;
    }
    P->nthing = m->things.n;
    P->thing_hidden = calloc((size_t)nt, 1);
    P->thing_dead = calloc((size_t)nt, 1);
    P->thing_hp = calloc((size_t)nt, sizeof(float));
    bool placed = false;
    for (int i = 0; i < m->things.n; i++) {
        const MThing *t = &m->things.a[i];
        int c = g_thing_info[t->type].category;
        if (c == TC_ENEMY) { P->kills_total++; P->thing_hp[i] = t->type == TH_IMP ? 60 : 40; }
        if (c == TC_PICKUP) P->items_total++;
        if (t->type == TH_PLAYER && !placed && !from_camera) {
            P->x = (float)t->x; P->y = (float)t->y; P->yaw = (float)(t->angle * SF_DEG2RAD);
            placed = true;
        }
    }
    if (from_camera || !placed) {
        if (ed.cam_valid) { P->x = ed.cam.x; P->y = ed.cam.y; P->yaw = ed.cam.yaw; P->pitch = SF_CLAMP(ed.cam.pitch, -1.2f, 1.2f); }
        else { P->x = (float)ed.vx; P->y = (float)ed.vy; P->yaw = (float)(SF_PI / 2); }
        if (!placed && !from_camera) play_msg("No player start \xE2\x80\x94 starting from the view");
    }
    int s = map_sector_at(m, P->x, P->y);
    P->z = s >= 0 ? P->floor_h[s] : 0;
    P->eye = P_HEIGHT * P_EYE;
    P->health = 100; P->armor = 0; P->ammo = 50;
    P->start_time = plat_time();
    ed.play_return = ed.mode;
    ed.mode = MODE_PLAY;
    plat_set_relative_mouse(true);
    if (!P->msg[0]) play_msg("Play mode \xE2\x80\x94 Esc returns to the editor");
}

void play_stop(void) {
    plat_set_relative_mouse(false);
    free_play();
    ed.mode = ed.play_return == MODE_3D ? MODE_3D : MODE_2D;
    if (ed.mode == MODE_PLAY) ed.mode = MODE_2D;
}

/* ------------------------------------------------------------------ world queries using live heights */
static int sector_at(float x, float y) { return map_sector_at(&ed.map, x, y); }

static bool line_blocks(const MLine *l, int from_side, float z, float height) {
    if (l->side[1].sector < 0 || l->side[0].sector < 0) return true;
    if (l->flags & LF_BLOCK) return true;
    int os = l->side[1 - from_side].sector;
    float of = P->floor_h[os], oc = P->ceil_h[os];
    if (of - z > P_STEP) return true;
    if (oc - SF_MAX(of, z) < height) return true;
    return false;
}

static void collide(float *px, float *py, float z, float height) {
    Map *m = &ed.map;
    for (int iter = 0; iter < 4; iter++) {
        bool any = false;
        for (int i = 0; i < m->lines.n; i++) {
            const MLine *l = &m->lines.a[i];
            const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
            double t, d = seg_dist(*px, *py, a->x, a->y, b->x, b->y, &t);
            if (d >= P_RADIUS) continue;
            double cr = (b->x - a->x) * (*py - a->y) - (b->y - a->y) * (*px - a->x);
            int side = cr < 0 ? 0 : 1;
            if (!line_blocks(l, side, z, height)) continue;
            t = SF_CLAMP(t, 0.0, 1.0);
            double qx = a->x + (b->x - a->x) * t, qy = a->y + (b->y - a->y) * t;
            double nx = *px - qx, ny = *py - qy, nl = sqrt(nx * nx + ny * ny);
            if (nl < 1e-6) {
                double lx = b->x - a->x, ly = b->y - a->y, ll = sqrt(lx * lx + ly * ly);
                nx = side == 0 ? ly / ll : -ly / ll; ny = side == 0 ? -lx / ll : lx / ll; nl = 1;
            }
            double push = P_RADIUS - d + 0.01;
            *px += (float)(nx / nl * push);
            *py += (float)(ny / nl * push);
            any = true;
        }
        if (!any) break;
    }
}

/* floor & ceiling under the player's circle */
static void env_heights(float x, float y, float *fz, float *cz, int *center_sector) {
    Map *m = &ed.map;
    int s = sector_at(x, y);
    *center_sector = s;
    *fz = s >= 0 ? P->floor_h[s] : -32768;
    *cz = s >= 0 ? P->ceil_h[s] : 32768;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        double t;
        if (seg_dist(x, y, a->x, a->y, b->x, b->y, &t) >= P_RADIUS - 0.5) continue;
        for (int k = 0; k < 2; k++) {
            int ss = l->side[k].sector;
            if (ss < 0) continue;
            if (P->floor_h[ss] > *fz && P->floor_h[ss] - P->z <= P_STEP + 0.5f) *fz = P->floor_h[ss];
            if (P->ceil_h[ss] < *cz) *cz = P->ceil_h[ss];
        }
    }
}

/* ------------------------------------------------------------------ specials */
static void use_line(void) {
    Map *m = &ed.map;
    float cx = cosf(P->yaw), cy = sinf(P->yaw);
    float x0 = P->x, y0 = P->y, x1 = P->x + cx * 72, y1 = P->y + cy * 72;
    double best = 1e9;
    int bl = -1;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        double dx = x1 - x0, dy = y1 - y0, ex = b->x - a->x, ey = b->y - a->y;
        double den = dx * ey - dy * ex;
        if (fabs(den) < 1e-9) continue;
        double t = ((a->x - x0) * ey - (a->y - y0) * ex) / den, u = ((a->x - x0) * dy - (a->y - y0) * dx) / den;
        if (t < 0 || t > 1 || u < 0 || u > 1) continue;
        if (t < best) { best = t; bl = i; }
    }
    int target = -1;
    if (bl >= 0) {
        const MLine *l = &m->lines.a[bl];
        for (int k = 0; k < 2; k++) {
            int s = l->side[k].sector;
            if (s >= 0 && (m->sectors.a[s].type == ST_DOOR || m->sectors.a[s].type == ST_LIFT)) target = s;
        }
    }
    if (target < 0) {
        int s = sector_at(P->x, P->y);
        if (s >= 0 && m->sectors.a[s].type == ST_LIFT) target = s;
    }
    if (target < 0) return;
    if (P->door_state[target] == 0 || P->door_state[target] == 3) { P->door_state[target] = 1; P->door_timer[target] = 0; }
}

static bool touches_sector(int s) {
    Map *m = &ed.map;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        if (l->side[0].sector != s && l->side[1].sector != s) continue;
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        double t;
        if (seg_dist(P->x, P->y, a->x, a->y, b->x, b->y, &t) < P_RADIUS) return true;
    }
    return false;
}

static void update_specials(float dt, int psec) {
    Map *m = &ed.map;
    for (int s = 0; s < P->nsec; s++) {
        const MSector *S = &m->sectors.a[s];
        if (S->type == ST_DOOR) {
            float open = S->ceil_h - 4, closed = S->floor_h;
            if (open < closed) open = S->ceil_h;
            float sp = 160 * dt;
            switch (P->door_state[s]) {
            case 1: P->ceil_h[s] = SF_MIN(open, P->ceil_h[s] + sp); if (P->ceil_h[s] >= open) { P->door_state[s] = 2; P->door_timer[s] = 3.5f; } break;
            case 2: P->door_timer[s] -= dt; if (P->door_timer[s] <= 0) P->door_state[s] = 3; break;
            case 3: {
                float nz = SF_MAX(closed, P->ceil_h[s] - sp);
                bool blocked = (s == psec || touches_sector(s)) && nz < P->z + P_HEIGHT;
                if (blocked) P->door_state[s] = 1;
                else { P->ceil_h[s] = nz; if (nz <= closed) P->door_state[s] = 0; }
            } break;
            default: break;
            }
        } else if (S->type == ST_LIFT) {
            float top = S->floor_h, low = map_lowest_neighbor_floor(m, s, S->floor_h);
            if (low > top) low = top;
            float sp = 140 * dt;
            switch (P->door_state[s]) {
            case 1: P->floor_h[s] = SF_MAX(low, P->floor_h[s] - sp); if (P->floor_h[s] <= low) { P->door_state[s] = 2; P->door_timer[s] = 3.0f; } break;
            case 2: P->door_timer[s] -= dt; if (P->door_timer[s] <= 0) P->door_state[s] = 3; break;
            case 3: P->floor_h[s] = SF_MIN(top, P->floor_h[s] + sp); if (P->floor_h[s] >= top) P->door_state[s] = 0; break;
            default: break;
            }
        } else if (S->type == ST_FLICKER) {
            float base = (float)S->light;
            P->light[s] = (rand() % 100) < 12 ? base * 0.45f : base;
        }
    }
}

static void fire(void) {
    Map *m = &ed.map;
    if (P->ammo <= 0) { play_msg("Out of ammo"); return; }
    P->ammo--;
    P->fire_time = plat_time();
    V3 f = v3(cosf(P->yaw) * cosf(P->pitch), sinf(P->yaw) * cosf(P->pitch), sinf(P->pitch));
    float ex = P->x, ey = P->y, ez = P->z + P->eye;
    /* nearest blocking wall along the ray (2D) */
    double wall = 8192;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        double dx = f.x, dy = f.y, lx = b->x - a->x, ly = b->y - a->y;
        double den = dx * ly - dy * lx;
        if (fabs(den) < 1e-9) continue;
        double t = ((a->x - ex) * ly - (a->y - ey) * lx) / den, u = ((a->x - ex) * dy - (a->y - ey) * dx) / den;
        if (t <= 0 || u < 0 || u > 1) continue;
        double hz = ez + f.z * t / SF_MAX(1e-4, sqrt(f.x * f.x + f.y * f.y));
        bool solid = l->side[1].sector < 0;
        if (!solid) {
            int s0 = l->side[0].sector, s1 = l->side[1].sector;
            float hf = SF_MAX(P->floor_h[s0], P->floor_h[s1]), lc = SF_MIN(P->ceil_h[s0], P->ceil_h[s1]);
            solid = hz < hf || hz > lc;
        }
        if (solid && t < wall) wall = t;
    }
    int hit = -1;
    double best = wall;
    for (int i = 0; i < m->things.n; i++) {
        const MThing *t = &m->things.a[i];
        if (g_thing_info[t->type].category != TC_ENEMY || P->thing_dead[i]) continue;
        double tx = t->x - ex, ty = t->y - ey;
        double along = tx * f.x + ty * f.y;
        if (along <= 0) continue;
        double hd = along / SF_MAX(1e-4, sqrt(f.x * f.x + f.y * f.y));
        double px = ex + f.x * hd, py = ey + f.y * hd;
        double perp = sqrt((px - t->x) * (px - t->x) + (py - t->y) * (py - t->y));
        int s = sector_at((float)t->x, (float)t->y);
        float tz = s >= 0 ? P->floor_h[s] : 0;
        double hz = ez + f.z * hd / SF_MAX(1e-4, sqrt(f.x * f.x + f.y * f.y));
        if (perp < 20 && hz > tz && hz < tz + 64 && hd < best) { best = hd; hit = i; }
    }
    if (hit >= 0) {
        P->thing_hp[hit] -= 20;
        if (P->thing_hp[hit] <= 0) { P->thing_dead[hit] = 1; P->kills++; }
    }
}

static void pickups(void) {
    Map *m = &ed.map;
    for (int i = 0; i < m->things.n; i++) {
        const MThing *t = &m->things.a[i];
        if (P->thing_hidden[i] || g_thing_info[t->type].category != TC_PICKUP) continue;
        double d = sqrt((t->x - P->x) * (t->x - P->x) + (t->y - P->y) * (t->y - P->y));
        if (d > 28) continue;
        if (t->type == TH_HEALTH) { if (P->health >= 100) continue; P->health = SF_MIN(100, P->health + 25); play_msg("Picked up a medikit (+25 health)"); }
        if (t->type == TH_ARMOR) { if (P->armor >= 100) continue; P->armor = SF_MIN(100, P->armor + 50); play_msg("Picked up armor (+50)"); }
        if (t->type == TH_AMMO) { if (P->ammo >= 200) continue; P->ammo = SF_MIN(200, P->ammo + 20); play_msg("Picked up ammo (+20)"); }
        P->thing_hidden[i] = 1;
        P->items++;
        P->pickup_flash = 1;
    }
}

/* ------------------------------------------------------------------ frame */
static void hud_box(float x, float y, const char *label, const char *val, const char *sub, uint32_t color, bool right) {
    float vw = gfx_text_w(FONT_HUD, val) + (sub ? gfx_text_w(FONT_MONO, sub) + 4 : 0);
    float w = SF_MAX(vw, gfx_text_w(FONT_SMALL, label)) + 28, h = 64;
    Rect r = rect(right ? x - w : x, y - h, w, h);
    gfx_rrect(r, 10, col(10, 11, 13, 215));
    gfx_rrect_line(r, 10, 1, col_hex(TH_BORDER2));
    gfx_text(FONT_SMALL, right ? r.x + r.w - 14 - gfx_text_w(FONT_SMALL, label) : r.x + 14, r.y + 8, label, col_hex(0xA3ABB8));
    float vx = right ? r.x + r.w - 14 - vw : r.x + 14;
    float ex = gfx_text(FONT_HUD, vx, r.y + 24, val, col_hex(color));
    if (sub) gfx_text(FONT_MONO, ex + 4, r.y + 36, sub, col_hex(TH_TEXT3));
}

void play_frame(Rect vp, const Input *in) {
    Map *m = &ed.map;
    if (!P->floor_h || P->nsec != m->sectors.n || P->nthing != m->things.n) { play_start(false); return; }
    float dt = (float)SF_MIN(in->dt, 0.05);
    if (in->kpressed[KEY_ESC]) { play_stop(); return; }
    bool alive = P->health > 0 && !P->exited;
    /* look */
    float s = 0.0022f * g_settings.mouse_sens;
    if (alive) {
        P->yaw -= in->mdx * s;
        P->pitch -= in->mdy * s * (g_settings.invert_y ? -1 : 1);
        P->pitch = SF_CLAMP(P->pitch, -1.35f, 1.35f);
    }
    /* move */
    P->crouch = alive && in->kdown[KEY_CTRL];
    float height = P->crouch ? P_CROUCH : P_HEIGHT;
    float target_eye = height * P_EYE;
    P->eye += (target_eye - P->eye) * SF_MIN(1.0f, dt * 12);
    float fx = cosf(P->yaw), fy = sinf(P->yaw), rx = sinf(P->yaw), ry = -cosf(P->yaw);
    float wx = 0, wy = 0;
    if (alive) {
        if (in->kdown['W'] || in->kdown[KEY_UP]) { wx += fx; wy += fy; }
        if (in->kdown['S'] || in->kdown[KEY_DOWN]) { wx -= fx; wy -= fy; }
        if (in->kdown['D']) { wx += rx; wy += ry; }
        if (in->kdown['A']) { wx -= rx; wy -= ry; }
    }
    float wl = sqrtf(wx * wx + wy * wy);
    float speed = (in->kdown[KEY_SHIFT] ? 420.0f : 260.0f) * (P->crouch ? 0.5f : 1.0f);
    if (wl > 0) { wx = wx / wl * speed * dt; wy = wy / wl * speed * dt; }
    int steps = (int)(sqrtf(wx * wx + wy * wy) / 6) + 1;
    for (int i = 0; i < steps; i++) {
        float nx = P->x + wx / steps, ny = P->y + wy / steps;
        collide(&nx, &ny, P->z, height);
        P->x = nx; P->y = ny;
    }
    float fz, cz;
    int psec;
    env_heights(P->x, P->y, &fz, &cz, &psec);
    /* gravity / steps */
    if (P->z > fz + 0.01f) { P->vz -= GRAVITY * dt; P->on_ground = false; }
    P->z += P->vz * dt;
    if (P->z <= fz) {
        if (fz - P->z <= P_STEP + 1 || P->on_ground) P->z += SF_MIN(fz - P->z, 600 * dt + 0.5f);
        if (P->z < fz) P->z = fz;
        if (P->vz < 0) P->vz = 0;
        P->on_ground = true;
    }
    if (P->z + height > cz && cz > fz) { P->z = SF_MAX(fz, cz - height); if (P->vz > 0) P->vz = 0; }
    if (alive && P->on_ground && in->kpressed[KEY_SPACE]) { P->vz = JUMP_V; P->on_ground = false; }
    /* bob */
    if (wl > 0 && P->on_ground) P->bob_t += dt * (speed / 30.0f);
    P->bob = (wl > 0 && P->on_ground) ? sinf(P->bob_t) * 3.0f : P->bob * 0.85f;
    /* actions */
    if (alive && (in->kpressed['E'] || in->kpressed[KEY_ENTER])) use_line();
    if (alive && (in->mpressed[0] || (in->mdown[0] && in->time - P->fire_time > 0.32))) fire();
    update_specials(dt, psec);
    if (alive) pickups();
    if (psec >= 0 && alive) {
        const MSector *S = &m->sectors.a[psec];
        if (S->type == ST_DAMAGE && P->z <= P->floor_h[psec] + 1) {
            P->damage_acc += 20 * dt;
            if (P->damage_acc >= 5) {
                float d = P->damage_acc;
                P->damage_acc = 0;
                float absorb = SF_MIN(P->armor, d * 0.33f);
                P->armor -= absorb;
                P->health -= d - absorb;
                P->hurt_flash = 1;
            }
        }
        if (S->type == ST_SECRET && !P->secret_found[psec]) { P->secret_found[psec] = 1; P->secrets_found++; play_msg("You found a secret area!"); }
        if (S->type == ST_EXIT && !P->exited) { P->exited = true; P->end_time = plat_time(); }
    }
    if (P->health <= 0 && !P->dead) { P->dead = true; P->health = 0; play_msg("You died \xE2\x80\x94 press R to restart or Esc to return"); }
    if ((P->dead || P->exited) && in->kpressed['R']) { play_start(false); return; }
    P->hurt_flash = SF_MAX(0, P->hurt_flash - dt * 2);
    P->pickup_flash = SF_MAX(0, P->pickup_flash - dt * 3);

    /* render */
    Cam3 cam = { P->x, P->y, P->z + P->eye + P->bob * 0.6f, P->yaw, P->pitch, g_settings.fov };
    if (P->dead) cam.z = P->z + 12;
    R3Opts o;
    memset(&o, 0, sizeof o);
    o.floor_h = P->floor_h; o.ceil_h = P->ceil_h; o.light = P->light;
    o.thing_hidden = P->thing_hidden; o.thing_dead = P->thing_dead;
    o.time = in->time;
    r3d_draw(m, &cam, vp, in->scale, in->win_h, &o);
    gfx_begin_2d(in->win_w, in->win_h, in->scale);
    gfx_clip_push(vp);
    /* weapon */
    bool flash = in->time - P->fire_time < 0.07;
    float ws = SF_MIN(vp.h * 0.56f, 440);
    float wxp = vp.x + vp.w * 0.5f - ws * 0.5f + P->bob * 2.5f, wyp = vp.y + vp.h - ws + 6 + fabsf(P->bob) * 2 + (in->time - P->fire_time < 0.1 ? 10 : 0);
    if (!P->dead) gfx_quad_tex(flash ? &g_assets.weapon_flash : &g_assets.weapon, rect(wxp, wyp, ws, ws), 0, 0, 1, 1, col(255, 255, 255, 255));
    /* crosshair */
    float cx = vp.x + vp.w * 0.5f, cy = vp.y + vp.h * 0.5f;
    Col ch = col(255, 255, 255, 220);
    gfx_line(cx, cy - 12, cx, cy - 4, 2, ch); gfx_line(cx, cy + 4, cx, cy + 12, 2, ch);
    gfx_line(cx - 12, cy, cx - 4, cy, 2, ch); gfx_line(cx + 4, cy, cx + 12, cy, 2, ch);
    /* flashes */
    if (P->hurt_flash > 0) gfx_rect(vp, col(255, 40, 30, (int)(90 * P->hurt_flash)));
    if (P->pickup_flash > 0) gfx_rect(vp, col(255, 220, 120, (int)(50 * P->pickup_flash)));
    /* banner */
    {
        const char *t1 = "PLAY MODE";
        char t2[96];
        snprintf(t2, sizeof t2, "%s", m->name);
        float w = gfx_text_w(FONT_UI_BOLD, t1) + gfx_text_w(FONT_SMALL, t2) + gfx_text_w(FONT_SMALL, "return to editor") + 120;
        Rect b = rect(vp.x + (vp.w - w) * 0.5f, vp.y + 14, w, 32);
        gfx_rrect(b, 16, col(10, 11, 13, 225));
        gfx_rrect_line(b, 16, 1, col_hex(TH_ACCENT_BD));
        gfx_circle(b.x + 16, b.y + 16, 4, col_hex(TH_RED));
        float x = gfx_text(FONT_UI_BOLD, b.x + 28, b.y + 16 - gfx_font_h(FONT_UI_BOLD) * 0.5f, t1, col_hex(0xFFFFFF));
        x = gfx_text(FONT_SMALL, x + 12, b.y + 16 - gfx_font_h(FONT_SMALL) * 0.5f, t2, col_hex(TH_TEXT2));
        float kw;
        ui_kbd(x + 12, b.y + 7, "Esc", &kw);
        gfx_text(FONT_SMALL, x + 18 + kw, b.y + 16 - gfx_font_h(FONT_SMALL) * 0.5f, "return to editor", col_hex(TH_TEXT2));
    }
    if (g_settings.show_fps) {
        char f[48];
        snprintf(f, sizeof f, "%.0f fps", ed.fps);
        float w;
        ui_chip(vp.x + vp.w - 90, vp.y + 18, -1, f, false, &w);
    }
    /* HUD */
    char hb[16], ab[16], mb[16], sb[16];
    snprintf(hb, sizeof hb, "%d", (int)ceilf(P->health));
    snprintf(ab, sizeof ab, "%d", (int)P->armor);
    snprintf(mb, sizeof mb, "%d", P->ammo);
    snprintf(sb, sizeof sb, "/ 200");
    hud_box(vp.x + 16, vp.y + vp.h - 16, "HEALTH", hb, NULL, P->health > 30 ? TH_GREEN : TH_RED, false);
    hud_box(vp.x + 16 + 130, vp.y + vp.h - 16, "ARMOR", ab, NULL, TH_BLUE, false);
    hud_box(vp.x + vp.w - 16, vp.y + vp.h - 16, "AMMO", mb, sb, TH_ACCENT_HI, true);
    if (P->msg[0] && in->time - P->msg_time < 3.0) {
        float w = gfx_text_w(FONT_UI, P->msg) + 28;
        Rect r = rect(vp.x + (vp.w - w) * 0.5f, vp.y + 60, w, 30);
        gfx_rrect(r, 8, col(10, 11, 13, 210));
        gfx_text_center(FONT_UI, r, P->msg, col_hex(TH_TEXT));
    }
    if (P->exited || P->dead) {
        Rect r = rect(vp.x + vp.w * 0.5f - 200, vp.y + vp.h * 0.5f - 90, 400, 180);
        gfx_rect(vp, col(0, 0, 0, 110));
        gfx_rrect(r, 14, col(14, 15, 18, 240));
        gfx_rrect_line(r, 14, 1, col_hex(P->dead ? 0x7A2A1E : TH_ACCENT_BD));
        gfx_text_center(FONT_HEAD, rect(r.x, r.y + 16, r.w, 30), P->dead ? "You died" : "Level complete", col_hex(0xFFFFFF));
        char l[96];
        double tt = (P->exited ? P->end_time : plat_time()) - P->start_time;
        snprintf(l, sizeof l, "Kills %d / %d   \xC2\xB7   Items %d / %d   \xC2\xB7   Secrets %d / %d", P->kills, P->kills_total, P->items, P->items_total, P->secrets_found, P->secrets_total);
        gfx_text_center(FONT_UI, rect(r.x, r.y + 64, r.w, 24), l, col_hex(TH_TEXT2));
        snprintf(l, sizeof l, "Time %d:%02d", (int)tt / 60, (int)tt % 60);
        gfx_text_center(FONT_MONO, rect(r.x, r.y + 92, r.w, 24), l, col_hex(TH_ACCENT_HI));
        gfx_text_center(FONT_SMALL, rect(r.x, r.y + 132, r.w, 24), "R  restart    \xC2\xB7    Esc  back to the editor", col_hex(TH_TEXT3));
    }
    gfx_clip_pop();
}
