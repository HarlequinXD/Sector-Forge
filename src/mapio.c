/* Sector Forge - .sfmap text format */
#include "map.h"
#include "plat.h"

static const char *tx(const char *s) { return s[0] ? s : "-"; }

static void write_str(Buf *b, const char *s) {
    buf_append(b, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') buf_append(b, "\\", 1);
        if (*s == '\n') { buf_append(b, " ", 1); continue; }
        buf_append(b, s, 1);
    }
    buf_append(b, "\"", 1);
}

void map_write(const Map *m, Buf *b) {
    buf_printf(b, "SFMAP 1\nname ");
    write_str(b, m->name);
    buf_printf(b, "\n");
    if (m->has_cam) buf_printf(b, "camera %.3f %.3f %.3f %.4f %.4f\n", m->cam_x, m->cam_y, m->cam_z, m->cam_yaw, m->cam_pitch);
    for (int i = 0; i < m->verts.n; i++) buf_printf(b, "v %.10g %.10g\n", m->verts.a[i].x, m->verts.a[i].y);
    for (int i = 0; i < m->sectors.n; i++) {
        const MSector *s = &m->sectors.a[i];
        buf_printf(b, "s %g %g %s %s %d %d %d %g %g %g %g %g %g %g %g ", s->floor_h, s->ceil_h, tx(s->floor_tex), tx(s->ceil_tex),
                   s->light, s->type, s->tag, s->fox, s->foy, s->fscale, s->frot, s->cox, s->coy, s->cscale, s->crot);
        write_str(b, s->name);
        buf_printf(b, "\n");
    }
    for (int i = 0; i < m->lines.n; i++) {
        const MLine *l = &m->lines.a[i];
        buf_printf(b, "l %d %d %d", l->v1, l->v2, l->flags);
        for (int k = 0; k < 2; k++) {
            const MSide *sd = &l->side[k];
            buf_printf(b, " %d %s %s %s %g %g", sd->sector, tx(sd->upper), tx(sd->middle), tx(sd->lower), sd->offx, sd->offy);
        }
        buf_printf(b, "\n");
    }
    for (int i = 0; i < m->things.n; i++) {
        const MThing *t = &m->things.a[i];
        buf_printf(b, "t %.10g %.10g %g %d %d\n", t->x, t->y, t->angle, t->type, t->flags);
    }
}

/* tokenizer over one line */
typedef struct { const char *p; } Tok;
static bool tok_next(Tok *t, char *out, size_t cap) {
    while (*t->p == ' ' || *t->p == '\t') t->p++;
    if (!*t->p || *t->p == '\n' || *t->p == '\r') return false;
    size_t n = 0;
    if (*t->p == '"') {
        t->p++;
        while (*t->p && *t->p != '"' && *t->p != '\n') {
            if (*t->p == '\\' && t->p[1]) t->p++;
            if (n + 1 < cap) out[n++] = *t->p;
            t->p++;
        }
        if (*t->p == '"') t->p++;
    } else {
        while (*t->p && *t->p != ' ' && *t->p != '\t' && *t->p != '\n' && *t->p != '\r') {
            if (n + 1 < cap) out[n++] = *t->p;
            t->p++;
        }
    }
    out[n] = 0;
    return true;
}
static double tok_num(Tok *t) { char b[64]; return tok_next(t, b, sizeof b) ? atof(b) : 0; }
static void tok_tex(Tok *t, char *dst) {
    char b[64];
    if (!tok_next(t, b, sizeof b) || !strcmp(b, "-")) b[0] = 0;
    sf_strcpy(dst, TEXNAME, b);
}

bool map_read(Map *m, const char *text) {
    map_free(m);
    map_init(m);
    const char *p = text;
    bool ok = strncmp(p, "SFMAP", 5) == 0;
    if (!ok) return false;
    char key[64];
    while (*p) {
        Tok t = { p };
        if (tok_next(&t, key, sizeof key)) {
            if (!strcmp(key, "name")) tok_next(&t, m->name, sizeof m->name);
            else if (!strcmp(key, "camera")) {
                m->has_cam = true;
                m->cam_x = (float)tok_num(&t); m->cam_y = (float)tok_num(&t); m->cam_z = (float)tok_num(&t);
                m->cam_yaw = (float)tok_num(&t); m->cam_pitch = (float)tok_num(&t);
            } else if (!strcmp(key, "v")) {
                MVert v = { 0 };
                v.x = tok_num(&t); v.y = tok_num(&t);
                arr_push(m->verts, v);
            } else if (!strcmp(key, "s")) {
                MSector s;
                memset(&s, 0, sizeof s);
                s.floor_h = (float)tok_num(&t); s.ceil_h = (float)tok_num(&t);
                tok_tex(&t, s.floor_tex); tok_tex(&t, s.ceil_tex);
                s.light = (int)tok_num(&t); s.type = (int)tok_num(&t); s.tag = (int)tok_num(&t);
                s.fox = (float)tok_num(&t); s.foy = (float)tok_num(&t); s.fscale = (float)tok_num(&t); s.frot = (float)tok_num(&t);
                s.cox = (float)tok_num(&t); s.coy = (float)tok_num(&t); s.cscale = (float)tok_num(&t); s.crot = (float)tok_num(&t);
                tok_next(&t, s.name, sizeof s.name);
                if (s.fscale <= 0) s.fscale = 1;
                if (s.cscale <= 0) s.cscale = 1;
                if (s.type < 0 || s.type >= ST_COUNT) s.type = 0;
                arr_push(m->sectors, s);
            } else if (!strcmp(key, "l")) {
                MLine l;
                memset(&l, 0, sizeof l);
                l.v1 = (int)tok_num(&t); l.v2 = (int)tok_num(&t); l.flags = (int)tok_num(&t);
                for (int k = 0; k < 2; k++) {
                    l.side[k].sector = (int)tok_num(&t);
                    tok_tex(&t, l.side[k].upper); tok_tex(&t, l.side[k].middle); tok_tex(&t, l.side[k].lower);
                    l.side[k].offx = (float)tok_num(&t); l.side[k].offy = (float)tok_num(&t);
                }
                arr_push(m->lines, l);
            } else if (!strcmp(key, "t")) {
                MThing th;
                memset(&th, 0, sizeof th);
                th.x = tok_num(&t); th.y = tok_num(&t); th.angle = (float)tok_num(&t);
                th.type = (int)tok_num(&t); th.flags = (int)tok_num(&t);
                if (th.type < 0 || th.type >= TH_COUNT) th.type = TH_BARREL;
                arr_push(m->things, th);
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    /* validate references */
    for (int i = 0; i < m->lines.n; i++) {
        MLine *l = &m->lines.a[i];
        if (l->v1 < 0 || l->v1 >= m->verts.n || l->v2 < 0 || l->v2 >= m->verts.n) { arr_remove(m->lines, i); i--; continue; }
        for (int k = 0; k < 2; k++) if (l->side[k].sector >= m->sectors.n) l->side[k].sector = -1;
    }
    m->geo_rev++;
    return true;
}

bool map_save(const Map *m, const char *path) {
    Buf b = { 0 };
    map_write(m, &b);
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = plat_fopen(tmp, "wb");
    if (!f) { buf_free(&b); return false; }
    bool ok = fwrite(b.s, 1, (size_t)b.n, f) == (size_t)b.n;
    ok = fclose(f) == 0 && ok;
    buf_free(&b);
    if (!ok) return false;
    plat_delete_file(path);
    return plat_copy_file(tmp, path) && plat_delete_file(tmp);
}

bool map_load(Map *m, const char *path) {
    char *txt = sf_read_file(path, NULL);
    if (!txt) return false;
    bool ok = map_read(m, txt);
    free(txt);
    return ok;
}
