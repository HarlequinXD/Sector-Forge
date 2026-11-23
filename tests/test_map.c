/* native unit tests for the map core (host build, not part of the app) */
#include "../src/map.h"
#include "../src/plat.h"
#include <assert.h>
FILE *plat_fopen(const char *p, const char *m) { return fopen(p, m); }
bool plat_delete_file(const char *p) { remove(p); return true; }
bool plat_copy_file(const char *a, const char *b) { FILE *f = fopen(a, "rb"), *g = fopen(b, "wb"); int c; if (!f || !g) return false; while ((c = fgetc(f)) != EOF) fputc(c, g); fclose(f); fclose(g); return true; }

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void rectp(D2 *p, double a, double b, double c, double d) { p[0] = d2(a, b); p[1] = d2(a, d); p[2] = d2(c, d); p[3] = d2(c, b); }
static int nsec(Map *m) { return m->sectors.n; }
static void dump(const Map *m, const char *path) {
    FILE *f = fopen(path, "w");
    IntArr vs = {0}, ls = {0};
    for (int s = 0; s < m->sectors.n; s++) {
        map_sector_loops(m, s, &vs, &ls);
        fprintf(f, "S %d %g %g", s, m->sectors.a[s].floor_h, m->sectors.a[s].ceil_h);
        for (int L = 0; L + 1 < ls.n; L++) { fprintf(f, " |"); for (int i = ls.a[L]; i < ls.a[L+1]; i++) fprintf(f, " %g,%g", m->verts.a[vs.a[i]].x, m->verts.a[vs.a[i]].y); }
        fprintf(f, "\n");
    }
    for (int i = 0; i < m->lines.n; i++) { const MLine *l = &m->lines.a[i]; fprintf(f, "L %g %g %g %g %d %d\n", m->verts.a[l->v1].x, m->verts.a[l->v1].y, m->verts.a[l->v2].x, m->verts.a[l->v2].y, l->side[0].sector, l->side[1].sector); }
    for (int i = 0; i < m->things.n; i++) fprintf(f, "T %g %g %d\n", m->things.a[i].x, m->things.a[i].y, m->things.a[i].type);
    fclose(f); arr_free(vs); arr_free(ls);
}
int main(void) {
    MapDefaults def; map_defaults_init(&def);
    Map m; map_init(&m); D2 p[8];
    rectp(p, 0, 0, 256, 256); map_draw_shape(&m, p, 4, true, &def);
    CHECK(nsec(&m) == 1); CHECK(m.lines.n == 4);
    CHECK(map_sector_at(&m, 128, 128) == 0); CHECK(map_sector_at(&m, -10, 128) == -1); CHECK(map_sector_at(&m, 300, 128) == -1);
    for (int i = 0; i < m.lines.n; i++) { CHECK(m.lines.a[i].side[0].sector == 0); CHECK(m.lines.a[i].side[1].sector == -1); }
    /* adjacent room sharing the x=256 wall */
    rectp(p, 256, 64, 512, 192); map_draw_shape(&m, p, 4, true, &def);
    CHECK(nsec(&m) == 2); CHECK(map_sector_at(&m, 400, 128) == 1); CHECK(map_sector_at(&m, 128, 128) == 0);
    { int two = 0; for (int i = 0; i < m.lines.n; i++) if (m.lines.a[i].side[1].sector >= 0) two++; CHECK(two == 1); }
    /* pillar inside room 0 */
    m.sectors.a[0].floor_h = 8;
    rectp(p, 96, 96, 160, 160); map_draw_shape(&m, p, 4, true, &def);
    CHECK(nsec(&m) == 3); { int s = map_sector_at(&m, 128, 128); CHECK(s == 2); CHECK(m.sectors.a[s].floor_h == 8); }
    CHECK(map_sector_at(&m, 20, 20) == 0);
    /* open line splitting room 1 */
    D2 q[2] = { d2(384, 64), d2(384, 192) }; map_draw_shape(&m, q, 2, false, &def);
    CHECK(nsec(&m) == 4); CHECK(map_sector_at(&m, 300, 128) != map_sector_at(&m, 450, 128));
    CHECK(map_sector_at(&m, 300, 128) >= 0 && map_sector_at(&m, 450, 128) >= 0);
    dump(&m, "/tmp/t1.txt");
    /* delete that line again -> merge */
    for (int i = 0; i < m.lines.n; i++) { MLine *l = &m.lines.a[i]; l->sel = m.verts.a[l->v1].x == 384 && m.verts.a[l->v2].x == 384; }
    map_delete_selection(&m, SM_LINES);
    CHECK(nsec(&m) == 3); CHECK(map_sector_at(&m, 300, 128) == map_sector_at(&m, 450, 128));
    /* crossing shape: overlaps room 0 and void */
    rectp(p, -64, 200, 64, 320); map_draw_shape(&m, p, 4, true, &def);
    CHECK(map_sector_at(&m, 0, 228) != map_sector_at(&m, 128, 228)); CHECK(map_sector_at(&m, 0, 228) >= 0);
    CHECK(map_sector_at(&m, 0, 300) == map_sector_at(&m, 0, 228) || map_sector_at(&m, 0, 300) >= 0);
    CHECK(map_sector_at(&m, -32, 100) == -1);
    dump(&m, "/tmp/t2.txt");
    /* delete sector 0 region */
    map_select_none(&m); { int s = map_sector_at(&m, 20, 20); m.sectors.a[s].sel = 1; }
    map_delete_selection(&m, SM_SECTORS);
    CHECK(map_sector_at(&m, 20, 20) == -1); CHECK(map_sector_at(&m, 128, 128) >= 0); CHECK(map_sector_at(&m, 400, 128) >= 0);
    /* roundtrip */
    Buf b = {0}; map_write(&m, &b); Map r; map_init(&r); CHECK(map_read(&r, b.s));
    CHECK(r.lines.n == m.lines.n && r.sectors.n == m.sectors.n && r.verts.n == m.verts.n);
    Buf b2 = {0}; map_write(&r, &b2); CHECK(strcmp(b.s, b2.s) == 0);
    /* vertex delete degree 2 on a square corner -> triangle */
    Map t; map_init(&t); rectp(p, 0, 0, 128, 128); map_draw_shape(&t, p, 4, true, &def);
    int v = map_find_vertex(&t, 128, 128, 0.01); t.verts.a[v].sel = 1; map_delete_selection(&t, SM_VERTS);
    CHECK(t.lines.n == 3); CHECK(t.sectors.n == 1); CHECK(map_sector_at(&t, 30, 30) == 0); CHECK(map_sector_at(&t, 110, 110) == -1);
    /* move-stitch: two separate rooms, then drag one against the other */
    Map u; map_init(&u); rectp(p, 0, 0, 128, 128); map_draw_shape(&u, p, 4, true, &def);
    rectp(p, 200, 0, 328, 128); map_draw_shape(&u, p, 4, true, &def);
    uint8_t mv[16] = {0}; for (int i = 0; i < u.verts.n; i++) if (u.verts.a[i].x >= 200) { mv[i] = 1; u.verts.a[i].x -= 72; }
    map_finalize_move(&u, mv); CHECK(u.lines.n == 7); CHECK(u.verts.n == 6);
    { int two = 0; for (int i = 0; i < u.lines.n; i++) if (u.lines.a[i].side[1].sector >= 0) two++; CHECK(two == 1); }
    /* templates */
    for (int k = 0; k < TPL_COUNT; k++) { Map tm; map_init(&tm); map_make_template(&tm, k, &def); char fn[64]; snprintf(fn, 64, "/tmp/tpl%d.txt", k); dump(&tm, fn); printf("template %d: %d sectors %d lines %d things\n", k, tm.sectors.n, tm.lines.n, tm.things.n); map_free(&tm); }
    /* copy paste */
    Map clip; map_init(&clip); map_select_none(&u); u.sectors.a[0].sel = 1; map_extract_selection(&u, SM_SECTORS, &clip);
    map_paste(&u, &clip, 0, 400, SM_SECTORS); CHECK(map_sector_at(&u, 64, 464) >= 0);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails != 0;
}
