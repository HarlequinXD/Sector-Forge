/* Sector Forge - polygon triangulation.
   Loops are grouped by nesting depth (even = outline, odd = hole, matching the
   odd winding rule), then each outline with its holes is triangulated by ear
   clipping with hole bridging (a C port of the earcut algorithm). */
#include "tess.h"

typedef struct Node {
    int i;
    double x, y;
    struct Node *prev, *next;
    bool steiner;
} Node;

/* stable-pointer node pool */
typedef struct { Node *blocks[4096]; int nblocks, used; } Pool;
#define POOL_BLOCK 512

static Node *node_new(Pool *pl, int i, double x, double y) {
    if (pl->nblocks == 0 || pl->used == POOL_BLOCK) {
        if (pl->nblocks == 4096) return NULL;
        pl->blocks[pl->nblocks++] = malloc(sizeof(Node) * POOL_BLOCK);
        pl->used = 0;
    }
    Node *n = &pl->blocks[pl->nblocks - 1][pl->used++];
    n->i = i; n->x = x; n->y = y; n->prev = n->next = NULL; n->steiner = false;
    return n;
}
static void pool_free(Pool *pl) { for (int i = 0; i < pl->nblocks; i++) free(pl->blocks[i]); pl->nblocks = 0; }

typedef struct { Pool pool; FloatArr *out; } Ctx;

static Node *insert_node(Ctx *c, int i, double x, double y, Node *last) {
    Node *p = node_new(&c->pool, i, x, y);
    if (!p) return last;
    if (!last) { p->prev = p; p->next = p; }
    else { p->next = last->next; p->prev = last; last->next->prev = p; last->next = p; }
    return p;
}
static void remove_node(Node *p) { p->next->prev = p->prev; p->prev->next = p->next; }
static bool equals(const Node *a, const Node *b) { return a->x == b->x && a->y == b->y; }
static double area(const Node *p, const Node *q, const Node *r) { return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y); }
static int sgn(double v) { return (v > 0) - (v < 0); }

static void emit(Ctx *c, const Node *a, const Node *b, const Node *d) {
    arr_push(*c->out, (float)a->x); arr_push(*c->out, (float)a->y);
    arr_push(*c->out, (float)b->x); arr_push(*c->out, (float)b->y);
    arr_push(*c->out, (float)d->x); arr_push(*c->out, (float)d->y);
}

static double signed_area(const double *xy, int start, int end) {
    double sum = 0;
    for (int i = start, j = end - 1; i < end; j = i++)
        sum += (xy[j * 2] - xy[i * 2]) * (xy[i * 2 + 1] + xy[j * 2 + 1]);
    return sum;
}

static Node *linked_list(Ctx *c, const double *xy, int start, int end, bool clockwise) {
    Node *last = NULL;
    if (clockwise == (signed_area(xy, start, end) > 0)) {
        for (int i = start; i < end; i++) last = insert_node(c, i, xy[i * 2], xy[i * 2 + 1], last);
    } else {
        for (int i = end - 1; i >= start; i--) last = insert_node(c, i, xy[i * 2], xy[i * 2 + 1], last);
    }
    if (last && equals(last, last->next)) { Node *n = last->next; remove_node(last); last = n; }
    return last;
}

static Node *filter_points(Node *start, Node *end) {
    if (!start) return start;
    if (!end) end = start;
    Node *p = start;
    bool again;
    do {
        again = false;
        if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
            remove_node(p);
            p = end = p->prev;
            if (p == p->next) break;
            again = true;
        } else p = p->next;
    } while (again || p != end);
    return end;
}

static bool point_in_triangle(double ax, double ay, double bx, double by, double cx, double cy, double px, double py) {
    return (cx - px) * (ay - py) >= (ax - px) * (cy - py) &&
           (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
           (bx - px) * (cy - py) >= (cx - px) * (by - py);
}

static bool is_ear(Node *ear) {
    Node *a = ear->prev, *b = ear, *c = ear->next;
    if (area(a, b, c) >= 0) return false;
    for (Node *p = c->next; p != a; p = p->next) {
        if (((p->x != a->x || p->y != a->y) && (p->x != c->x || p->y != c->y)) &&
            point_in_triangle(a->x, a->y, b->x, b->y, c->x, c->y, p->x, p->y) && area(p->prev, p, p->next) >= 0)
            return false;
    }
    return true;
}

static bool on_segment(const Node *p, const Node *q, const Node *r) {
    return q->x <= SF_MAX(p->x, r->x) && q->x >= SF_MIN(p->x, r->x) && q->y <= SF_MAX(p->y, r->y) && q->y >= SF_MIN(p->y, r->y);
}
static bool intersects(const Node *p1, const Node *q1, const Node *p2, const Node *q2) {
    int o1 = sgn(area(p1, q1, p2)), o2 = sgn(area(p1, q1, q2)), o3 = sgn(area(p2, q2, p1)), o4 = sgn(area(p2, q2, q1));
    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && on_segment(p1, p2, q1)) return true;
    if (o2 == 0 && on_segment(p1, q2, q1)) return true;
    if (o3 == 0 && on_segment(p2, p1, q2)) return true;
    if (o4 == 0 && on_segment(p2, q1, q2)) return true;
    return false;
}
static bool intersects_polygon(const Node *a, const Node *b) {
    const Node *p = a;
    do {
        if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
        p = p->next;
    } while (p != a);
    return false;
}
static bool locally_inside(const Node *a, const Node *b) {
    return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0
                                         : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
}
static bool middle_inside(const Node *a, const Node *b) {
    const Node *p = a;
    bool inside = false;
    double px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
    do {
        if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y &&
            (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
            inside = !inside;
        p = p->next;
    } while (p != a);
    return inside;
}
static bool is_valid_diagonal(const Node *a, const Node *b) {
    return a->next->i != b->i && a->prev->i != b->i && !intersects_polygon(a, b) &&
           ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b) &&
             (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0)) ||
            (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
}
static Node *split_polygon(Ctx *c, Node *a, Node *b) {
    Node *a2 = node_new(&c->pool, a->i, a->x, a->y), *b2 = node_new(&c->pool, b->i, b->x, b->y);
    if (!a2 || !b2) return NULL;
    Node *an = a->next, *bp = b->prev;
    a->next = b; b->prev = a;
    a2->next = an; an->prev = a2;
    b2->next = a2; a2->prev = b2;
    bp->next = b2; b2->prev = bp;
    return b2;
}

static void earcut_linked(Ctx *c, Node *ear, int pass, int depth);

static Node *cure_local_intersections(Ctx *c, Node *start) {
    Node *p = start;
    do {
        Node *a = p->prev, *b = p->next->next;
        if (!equals(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
            emit(c, a, p, b);
            remove_node(p);
            remove_node(p->next);
            p = start = b;
        }
        p = p->next;
    } while (p != start);
    return filter_points(p, NULL);
}

static void split_earcut(Ctx *c, Node *start, int depth) {
    Node *a = start;
    do {
        Node *b = a->next->next;
        while (b != a->prev) {
            if (a->i != b->i && is_valid_diagonal(a, b)) {
                Node *d = split_polygon(c, a, b);
                if (!d) return;
                a = filter_points(a, a->next);
                d = filter_points(d, d->next);
                earcut_linked(c, a, 0, depth + 1);
                earcut_linked(c, d, 0, depth + 1);
                return;
            }
            b = b->next;
        }
        a = a->next;
    } while (a != start);
}

static void earcut_linked(Ctx *c, Node *ear, int pass, int depth) {
    if (!ear || depth > 64) return;
    Node *stop = ear;
    while (ear->prev != ear->next) {
        Node *prev = ear->prev, *next = ear->next;
        if (is_ear(ear)) {
            emit(c, prev, ear, next);
            remove_node(ear);
            ear = next->next;
            stop = next->next;
            continue;
        }
        ear = next;
        if (ear == stop) {
            if (pass == 0) earcut_linked(c, filter_points(ear, NULL), 1, depth);
            else if (pass == 1) earcut_linked(c, cure_local_intersections(c, filter_points(ear, NULL)), 2, depth);
            else split_earcut(c, ear, depth);
            break;
        }
    }
}

static Node *get_leftmost(Node *start) {
    Node *p = start, *l = start;
    do {
        if (p->x < l->x || (p->x == l->x && p->y < l->y)) l = p;
        p = p->next;
    } while (p != start);
    return l;
}

static bool sector_contains_sector(const Node *m, const Node *p) {
    return area(m->prev, m, p->prev) < 0 && area(p->next, m, m->next) < 0;
}

static Node *find_hole_bridge(Node *hole, Node *outer) {
    Node *p = outer, *m = NULL;
    double hx = hole->x, hy = hole->y, qx = -1e300;
    do {
        if (hy <= p->y && hy >= p->next->y && p->next->y != p->y) {
            double x = p->x + (hy - p->y) * (p->next->x - p->x) / (p->next->y - p->y);
            if (x <= hx && x > qx) {
                qx = x;
                m = p->x < p->next->x ? p : p->next;
                if (x == hx) return m;
            }
        }
        p = p->next;
    } while (p != outer);
    if (!m) return NULL;
    Node *stop = m;
    double mx = m->x, my = m->y, tan_min = 1e300;
    p = m;
    do {
        if (hx >= p->x && p->x >= mx && hx != p->x &&
            point_in_triangle(hy < my ? hx : qx, hy, mx, my, hy < my ? qx : hx, hy, p->x, p->y)) {
            double tn = fabs(hy - p->y) / (hx - p->x);
            if (locally_inside(p, hole) &&
                (tn < tan_min || (tn == tan_min && (p->x > m->x || (p->x == m->x && sector_contains_sector(m, p)))))) {
                m = p;
                tan_min = tn;
            }
        }
        p = p->next;
    } while (p != stop);
    return m;
}

static Node *eliminate_hole(Ctx *c, Node *hole, Node *outer) {
    Node *bridge = find_hole_bridge(hole, outer);
    if (!bridge) return outer;
    Node *rev = split_polygon(c, bridge, hole);
    if (!rev) return outer;
    filter_points(rev, rev->next);
    return filter_points(bridge, bridge->next);
}

static int cmp_x(const void *a, const void *b) {
    const Node *na = *(Node *const *)a, *nb = *(Node *const *)b;
    return na->x < nb->x ? -1 : na->x > nb->x;
}

/* point-in-polygon for a loop (even-odd) */
static bool loop_contains(const double *xy, int s, int e, double px, double py) {
    bool in = false;
    for (int i = s, j = e - 1; i < e; j = i++) {
        double xi = xy[i * 2], yi = xy[i * 2 + 1], xj = xy[j * 2], yj = xy[j * 2 + 1];
        if (((yi > py) != (yj > py)) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

void tess_odd(const double *xy, const int *ls, int nloops, FloatArr *out) {
    if (nloops <= 0) return;
    int *depth = calloc((size_t)nloops, sizeof(int)), *parent = malloc(sizeof(int) * (size_t)nloops);
    double *ar = calloc((size_t)nloops, sizeof(double));
    for (int L = 0; L < nloops; L++) {
        parent[L] = -1;
        ar[L] = fabs(signed_area(xy, ls[L], ls[L + 1])) * 0.5;
    }
    for (int L = 0; L < nloops; L++) {
        int s = ls[L], e = ls[L + 1];
        if (e - s < 3 || ar[L] <= 0) { depth[L] = -1; continue; }
        /* test point: midpoint of the first edge nudged a hair to its interior side is overkill;
           a plain edge midpoint is reliable because separate loops of one sector never share edges */
        double px = (xy[s * 2] + xy[(s + 1) * 2]) * 0.5, py = (xy[s * 2 + 1] + xy[(s + 1) * 2 + 1]) * 0.5;
        double best = 1e300;
        for (int M = 0; M < nloops; M++) {
            if (M == L || ls[M + 1] - ls[M] < 3 || ar[M] <= 0) continue;
            if (loop_contains(xy, ls[M], ls[M + 1], px, py)) {
                depth[L]++;
                if (ar[M] < best) { best = ar[M]; parent[L] = M; }
            }
        }
    }
    Ctx c = { 0 };
    c.out = out;
    Node **holes = malloc(sizeof(Node *) * (size_t)nloops);
    for (int L = 0; L < nloops; L++) {
        if (depth[L] < 0 || (depth[L] & 1)) continue;
        Node *outer = linked_list(&c, xy, ls[L], ls[L + 1], true);
        if (!outer || outer->next == outer->prev) continue;
        int nh = 0;
        for (int H = 0; H < nloops; H++) {
            if (depth[H] < 0 || !(depth[H] & 1) || parent[H] != L) continue;
            Node *h = linked_list(&c, xy, ls[H], ls[H + 1], false);
            if (!h) continue;
            if (h == h->next) h->steiner = true;
            holes[nh++] = get_leftmost(h);
        }
        qsort(holes, (size_t)nh, sizeof(Node *), cmp_x);
        for (int k = 0; k < nh; k++) outer = eliminate_hole(&c, holes[k], outer);
        earcut_linked(&c, outer, 0, 0);
    }
    free(holes);
    pool_free(&c.pool);
    free(depth); free(parent); free(ar);
}
