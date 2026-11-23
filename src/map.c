/* Sector Forge - map model, planar face tracing and sector editing operations */
#include "map.h"

const ThingInfo g_thing_info[TH_COUNT] = {
    { "Player Start", TC_START,  0x3DDC84, 16, 56, "player" },
    { "Torch Light",  TC_LIGHT,  0xFFD23D, 10, 64, "light" },
    { "Imp",          TC_ENEMY,  0xFF4D4D, 20, 56, "enemy" },
    { "Soldier",      TC_ENEMY,  0xFF7A5C, 20, 56, "enemy" },
    { "Health",       TC_PICKUP, 0x3DDC84, 14, 20, "item" },
    { "Armor",        TC_PICKUP, 0x4DA3FF, 14, 20, "item" },
    { "Ammo",         TC_PICKUP, 0xFFB27A, 14, 16, "item" },
    { "Barrel",       TC_PROP,   0x9AA3B0, 14, 40, "thing" },
    { "Lamp Column",  TC_LIGHT,  0xFFE08A, 12, 80, "light" },
    { "Dead Tree",    TC_PROP,   0x8E7A5A, 18, 96, "thing" },
};

const char *g_sector_type_names[ST_COUNT] = { "Normal", "Door", "Lift", "Damage", "Secret", "Flicker Light", "Exit" };

/* ------------------------------------------------------------------ basics */
void map_init(Map *m) { memset(m, 0, sizeof *m); sf_strcpy(m->name, sizeof m->name, "Untitled"); }

void map_free(Map *m) {
    arr_free(m->verts); arr_free(m->lines); arr_free(m->sectors); arr_free(m->things);
}

#define COPY_ARR(D, S) do { arr_clear(D); arr_reserve(D, (S).n); if ((S).n) memcpy((D).a, (S).a, sizeof(*(S).a) * (size_t)(S).n); (D).n = (S).n; } while (0)

void map_copy(Map *dst, const Map *src) {
    COPY_ARR(dst->verts, src->verts);
    COPY_ARR(dst->lines, src->lines);
    COPY_ARR(dst->sectors, src->sectors);
    COPY_ARR(dst->things, src->things);
    memcpy(dst->name, src->name, sizeof dst->name);
    dst->has_cam = src->has_cam;
    dst->cam_x = src->cam_x; dst->cam_y = src->cam_y; dst->cam_z = src->cam_z;
    dst->cam_yaw = src->cam_yaw; dst->cam_pitch = src->cam_pitch;
    dst->geo_rev = src->geo_rev + 1;
}

void map_defaults_init(MapDefaults *d) {
    d->floor_h = 0; d->ceil_h = 128; d->light = 192;
    sf_strcpy(d->floor_tex, TEXNAME, "FLOOR_STONE");
    sf_strcpy(d->ceil_tex, TEXNAME, "CEIL_TECH");
    sf_strcpy(d->wall_tex, TEXNAME, "BRICK_RED");
}

void sector_init(MSector *s, const MapDefaults *d) {
    memset(s, 0, sizeof *s);
    s->floor_h = d->floor_h; s->ceil_h = d->ceil_h; s->light = d->light;
    sf_strcpy(s->floor_tex, TEXNAME, d->floor_tex);
    sf_strcpy(s->ceil_tex, TEXNAME, d->ceil_tex);
    s->fscale = s->cscale = 1;
}

void side_init(MSide *s, int sector, const char *tex) {
    memset(s, 0, sizeof *s);
    s->sector = sector;
    sf_strcpy(s->upper, TEXNAME, tex);
    sf_strcpy(s->middle, TEXNAME, tex);
    sf_strcpy(s->lower, TEXNAME, tex);
}

/* ------------------------------------------------------------------ geometry helpers */
double seg_dist(double px, double py, double ax, double ay, double bx, double by, double *tout) {
    double dx = bx - ax, dy = by - ay;
    double l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    if (tout) *tout = t;
    t = SF_CLAMP(t, 0.0, 1.0);
    double qx = ax + dx * t - px, qy = ay + dy * t - py;
    return sqrt(qx * qx + qy * qy);
}

bool poly_contains(const D2 *p, int n, double x, double y) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((p[i].y > y) != (p[j].y > y)) {
            double xi = p[j].x + (y - p[j].y) * (p[i].x - p[j].x) / (p[i].y - p[j].y);
            if (x < xi) in = !in;
        }
    }
    return in;
}

int map_find_vertex(const Map *m, double x, double y, double eps) {
    int best = -1;
    double bd = eps;
    for (int i = 0; i < m->verts.n; i++) {
        double d = fabs(m->verts.a[i].x - x) + fabs(m->verts.a[i].y - y);
        if (d <= bd) { bd = d; best = i; }
    }
    return best;
}

int map_find_line(const Map *m, int a, int b) {
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        if ((l->v1 == a && l->v2 == b) || (l->v1 == b && l->v2 == a)) return i;
    }
    return -1;
}

double map_line_length(const Map *m, int l) {
    const MVert *a = &m->verts.a[m->lines.a[l].v1], *b = &m->verts.a[m->lines.a[l].v2];
    return sqrt((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y));
}

/* Ray cast towards +x: the nearest line crossed decides which sector we are in. */
int map_sector_at(const Map *m, double x, double y) {
    double best = 1e30;
    int bl = -1;
    bool front = false;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        if ((a->y > y) == (b->y > y)) continue;
        double xi = a->x + (y - a->y) * (b->x - a->x) / (b->y - a->y);
        if (xi < x) continue;
        double d = xi - x;
        if (d < best) {
            best = d; bl = i;
            double cr = (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
            front = cr < 0;
        }
    }
    if (bl < 0) return -1;
    return m->lines.a[bl].side[front ? 0 : 1].sector;
}

bool map_bounds(const Map *m, double *x0, double *y0, double *x1, double *y1) {
    if (!m->verts.n && !m->things.n) return false;
    *x0 = *y0 = 1e30; *x1 = *y1 = -1e30;
    for (int i = 0; i < m->verts.n; i++) {
        *x0 = SF_MIN(*x0, m->verts.a[i].x); *x1 = SF_MAX(*x1, m->verts.a[i].x);
        *y0 = SF_MIN(*y0, m->verts.a[i].y); *y1 = SF_MAX(*y1, m->verts.a[i].y);
    }
    for (int i = 0; i < m->things.n; i++) {
        *x0 = SF_MIN(*x0, m->things.a[i].x); *x1 = SF_MAX(*x1, m->things.a[i].x);
        *y0 = SF_MIN(*y0, m->things.a[i].y); *y1 = SF_MAX(*y1, m->things.a[i].y);
    }
    return true;
}

int map_sector_line_count(const Map *m, int s) {
    int n = 0;
    for (int i = 0; i < m->lines.n; i++)
        if (m->lines.a[i].side[0].sector == s || m->lines.a[i].side[1].sector == s) n++;
    return n;
}

static float neighbor_scan(const Map *m, int s, float fallback, int what) {
    bool any = false;
    float v = fallback;
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        int o = -1;
        if (l->side[0].sector == s) o = l->side[1].sector;
        else if (l->side[1].sector == s) o = l->side[0].sector;
        if (o < 0 || o == s) continue;
        const MSector *os = &m->sectors.a[o];
        float c = what == 0 ? os->ceil_h : os->floor_h;
        if (!any) { v = c; any = true; }
        else if (what == 2 ? c > v : c < v) v = c;
    }
    return v;
}
float map_lowest_neighbor_ceil(const Map *m, int s, float fb) { return neighbor_scan(m, s, fb, 0); }
float map_lowest_neighbor_floor(const Map *m, int s, float fb) { return neighbor_scan(m, s, fb, 1); }
float map_highest_neighbor_floor(const Map *m, int s, float fb) { return neighbor_scan(m, s, fb, 2); }

/* ------------------------------------------------------------------ half-edge face tracing
 * half-edge h: line = h>>1; h even = v1->v2 (left side = back), h odd = v2->v1 (left side = front)
 * Faces lie on the LEFT of half-edges; CCW (positive area) loops are bounded faces. */
static inline int he_org(const Map *m, int h) { const MLine *l = &m->lines.a[h >> 1]; return (h & 1) ? l->v2 : l->v1; }
static inline int he_dst(const Map *m, int h) { const MLine *l = &m->lines.a[h >> 1]; return (h & 1) ? l->v1 : l->v2; }
static inline int he_side(int h) { return (h & 1) ? 0 : 1; }

typedef struct {
    int *start, *list;   /* per-vertex outgoing half-edges sorted by angle */
    double *ang;
    int *pos;            /* index of half-edge within its vertex list */
} HeIndex;

static int g_sort_m_dummy;
static const double *g_sort_ang;
static int cmp_he(const void *a, const void *b) {
    double x = g_sort_ang[*(const int *)a], y = g_sort_ang[*(const int *)b];
    return x < y ? -1 : x > y ? 1 : 0;
}

/* include: optional per-half-edge filter (NULL = all) */
static void he_build(const Map *m, HeIndex *hi, const uint8_t *include) {
    int nh = m->lines.n * 2, nv = m->verts.n;
    hi->start = calloc((size_t)nv + 1, sizeof(int));
    hi->list = malloc(sizeof(int) * (size_t)(nh ? nh : 1));
    hi->ang = malloc(sizeof(double) * (size_t)(nh ? nh : 1));
    hi->pos = malloc(sizeof(int) * (size_t)(nh ? nh : 1));
    for (int h = 0; h < nh; h++) {
        hi->pos[h] = -1;
        if (include && !include[h]) continue;
        const MVert *a = &m->verts.a[he_org(m, h)], *b = &m->verts.a[he_dst(m, h)];
        hi->ang[h] = atan2(b->y - a->y, b->x - a->x);
        hi->start[he_org(m, h) + 1]++;
    }
    for (int v = 0; v < nv; v++) hi->start[v + 1] += hi->start[v];
    int *fill = calloc((size_t)nv, sizeof(int));
    for (int h = 0; h < nh; h++) {
        if (include && !include[h]) continue;
        int v = he_org(m, h);
        hi->list[hi->start[v] + fill[v]++] = h;
    }
    free(fill);
    g_sort_ang = hi->ang;
    (void)g_sort_m_dummy;
    for (int v = 0; v < nv; v++) {
        int c = hi->start[v + 1] - hi->start[v];
        if (c > 1) qsort(hi->list + hi->start[v], (size_t)c, sizeof(int), cmp_he);
        for (int k = 0; k < c; k++) hi->pos[hi->list[hi->start[v] + k]] = k;
    }
}

static void he_free(HeIndex *hi) { free(hi->start); free(hi->list); free(hi->ang); free(hi->pos); }

static int he_next(const Map *m, const HeIndex *hi, int h) {
    int b = he_dst(m, h);
    int twin = h ^ 1;
    int s = hi->start[b], c = hi->start[b + 1] - s;
    int p = hi->pos[twin];
    if (p < 0 || c == 0) return twin;
    int k = (p - 1 + c) % c;
    return hi->list[s + k];
}

typedef struct {
    IntArr hes;     /* half-edges of the loop */
    double area;
    double sx, sy;  /* sample point */
    int parent;     /* for holes: index of containing positive loop */
    int sector;
    bool touches_new;
} Loop;

static double loop_area(const Map *m, const Loop *L) {
    double a = 0;
    for (int i = 0; i < L->hes.n; i++) {
        const MVert *p = &m->verts.a[he_org(m, L->hes.a[i])], *q = &m->verts.a[he_dst(m, L->hes.a[i])];
        a += p->x * q->y - q->x * p->y;
    }
    return a * 0.5;
}

static void loop_edge_sample(const Map *m, const Loop *L, double *sx, double *sy) {
    double best = -1;
    *sx = *sy = 0;
    for (int i = 0; i < L->hes.n; i++) {
        const MVert *p = &m->verts.a[he_org(m, L->hes.a[i])], *q = &m->verts.a[he_dst(m, L->hes.a[i])];
        double dx = q->x - p->x, dy = q->y - p->y, len = sqrt(dx * dx + dy * dy);
        if (len > best) {
            best = len;
            double e = SF_MIN(0.25, len * 0.25);
            *sx = (p->x + q->x) * 0.5 - dy / len * e;
            *sy = (p->y + q->y) * 0.5 + dx / len * e;
        }
    }
}

static bool loop_contains(const Map *m, const Loop *L, double x, double y) {
    bool in = false;
    for (int i = 0; i < L->hes.n; i++) {
        const MVert *a = &m->verts.a[he_org(m, L->hes.a[i])], *b = &m->verts.a[he_dst(m, L->hes.a[i])];
        if ((a->y > y) != (b->y > y)) {
            double xi = a->x + (y - a->y) * (b->x - a->x) / (b->y - a->y);
            if (x < xi) in = !in;
        }
    }
    return in;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* find a robust interior point of a face given its loops (outer + holes), even-odd */
static void face_sample(const Map *m, Loop *loops, int outer, const IntArr *holes) {
    Loop *O = &loops[outer];
    double y0 = 1e30, y1 = -1e30;
    for (int i = 0; i < O->hes.n; i++) {
        double y = m->verts.a[he_org(m, O->hes.a[i])].y;
        y0 = SF_MIN(y0, y); y1 = SF_MAX(y1, y);
    }
    static const double fr[] = { 0.5, 0.37, 0.63, 0.21, 0.79, 0.11, 0.89, 0.04, 0.96 };
    double bestw = -1, bx = 0, by = 0;
    ARR(double) xs = { 0 };
    for (int f = 0; f < SF_COUNT(fr); f++) {
        double y = y0 + (y1 - y0) * fr[f] + (y1 - y0) * 1.37e-5;
        arr_clear(xs);
        for (int k = -1; k < holes->n; k++) {
            Loop *L = k < 0 ? O : &loops[holes->a[k]];
            for (int i = 0; i < L->hes.n; i++) {
                const MVert *a = &m->verts.a[he_org(m, L->hes.a[i])], *b = &m->verts.a[he_dst(m, L->hes.a[i])];
                if ((a->y > y) != (b->y > y)) {
                    double xi = a->x + (y - a->y) * (b->x - a->x) / (b->y - a->y);
                    arr_push(xs, xi);
                }
            }
        }
        if (xs.n >= 2) {
            qsort(xs.a, (size_t)xs.n, sizeof(double), cmp_double);
            for (int i = 0; i + 1 < xs.n; i += 2) {
                double w = xs.a[i + 1] - xs.a[i];
                if (w > bestw) { bestw = w; bx = (xs.a[i] + xs.a[i + 1]) * 0.5; by = y; }
            }
        }
        if (bestw > 1.0) break;
    }
    arr_free(xs);
    if (bestw > 1e-6) { O->sx = bx; O->sy = by; }
    else loop_edge_sample(m, O, &O->sx, &O->sy);
}

static int add_sector_copy(Map *m, int tpl, const MapDefaults *def) {
    MSector s;
    if (tpl >= 0 && tpl < m->sectors.n) { s = m->sectors.a[tpl]; s.sel = 0; s.mark = 0; s.name[0] = 0; }
    else sector_init(&s, def);
    arr_push(m->sectors, s);
    return m->sectors.n - 1;
}

/* Rebuild sector assignment of every line side from the planar faces.
 * old:  map state before the edit (decides which sector a face used to belong to)
 * poly: optional closed outline; faces inside it become new sectors
 * Lines with mark != 0 are "new"; faces touching them that share a sector get split apart. */
static void map_retrace(Map *m, const Map *old, const D2 *poly, int npoly, const MapDefaults *def) {
    int nh = m->lines.n * 2;
    if (!nh) return;
    HeIndex hi;
    he_build(m, &hi, NULL);
    uint8_t *vis = calloc((size_t)nh, 1);
    ARR(Loop) loops = { 0 };
    for (int h = 0; h < nh; h++) {
        if (vis[h]) continue;
        Loop L;
        memset(&L, 0, sizeof L);
        L.parent = -1;
        int e = h, guard = 0;
        while (!vis[e] && guard++ <= nh) {
            vis[e] = 1;
            arr_push(L.hes, e);
            if (m->lines.a[e >> 1].mark) L.touches_new = true;
            e = he_next(m, &hi, e);
        }
        L.area = loop_area(m, &L);
        arr_push(loops, L);
    }
    free(vis);
    he_free(&hi);

    /* holes -> containing positive loop (smallest area) */
    for (int i = 0; i < loops.n; i++) {
        Loop *L = &loops.a[i];
        if (L->area > 1e-9) continue;
        loop_edge_sample(m, L, &L->sx, &L->sy);
        double ba = 1e300;
        for (int j = 0; j < loops.n; j++) {
            Loop *P = &loops.a[j];
            if (P->area <= 1e-9 || P->area >= ba) continue;
            if (loop_contains(m, P, L->sx, L->sy)) { ba = P->area; L->parent = j; }
        }
    }
    /* face samples for positive loops (including their holes) */
    IntArr holes = { 0 };
    for (int i = 0; i < loops.n; i++) {
        if (loops.a[i].area <= 1e-9) continue;
        arr_clear(holes);
        for (int j = 0; j < loops.n; j++) if (loops.a[j].parent == i) arr_push(holes, j);
        face_sample(m, loops.a, i, &holes);
        for (int j = 0; j < holes.n; j++) if (loops.a[holes.a[j]].touches_new) loops.a[i].touches_new = true;
    }
    arr_free(holes);

    /* classify faces */
    int nsec_old = m->sectors.n;
    uint8_t *claimed = calloc((size_t)(nsec_old + 1), 1);
    for (int pass = 0; pass < 2; pass++) {
        /* pass 0: faces not touching new lines claim first; pass 1: the rest */
        for (int i = 0; i < loops.n; i++) {
            Loop *L = &loops.a[i];
            bool face = L->area > 1e-9;
            if (face && (pass == 0) == L->touches_new) continue;
            if (!face && pass == 1) continue;
            if (!face && L->parent >= 0) continue; /* resolved via parent */
            int s;
            if (face && poly && L->touches_new && poly_contains(poly, npoly, L->sx, L->sy)) {
                int tpl = map_sector_at(old, L->sx, L->sy);
                s = add_sector_copy(m, tpl, def);
            } else {
                s = map_sector_at(old, L->sx, L->sy);
                if (s >= nsec_old) s = -1;
                if (face && s >= 0) {
                    if (claimed[s] && L->touches_new) s = add_sector_copy(m, s, def);
                    else claimed[s] = 1;
                }
            }
            L->sector = s;
        }
    }
    free(claimed);
    for (int i = 0; i < loops.n; i++) {
        Loop *L = &loops.a[i];
        if (L->area <= 1e-9 && L->parent >= 0) L->sector = loops.a[L->parent].sector;
    }
    /* apply */
    for (int i = 0; i < loops.n; i++) {
        Loop *L = &loops.a[i];
        for (int k = 0; k < L->hes.n; k++) {
            int h = L->hes.a[k];
            MSide *sd = &m->lines.a[h >> 1].side[he_side(h)];
            sd->sector = L->sector;
        }
        arr_free(L->hes);
    }
    arr_free(loops);
}

/* ------------------------------------------------------------------ edits */
static int add_line(Map *m, int a, int b, const MapDefaults *def) {
    MLine l;
    memset(&l, 0, sizeof l);
    l.v1 = a; l.v2 = b;
    side_init(&l.side[0], -1, def ? def->wall_tex : "BRICK_RED");
    side_init(&l.side[1], -1, def ? def->wall_tex : "BRICK_RED");
    arr_push(m->lines, l);
    return m->lines.n - 1;
}

int map_split_line(Map *m, int li, int v) {
    MLine nl = m->lines.a[li];
    m->lines.a[li].v2 = v;
    nl.v1 = v;
    nl.sel = 0;
    arr_push(m->lines, nl);
    m->geo_rev++;
    return m->lines.n - 1;
}

int map_add_vertex(Map *m, double x, double y) {
    int v = map_find_vertex(m, x, y, MAP_EPS);
    if (v >= 0) return v;
    MVert nv = { x, y, 0, 0 };
    arr_push(m->verts, nv);
    v = m->verts.n - 1;
    int nl = m->lines.n;
    for (int i = 0; i < nl; i++) {
        const MLine *l = &m->lines.a[i];
        const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
        double t;
        if (seg_dist(x, y, a->x, a->y, b->x, b->y, &t) < MAP_EPS && t > 0 && t < 1) {
            map_split_line(m, i, v);
            break;
        }
    }
    m->geo_rev++;
    return v;
}

typedef struct { double t; int v; } SplitPt;
static int cmp_split(const void *a, const void *b) {
    double x = ((const SplitPt *)a)->t, y = ((const SplitPt *)b)->t;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void add_segment(Map *m, int va, int vb, const MapDefaults *def) {
    if (va == vb) return;
    ARR(SplitPt) sp = { 0 };
    double ax = m->verts.a[va].x, ay = m->verts.a[va].y, bx = m->verts.a[vb].x, by = m->verts.a[vb].y;
    double dx = bx - ax, dy = by - ay;
    /* proper crossings with existing lines */
    int nl = m->lines.n;
    for (int i = 0; i < nl; i++) {
        MLine *l = &m->lines.a[i];
        if (l->v1 == va || l->v1 == vb || l->v2 == va || l->v2 == vb) continue;
        double cx = m->verts.a[l->v1].x, cy = m->verts.a[l->v1].y;
        double ex = m->verts.a[l->v2].x - cx, ey = m->verts.a[l->v2].y - cy;
        double den = dx * ey - dy * ex;
        if (fabs(den) < 1e-12) continue;
        double t = ((cx - ax) * ey - (cy - ay) * ex) / den;
        double u = ((cx - ax) * dy - (cy - ay) * dx) / den;
        double tl = sqrt(dx * dx + dy * dy), ul = sqrt(ex * ex + ey * ey);
        double te = MAP_EPS / tl, ue = MAP_EPS / ul;
        if (t <= te || t >= 1 - te || u <= ue || u >= 1 - ue) continue;
        double px = ax + dx * t, py = ay + dy * t;
        int v = map_find_vertex(m, px, py, MAP_EPS);
        if (v < 0) {
            MVert nv = { px, py, 0, 0 };
            arr_push(m->verts, nv);
            v = m->verts.n - 1;
        }
        map_split_line(m, i, v);
    }
    /* any vertex lying on the segment */
    for (int i = 0; i < m->verts.n; i++) {
        if (i == va || i == vb) continue;
        double t;
        if (seg_dist(m->verts.a[i].x, m->verts.a[i].y, ax, ay, bx, by, &t) < MAP_EPS && t > 0 && t < 1) {
            SplitPt s = { t, i };
            arr_push(sp, s);
        }
    }
    if (sp.n > 1) qsort(sp.a, (size_t)sp.n, sizeof(SplitPt), cmp_split);
    int prev = va;
    for (int k = 0; k <= sp.n; k++) {
        int cur = k < sp.n ? sp.a[k].v : vb;
        if (cur == prev) continue;
        int l = map_find_line(m, prev, cur);
        if (l < 0) l = add_line(m, prev, cur, def);
        m->lines.a[l].mark = 1;
        prev = cur;
    }
    arr_free(sp);
}

void map_draw_shape(Map *m, const D2 *pts, int n, bool closed, const MapDefaults *def) {
    if (n < 2 || (closed && n < 3)) return;
    Map old;
    map_init(&old);
    map_copy(&old, m);
    for (int i = 0; i < m->lines.n; i++) m->lines.a[i].mark = 0;
    int *vi = malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) vi[i] = map_add_vertex(m, pts[i].x, pts[i].y);
    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; i++) add_segment(m, vi[i], vi[(i + 1) % n], def);
    free(vi);
    map_retrace(m, &old, closed ? pts : NULL, closed ? n : 0, def);
    /* give brand new sides a texture */
    for (int i = 0; i < m->lines.n; i++) m->lines.a[i].mark = 0;
    map_cleanup(m);
    map_free(&old);
    m->geo_rev++;
}

static void flip_line(MLine *l) {
    int t = l->v1; l->v1 = l->v2; l->v2 = t;
    MSide s = l->side[0]; l->side[0] = l->side[1]; l->side[1] = s;
}

void map_cleanup(Map *m) {
    /* zero length lines */
    for (int i = 0; i < m->lines.n; i++) {
        MLine *l = &m->lines.a[i];
        if (l->v1 == l->v2 || map_line_length(m, i) < MAP_EPS) {
            /* merge vertex v2 into v1 */
            int keep = l->v1, drop = l->v2;
            arr_remove(m->lines, i);
            i--;
            if (keep != drop)
                for (int k = 0; k < m->lines.n; k++) {
                    if (m->lines.a[k].v1 == drop) m->lines.a[k].v1 = keep;
                    if (m->lines.a[k].v2 == drop) m->lines.a[k].v2 = keep;
                }
        }
    }
    /* duplicate lines -> stitch */
    for (int i = 0; i < m->lines.n; i++) {
        for (int j = i + 1; j < m->lines.n; j++) {
            MLine *a = &m->lines.a[i], *b = &m->lines.a[j];
            if (!((a->v1 == b->v1 && a->v2 == b->v2) || (a->v1 == b->v2 && a->v2 == b->v1))) continue;
            if (a->v1 == b->v2) { /* opposite direction: b's front faces a's back */
                if (a->side[1].sector < 0 || a->side[1].sector == a->side[0].sector) a->side[1] = b->side[0];
            } else {
                if (a->side[1].sector < 0) a->side[1] = b->side[1];
            }
            a->sel |= b->sel;
            arr_remove(m->lines, j);
            j--;
        }
    }
    /* front side must exist */
    for (int i = 0; i < m->lines.n; i++) {
        MLine *l = &m->lines.a[i];
        if (l->side[0].sector < 0 && l->side[1].sector >= 0) flip_line(l);
    }
    /* unused vertices */
    int *remap = malloc(sizeof(int) * (size_t)(m->verts.n ? m->verts.n : 1));
    uint8_t *used = calloc((size_t)(m->verts.n ? m->verts.n : 1), 1);
    for (int i = 0; i < m->lines.n; i++) { used[m->lines.a[i].v1] = 1; used[m->lines.a[i].v2] = 1; }
    int nv = 0;
    for (int i = 0; i < m->verts.n; i++) {
        if (used[i]) { m->verts.a[nv] = m->verts.a[i]; remap[i] = nv++; }
        else remap[i] = -1;
    }
    m->verts.n = nv;
    for (int i = 0; i < m->lines.n; i++) { m->lines.a[i].v1 = remap[m->lines.a[i].v1]; m->lines.a[i].v2 = remap[m->lines.a[i].v2]; }
    free(remap); free(used);
    /* unused sectors */
    int ns = m->sectors.n;
    if (ns) {
        int *sr = malloc(sizeof(int) * (size_t)ns);
        uint8_t *su = calloc((size_t)ns, 1);
        for (int i = 0; i < m->lines.n; i++)
            for (int k = 0; k < 2; k++) if (m->lines.a[i].side[k].sector >= 0) su[m->lines.a[i].side[k].sector] = 1;
        int n = 0;
        for (int i = 0; i < ns; i++) {
            if (su[i]) { m->sectors.a[n] = m->sectors.a[i]; sr[i] = n++; }
            else sr[i] = -1;
        }
        m->sectors.n = n;
        for (int i = 0; i < m->lines.n; i++)
            for (int k = 0; k < 2; k++) if (m->lines.a[i].side[k].sector >= 0) m->lines.a[i].side[k].sector = sr[m->lines.a[i].side[k].sector];
        free(sr); free(su);
    }
    m->geo_rev++;
}

void map_select_none(Map *m) {
    for (int i = 0; i < m->verts.n; i++) m->verts.a[i].sel = 0;
    for (int i = 0; i < m->lines.n; i++) m->lines.a[i].sel = 0;
    for (int i = 0; i < m->sectors.n; i++) m->sectors.a[i].sel = 0;
    for (int i = 0; i < m->things.n; i++) m->things.a[i].sel = 0;
}

int map_sel_count(const Map *m, int sm) {
    int n = 0;
    if (sm == SM_VERTS) for (int i = 0; i < m->verts.n; i++) n += m->verts.a[i].sel ? 1 : 0;
    if (sm == SM_LINES) for (int i = 0; i < m->lines.n; i++) n += m->lines.a[i].sel ? 1 : 0;
    if (sm == SM_SECTORS) for (int i = 0; i < m->sectors.n; i++) n += m->sectors.a[i].sel ? 1 : 0;
    if (sm == SM_THINGS) for (int i = 0; i < m->things.n; i++) n += m->things.a[i].sel ? 1 : 0;
    return n;
}

static void remove_vertex_lines_and_retrace(Map *m, const uint8_t *kill_line) {
    Map old;
    map_init(&old);
    map_copy(&old, m);
    int n = 0;
    for (int i = 0; i < m->lines.n; i++) if (!kill_line[i]) m->lines.a[n++] = m->lines.a[i];
    m->lines.n = n;
    for (int i = 0; i < m->lines.n; i++) m->lines.a[i].mark = 0;
    map_retrace(m, &old, NULL, 0, NULL);
    map_cleanup(m);
    map_free(&old);
}

void map_delete_selection(Map *m, int sm) {
    if (sm == SM_THINGS) {
        int n = 0;
        for (int i = 0; i < m->things.n; i++) if (!m->things.a[i].sel) m->things.a[n++] = m->things.a[i];
        m->things.n = n;
        return;
    }
    if (sm == SM_SECTORS) {
        for (int i = 0; i < m->lines.n; i++) {
            MLine *l = &m->lines.a[i];
            for (int k = 0; k < 2; k++)
                if (l->side[k].sector >= 0 && m->sectors.a[l->side[k].sector].sel) l->side[k].sector = -1;
        }
        int n = 0;
        for (int i = 0; i < m->lines.n; i++) {
            MLine *l = &m->lines.a[i];
            if (l->side[0].sector < 0 && l->side[1].sector < 0) continue;
            m->lines.a[n++] = *l;
        }
        m->lines.n = n;
        /* things standing in deleted sectors go too */
        map_cleanup(m);
        return;
    }
    uint8_t *kill = calloc((size_t)(m->lines.n ? m->lines.n : 1), 1);
    if (sm == SM_LINES) {
        for (int i = 0; i < m->lines.n; i++) kill[i] = m->lines.a[i].sel;
    } else if (sm == SM_VERTS) {
        /* degree-2 vertices merge their two lines; others remove attached lines */
        for (int v = 0; v < m->verts.n; v++) {
            if (!m->verts.a[v].sel) continue;
            int deg = 0, la = -1, lb = -1;
            for (int i = 0; i < m->lines.n; i++) {
                if (kill[i]) continue;
                if (m->lines.a[i].v1 == v || m->lines.a[i].v2 == v) { deg++; if (la < 0) la = i; else lb = i; }
            }
            if (deg == 2) {
                MLine *A = &m->lines.a[la], *B = &m->lines.a[lb];
                if (A->v2 != v) flip_line(A);          /* A: x -> v */
                if (B->v1 != v) flip_line(B);          /* B: v -> y */
                if (A->v1 == B->v2) { kill[la] = kill[lb] = 1; continue; }
                A->v2 = B->v2;
                kill[lb] = 1;
            } else {
                for (int i = 0; i < m->lines.n; i++)
                    if (m->lines.a[i].v1 == v || m->lines.a[i].v2 == v) kill[i] = 1;
            }
        }
    }
    remove_vertex_lines_and_retrace(m, kill);
    free(kill);
}

void map_collect_sel_verts(const Map *m, int sm, uint8_t *mask) {
    memset(mask, 0, (size_t)m->verts.n);
    if (sm == SM_VERTS) for (int i = 0; i < m->verts.n; i++) mask[i] = m->verts.a[i].sel;
    if (sm == SM_LINES)
        for (int i = 0; i < m->lines.n; i++)
            if (m->lines.a[i].sel) mask[m->lines.a[i].v1] = mask[m->lines.a[i].v2] = 1;
    if (sm == SM_SECTORS)
        for (int i = 0; i < m->lines.n; i++) {
            const MLine *l = &m->lines.a[i];
            bool s = (l->side[0].sector >= 0 && m->sectors.a[l->side[0].sector].sel) ||
                     (l->side[1].sector >= 0 && m->sectors.a[l->side[1].sector].sel);
            if (s) mask[l->v1] = mask[l->v2] = 1;
        }
}

void map_finalize_move(Map *m, const uint8_t *moved) {
    int nv = m->verts.n;
    /* merge moved vertices onto others */
    for (int v = 0; v < nv; v++) {
        if (!moved[v]) continue;
        for (int u = 0; u < m->verts.n; u++) {
            if (u == v) continue;
            if (fabs(m->verts.a[u].x - m->verts.a[v].x) + fabs(m->verts.a[u].y - m->verts.a[v].y) < MAP_EPS) {
                for (int i = 0; i < m->lines.n; i++) {
                    if (m->lines.a[i].v1 == v) m->lines.a[i].v1 = u;
                    if (m->lines.a[i].v2 == v) m->lines.a[i].v2 = u;
                }
                break;
            }
        }
    }
    /* moved vertex on a line -> split; any vertex on a moved line -> split */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < m->lines.n; i++) {
            MLine *l = &m->lines.a[i];
            bool lmoved = (l->v1 < nv && moved[l->v1]) || (l->v2 < nv && moved[l->v2]);
            for (int v = 0; v < m->verts.n; v++) {
                if (v == l->v1 || v == l->v2) continue;
                bool vmoved = v < nv && moved[v];
                if (pass == 0 ? !vmoved : !lmoved) continue;
                const MVert *a = &m->verts.a[l->v1], *b = &m->verts.a[l->v2];
                double t;
                if (seg_dist(m->verts.a[v].x, m->verts.a[v].y, a->x, a->y, b->x, b->y, &t) < MAP_EPS && t > 0 && t < 1) {
                    bool used = false;
                    for (int k = 0; k < m->lines.n && !used; k++) used = m->lines.a[k].v1 == v || m->lines.a[k].v2 == v;
                    if (used) { map_split_line(m, i, v); l = &m->lines.a[i]; }
                }
            }
        }
    }
    map_cleanup(m);
}

void map_join_sectors(Map *m, bool merge) {
    int target = -1;
    for (int i = 0; i < m->sectors.n; i++) if (m->sectors.a[i].sel) { target = i; break; }
    if (target < 0) return;
    for (int i = 0; i < m->lines.n; i++)
        for (int k = 0; k < 2; k++) {
            int s = m->lines.a[i].side[k].sector;
            if (s >= 0 && m->sectors.a[s].sel) m->lines.a[i].side[k].sector = target;
        }
    if (merge) {
        int n = 0;
        for (int i = 0; i < m->lines.n; i++) {
            MLine *l = &m->lines.a[i];
            if (l->side[0].sector == target && l->side[1].sector == target) continue;
            m->lines.a[n++] = *l;
        }
        m->lines.n = n;
    }
    map_cleanup(m);
    for (int i = 0; i < m->sectors.n; i++) m->sectors.a[i].sel = 0;
    /* target index may have shifted; reselect the sector at a line that references it */
}

void map_flip_lines(Map *m) {
    for (int i = 0; i < m->lines.n; i++) {
        MLine *l = &m->lines.a[i];
        if (!l->sel) continue;
        if (l->side[1].sector >= 0) flip_line(l);
        else { int t = l->v1; l->v1 = l->v2; l->v2 = t; } /* one-sided: reverse direction only */
    }
    map_cleanup(m);
}

void map_mirror_fix(Map *m, const uint8_t *vmask) {
    for (int i = 0; i < m->lines.n; i++) {
        MLine *l = &m->lines.a[i];
        if (vmask[l->v1] && vmask[l->v2]) { int t = l->v1; l->v1 = l->v2; l->v2 = t; }
    }
    m->geo_rev++;
}

/* Sector boundary loops: half-edges whose left side is s, excluding lines with s on both sides. */
void map_sector_loops(const Map *m, int s, IntArr *verts, IntArr *loop_start) {
    arr_clear(*verts);
    arr_clear(*loop_start);
    int nh = m->lines.n * 2;
    if (!nh) { arr_push(*loop_start, 0); return; }
    uint8_t *inc = calloc((size_t)nh, 1);
    int cnt = 0;
    for (int h = 0; h < nh; h++) {
        const MLine *l = &m->lines.a[h >> 1];
        if (l->side[0].sector == s && l->side[1].sector == s) continue;
        if (l->side[he_side(h)].sector == s) { inc[h] = 1; cnt++; }
    }
    if (!cnt) { free(inc); arr_push(*loop_start, 0); return; }
    HeIndex hi;
    he_build(m, &hi, inc);
    uint8_t *vis = calloc((size_t)nh, 1);
    for (int h = 0; h < nh; h++) {
        if (!inc[h] || vis[h]) continue;
        arr_push(*loop_start, verts->n);
        int e = h, guard = 0;
        while (!vis[e] && guard++ <= nh) {
            vis[e] = 1;
            arr_push(*verts, he_org(m, e));
            /* next among included half-edges */
            int b = he_dst(m, e);
            int st = hi.start[b], c = hi.start[b + 1] - st;
            if (!c) break;
            /* angle of the reverse edge (b -> org) */
            const MVert *pb = &m->verts.a[b], *pa = &m->verts.a[he_org(m, e)];
            double ra = atan2(pa->y - pb->y, pa->x - pb->x);
            int pick = -1;
            double bestd = 1e9;
            for (int k = 0; k < c; k++) {
                int g = hi.list[st + k];
                double d = ra - hi.ang[g];
                while (d <= 1e-12) d += 2 * SF_PI;
                if (d < bestd) { bestd = d; pick = g; }
            }
            e = pick;
            if (e < 0) break;
        }
    }
    arr_push(*loop_start, verts->n);
    free(vis);
    free(inc);
    he_free(&hi);
}

void map_sector_label_point(const Map *m, int s, double *x, double *y) {
    IntArr vs = { 0 }, ls = { 0 };
    map_sector_loops(m, s, &vs, &ls);
    *x = *y = 0;
    double y0 = 1e30, y1 = -1e30;
    for (int i = 0; i < vs.n; i++) { double yy = m->verts.a[vs.a[i]].y; y0 = SF_MIN(y0, yy); y1 = SF_MAX(y1, yy); }
    double bestw = -1;
    ARR(double) xs = { 0 };
    for (int f = 1; f < 8; f++) {
        double yy = y0 + (y1 - y0) * f / 8.0 + 1.3e-4;
        arr_clear(xs);
        for (int L = 0; L + 1 < ls.n; L++) {
            int a0 = ls.a[L], a1 = ls.a[L + 1];
            for (int i = a0; i < a1; i++) {
                const MVert *a = &m->verts.a[vs.a[i]], *b = &m->verts.a[vs.a[i + 1 < a1 ? i + 1 : a0]];
                if ((a->y > yy) != (b->y > yy)) { double xi = a->x + (yy - a->y) * (b->x - a->x) / (b->y - a->y); arr_push(xs, xi); }
            }
        }
        if (xs.n >= 2) {
            qsort(xs.a, (size_t)xs.n, sizeof(double), cmp_double);
            for (int i = 0; i + 1 < xs.n; i += 2) {
                double w = xs.a[i + 1] - xs.a[i];
                double score = w - fabs((double)f - 4.0) * (y1 - y0) * 0.05;
                if (score > bestw) { bestw = score; *x = (xs.a[i] + xs.a[i + 1]) * 0.5; *y = yy; }
            }
        }
    }
    arr_free(xs);
    arr_free(vs);
    arr_free(ls);
}

/* ------------------------------------------------------------------ clipboard */
void map_extract_selection(const Map *src, int sm, Map *clip) {
    map_free(clip);
    map_init(clip);
    if (sm == SM_THINGS || sm == SM_SECTORS) {
        for (int i = 0; i < src->things.n; i++) {
            const MThing *t = &src->things.a[i];
            bool take = t->sel;
            if (sm == SM_SECTORS) {
                int s = map_sector_at(src, t->x, t->y);
                take = s >= 0 && src->sectors.a[s].sel;
            }
            if (take) arr_push(clip->things, *t);
        }
        if (sm == SM_THINGS) return;
    }
    int *vr = malloc(sizeof(int) * (size_t)(src->verts.n ? src->verts.n : 1));
    int *sr = malloc(sizeof(int) * (size_t)(src->sectors.n ? src->sectors.n : 1));
    for (int i = 0; i < src->verts.n; i++) vr[i] = -1;
    for (int i = 0; i < src->sectors.n; i++) {
        sr[i] = -1;
        if (sm == SM_SECTORS && src->sectors.a[i].sel) { sr[i] = clip->sectors.n; arr_push(clip->sectors, src->sectors.a[i]); }
    }
    for (int i = 0; i < src->lines.n; i++) {
        const MLine *l = &src->lines.a[i];
        bool take = false;
        if (sm == SM_LINES) take = l->sel;
        else if (sm == SM_VERTS) take = src->verts.a[l->v1].sel && src->verts.a[l->v2].sel;
        else take = (l->side[0].sector >= 0 && sr[l->side[0].sector] >= 0) || (l->side[1].sector >= 0 && sr[l->side[1].sector] >= 0);
        if (!take) continue;
        MLine nl = *l;
        int ends[2] = { l->v1, l->v2 };
        for (int k = 0; k < 2; k++) {
            if (vr[ends[k]] < 0) { vr[ends[k]] = clip->verts.n; arr_push(clip->verts, src->verts.a[ends[k]]); }
        }
        nl.v1 = vr[l->v1]; nl.v2 = vr[l->v2];
        for (int k = 0; k < 2; k++) nl.side[k].sector = nl.side[k].sector >= 0 ? sr[nl.side[k].sector] : -1;
        if (nl.side[0].sector < 0 && nl.side[1].sector >= 0) flip_line(&nl);
        arr_push(clip->lines, nl);
    }
    free(vr); free(sr);
}

void map_paste(Map *m, const Map *clip, double dx, double dy, int sm) {
    map_select_none(m);
    int v0 = m->verts.n, s0 = m->sectors.n;
    for (int i = 0; i < clip->verts.n; i++) {
        MVert v = clip->verts.a[i];
        v.x += dx; v.y += dy; v.sel = sm == SM_VERTS; v.mark = 0;
        arr_push(m->verts, v);
    }
    for (int i = 0; i < clip->sectors.n; i++) { MSector s = clip->sectors.a[i]; s.sel = sm == SM_SECTORS; arr_push(m->sectors, s); }
    for (int i = 0; i < clip->lines.n; i++) {
        MLine l = clip->lines.a[i];
        l.v1 += v0; l.v2 += v0;
        for (int k = 0; k < 2; k++) if (l.side[k].sector >= 0) l.side[k].sector += s0;
        l.sel = sm == SM_LINES;
        arr_push(m->lines, l);
    }
    for (int i = 0; i < clip->things.n; i++) {
        MThing t = clip->things.a[i];
        t.x += dx; t.y += dy; t.sel = sm == SM_THINGS;
        arr_push(m->things, t);
    }
    uint8_t *moved = calloc((size_t)(m->verts.n ? m->verts.n : 1), 1);
    for (int i = v0; i < m->verts.n; i++) moved[i] = 1;
    map_finalize_move(m, moved);
    free(moved);
}

/* ------------------------------------------------------------------ templates */
const char *g_template_names[TPL_COUNT] = { "Empty Map", "Starter Room", "Dungeon", "Arena" };
const char *g_template_desc[TPL_COUNT] = { "Blank grid, one player start", "Lit room with textures", "Rooms, stairs and a door", "Octagon pit with pillars" };

static void rect_pts(D2 *p, double x0, double y0, double x1, double y1) {
    p[0] = d2(x0, y0); p[1] = d2(x0, y1); p[2] = d2(x1, y1); p[3] = d2(x1, y0);
}

static int last_sector_at(Map *m, double x, double y) { return map_sector_at(m, x, y); }

static void add_thing(Map *m, double x, double y, float ang, int type) {
    MThing t = { x, y, ang, type, 0, 0, 0 };
    arr_push(m->things, t);
}

static void set_sector(Map *m, double x, double y, float fl, float cl, const char *ft, const char *ct, int light, int type) {
    int s = last_sector_at(m, x, y);
    if (s < 0) return;
    MSector *S = &m->sectors.a[s];
    S->floor_h = fl; S->ceil_h = cl; S->light = light; S->type = type;
    if (ft) sf_strcpy(S->floor_tex, TEXNAME, ft);
    if (ct) sf_strcpy(S->ceil_tex, TEXNAME, ct);
}

static void set_walls(Map *m, int sector, const char *tex) {
    for (int i = 0; i < m->lines.n; i++) {
        if (m->lines.a[i].side[0].sector != sector && m->lines.a[i].side[1].sector != sector) continue;
        for (int k = 0; k < 2; k++) {
            {
                MSide *sd = &m->lines.a[i].side[k];
                sf_strcpy(sd->upper, TEXNAME, tex); sf_strcpy(sd->middle, TEXNAME, tex); sf_strcpy(sd->lower, TEXNAME, tex);
            }
        }
    }
}

void map_make_template(Map *m, int tpl, const MapDefaults *def) {
    D2 p[16];
    if (tpl == TPL_EMPTY) { add_thing(m, 0, 0, 90, TH_PLAYER); return; }
    if (tpl == TPL_STARTER) {
        rect_pts(p, -256, -256, 256, 256);
        map_draw_shape(m, p, 4, true, def);
        set_sector(m, 0, 0, 0, 160, "FLOOR_TILE", "CEIL_LIGHT", 208, ST_NORMAL);
        rect_pts(p, -32, 96, 32, 160);
        map_draw_shape(m, p, 4, true, def);
        set_sector(m, 0, 128, 0, 160, "FLOOR_METAL", "CEIL_LIGHT", 208, ST_NORMAL);
        { int s = map_sector_at(m, 0, 128); if (s >= 0) { m->sectors.a[s].floor_h = 160; set_walls(m, s, "SUPPORT_BEAM"); } }
        add_thing(m, 0, -160, 90, TH_PLAYER);
        add_thing(m, -224, 224, 0, TH_LIGHT); add_thing(m, 224, 224, 0, TH_LIGHT);
        add_thing(m, -224, -224, 0, TH_LIGHT); add_thing(m, 224, -224, 0, TH_LIGHT);
        add_thing(m, 160, 0, 0, TH_HEALTH);
        return;
    }
    if (tpl == TPL_DUNGEON) {
        rect_pts(p, -384, -256, 0, 128);  map_draw_shape(m, p, 4, true, def);
        set_sector(m, -192, -64, 0, 192, "FLOOR_STONE", "CEIL_STONE", 176, ST_NORMAL);
        rect_pts(p, 0, -48, 32, 48);      map_draw_shape(m, p, 4, true, def);
        set_sector(m, 16, 0, 0, 128, "FLOOR_METAL", "CEIL_TECH", 176, ST_DOOR);
        { int s = map_sector_at(m, 16, 0); if (s >= 0) set_walls(m, s, "DOOR_METAL"); }
        rect_pts(p, 32, -48, 288, 48);    map_draw_shape(m, p, 4, true, def);
        set_sector(m, 160, 0, 0, 128, "FLOOR_TILE", "CEIL_TECH", 144, ST_NORMAL);
        for (int i = 0; i < 4; i++) {
            rect_pts(p, 288 + i * 32, -48, 320 + i * 32, 48); map_draw_shape(m, p, 4, true, def);
            set_sector(m, 304 + i * 32, 0, (float)(-16 * (i + 1)), 128, "FLOOR_STONE", "CEIL_TECH", 144, ST_NORMAL);
        }
        rect_pts(p, 416, -224, 704, 224); map_draw_shape(m, p, 4, true, def);
        set_sector(m, 560, 0, -64, 160, "FLOOR_DIRT", "CEIL_STONE", 128, ST_NORMAL);
        rect_pts(p, 528, -48, 592, 48);   map_draw_shape(m, p, 4, true, def);
        set_sector(m, 560, 0, -72, 160, "LAVA", "CEIL_STONE", 255, ST_DAMAGE);
        add_thing(m, -320, -192, 45, TH_PLAYER);
        add_thing(m, -192, 96, 0, TH_LIGHT); add_thing(m, 640, 192, 0, TH_LIGHT);
        add_thing(m, 640, -160, 180, TH_IMP); add_thing(m, 480, 160, 200, TH_SOLDIER);
        add_thing(m, -64, -224, 0, TH_AMMO); add_thing(m, 160, 32, 0, TH_ARMOR); add_thing(m, 672, 0, 0, TH_BARREL);
        return;
    }
    if (tpl == TPL_ARENA) {
        for (int k = 0; k < 8; k++) { double a = (22.5 + 45 * k) * SF_DEG2RAD; p[k] = d2(floor(cos(a) * 640 + 0.5), floor(sin(a) * 640 + 0.5)); }
        map_draw_shape(m, p, 8, true, def);
        set_sector(m, 0, 0, 0, 320, "FLOOR_STONE", "F_SKY", 200, ST_NORMAL);
        for (int k = 0; k < 8; k++) { double a = (22.5 + 45 * k) * SF_DEG2RAD; p[k] = d2(floor(cos(a) * 320 + 0.5), floor(sin(a) * 320 + 0.5)); }
        map_draw_shape(m, p, 8, true, def);
        set_sector(m, 0, 0, -96, 320, "FLOOR_METAL", "F_SKY", 200, ST_NORMAL);
        { int s = map_sector_at(m, 0, 0); if (s >= 0) set_walls(m, s, "METAL_PANEL"); }
        for (int k = 0; k < 4; k++) {
            double a = (45 + 90 * k) * SF_DEG2RAD, cx = floor(cos(a) * 480 + 0.5), cy = floor(sin(a) * 480 + 0.5);
            rect_pts(p, cx - 32, cy - 32, cx + 32, cy + 32);
            map_draw_shape(m, p, 4, true, def);
            int s = map_sector_at(m, cx, cy);
            if (s >= 0) { m->sectors.a[s].floor_h = 320; set_walls(m, s, "STONE_BLOCKS"); }
            add_thing(m, cx + 64, cy, 0, TH_LIGHT);
        }
        add_thing(m, 0, -560, 90, TH_PLAYER);
        add_thing(m, 0, 0, 270, TH_IMP); add_thing(m, 96, 96, 270, TH_IMP);
        add_thing(m, 0, 520, 0, TH_HEALTH);
        return;
    }
}
