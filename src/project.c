/* Sector Forge - project management */
#include "project.h"
#include "plat.h"
#include <time.h>

Settings g_settings;

static void write_q(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); fputc(*s, f); }
    fputc('"', f);
}

/* parse "quoted" token starting at *p */
static bool read_q(const char **pp, char *out, size_t cap) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && *p != '\n') {
        if (*p == '\\' && p[1]) p++;
        if (n + 1 < cap) out[n++] = *p;
        p++;
    }
    out[n] = 0;
    if (*p == '"') p++;
    *pp = p;
    return true;
}

void project_map_path(const Project *p, const char *mapfile, char *out, size_t cap) {
    char maps[600];
    sf_path_join(maps, sizeof maps, p->dir, "maps");
    sf_path_join(out, cap, maps, mapfile);
}

void project_tex_dir(const Project *p, char *out, size_t cap) { sf_path_join(out, cap, p->dir, "textures"); }

bool project_save(const Project *p) {
    FILE *f = plat_fopen(p->file, "wb");
    if (!f) return false;
    fprintf(f, "SFPROJ 1\nname ");
    write_q(f, p->name);
    fprintf(f, "\ncreated %lld\n", (long long)p->created);
    for (int i = 0; i < p->nmaps; i++) { fprintf(f, "map "); write_q(f, p->maps[i].file); fputc(' ', f); write_q(f, p->maps[i].title); fputc('\n', f); }
    fprintf(f, "last ");
    write_q(f, p->last_map);
    fprintf(f, "\ndefaults %g %g %s %s %s %d\n", p->def.floor_h, p->def.ceil_h, p->def.floor_tex, p->def.ceil_tex, p->def.wall_tex, p->def.light);
    return fclose(f) == 0;
}

bool project_load(Project *p, const char *file) {
    char *txt = sf_read_file(file, NULL);
    if (!txt) return false;
    if (strncmp(txt, "SFPROJ", 6)) { free(txt); return false; }
    memset(p, 0, sizeof *p);
    map_defaults_init(&p->def);
    sf_strcpy(p->file, sizeof p->file, file);
    sf_strcpy(p->dir, sizeof p->dir, file);
    char *sl = strrchr(p->dir, '\\'), *sl2 = strrchr(p->dir, '/');
    if (sl2 > sl) sl = sl2;
    if (sl) *sl = 0;
    const char *s = txt;
    while (*s) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        if (!strncmp(s, "name ", 5)) { const char *q = s + 5; read_q(&q, p->name, sizeof p->name); }
        else if (!strncmp(s, "created ", 8)) p->created = atoll(s + 8);
        else if (!strncmp(s, "map ", 4) && p->nmaps < MAX_MAPS) {
            const char *q = s + 4;
            ProjMap *m = &p->maps[p->nmaps];
            if (read_q(&q, m->file, sizeof m->file)) {
                if (!read_q(&q, m->title, sizeof m->title)) sf_strcpy(m->title, sizeof m->title, m->file);
                p->nmaps++;
            }
        } else if (!strncmp(s, "last ", 5)) { const char *q = s + 5; read_q(&q, p->last_map, sizeof p->last_map); }
        else if (!strncmp(s, "defaults ", 9)) {
            char ft[64] = "", ct[64] = "", wt[64] = "";
            float fl = 0, cl = 128;
            int li = 192;
            if (sscanf(s + 9, "%f %f %63s %63s %63s %d", &fl, &cl, ft, ct, wt, &li) == 6) {
                p->def.floor_h = fl; p->def.ceil_h = cl; p->def.light = li;
                sf_strcpy(p->def.floor_tex, TEXNAME, ft); sf_strcpy(p->def.ceil_tex, TEXNAME, ct); sf_strcpy(p->def.wall_tex, TEXNAME, wt);
            }
        }
        s = *e ? e + 1 : e;
    }
    free(txt);
    if (!p->name[0]) sf_strcpy(p->name, sizeof p->name, "Untitled Project");
    return true;
}

int project_find_map(const Project *p, const char *file) {
    for (int i = 0; i < p->nmaps; i++) if (!sf_stricmp(p->maps[i].file, file)) return i;
    return -1;
}

int project_add_map(Project *p, const char *title, int tpl) {
    if (p->nmaps >= MAX_MAPS) return -1;
    char file[64];
    for (int k = 1; k < 1000; k++) {
        snprintf(file, sizeof file, "map%02d.sfmap", k);
        if (project_find_map(p, file) < 0) {
            char path[1024];
            project_map_path(p, file, path, sizeof path);
            if (!plat_exists(path)) break;
        }
    }
    Map m;
    map_init(&m);
    sf_strcpy(m.name, sizeof m.name, title);
    map_make_template(&m, tpl, &p->def);
    char path[1024];
    project_map_path(p, file, path, sizeof path);
    bool ok = map_save(&m, path);
    map_free(&m);
    if (!ok) return -1;
    ProjMap *pm = &p->maps[p->nmaps++];
    sf_strcpy(pm->file, sizeof pm->file, file);
    sf_strcpy(pm->title, sizeof pm->title, title);
    project_save(p);
    return p->nmaps - 1;
}

bool project_delete_map(Project *p, int idx) {
    if (idx < 0 || idx >= p->nmaps || p->nmaps <= 1) return false;
    char path[1024];
    project_map_path(p, p->maps[idx].file, path, sizeof path);
    plat_recycle(path);
    memmove(&p->maps[idx], &p->maps[idx + 1], sizeof(ProjMap) * (size_t)(p->nmaps - idx - 1));
    p->nmaps--;
    if (project_find_map(p, p->last_map) < 0) sf_strcpy(p->last_map, sizeof p->last_map, p->maps[0].file);
    project_save(p);
    return true;
}

bool project_create(Project *p, const char *parent, const char *name, int tpl, char *err, size_t errcap) {
    memset(p, 0, sizeof *p);
    map_defaults_init(&p->def);
    char clean[64];
    sf_strcpy(clean, sizeof clean, name);
    sf_sanitize_name(clean);
    if (!clean[0]) { snprintf(err, errcap, "Please enter a project name."); return false; }
    sf_strcpy(p->name, sizeof p->name, name);
    sf_path_join(p->dir, sizeof p->dir, parent, clean);
    if (plat_exists(p->dir)) { snprintf(err, errcap, "A folder named \"%s\" already exists there.", clean); return false; }
    char sub[600];
    if (!plat_mkdirs(p->dir)) { snprintf(err, errcap, "Could not create the project folder."); return false; }
    sf_path_join(sub, sizeof sub, p->dir, "maps"); plat_mkdirs(sub);
    sf_path_join(sub, sizeof sub, p->dir, "textures"); plat_mkdirs(sub);
    sf_path_join(p->file, sizeof p->file, p->dir, "project.sfproj");
    p->created = (int64_t)time(NULL);
    project_save(p);
    int i = project_add_map(p, "E1M1", tpl);
    if (i < 0) { snprintf(err, errcap, "Could not write the first map."); return false; }
    sf_strcpy(p->last_map, sizeof p->last_map, p->maps[i].file);
    project_save(p);
    projects_remember(p->file);
    return true;
}

/* ------------------------------------------------------------------ recent list */
static void recent_path(char *out, size_t cap) {
    char ad[512], dir[600];
    plat_appdata_dir(ad, sizeof ad);
    sf_path_join(dir, sizeof dir, ad, "SectorForge");
    plat_mkdirs(dir);
    sf_path_join(out, cap, dir, "projects.txt");
}

static int read_recent(char (*out)[600], int max) {
    char path[700];
    recent_path(path, sizeof path);
    char *txt = sf_read_file(path, NULL);
    if (!txt) return 0;
    int n = 0;
    char *s = txt;
    while (*s && n < max) {
        char *e = s;
        while (*e && *e != '\n' && *e != '\r') e++;
        char c = *e;
        *e = 0;
        if (*s) sf_strcpy(out[n++], 600, s);
        if (!c) break;
        s = e + 1;
        while (*s == '\n' || *s == '\r') s++;
    }
    free(txt);
    return n;
}

static void write_recent(char (*list)[600], int n) {
    char path[700];
    recent_path(path, sizeof path);
    FILE *f = plat_fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", list[i]);
    fclose(f);
}

void projects_remember(const char *file) {
    static char list[128][600];
    int n = read_recent(list, 128);
    for (int i = 0; i < n; i++) if (!sf_stricmp(list[i], file)) return;
    if (n < 128) sf_strcpy(list[n++], 600, file);
    write_recent(list, n);
}

void projects_forget(const char *file) {
    static char list[128][600];
    int n = read_recent(list, 128), m = 0;
    for (int i = 0; i < n; i++) if (sf_stricmp(list[i], file)) memcpy(list[m++], list[i], 600);
    write_recent(list, m);
}

static void build_thumb(ProjectInfo *pi, const Project *p) {
    const char *mf = p->last_map[0] ? p->last_map : (p->nmaps ? p->maps[0].file : NULL);
    if (!mf) return;
    char path[1024];
    project_map_path(p, mf, path, sizeof path);
    Map m;
    map_init(&m);
    if (!map_load(&m, path)) { map_free(&m); return; }
    pi->nsectors = m.sectors.n;
    pi->nthings = m.things.n;
    double x0, y0, x1, y1;
    if (map_bounds(&m, &x0, &y0, &x1, &y1) && m.lines.n) {
        double span = SF_MAX(x1 - x0, y1 - y0);
        if (span < 1) span = 1;
        double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
        int n = SF_MIN(m.lines.n, 4000);
        pi->seg = malloc(sizeof(float) * 4 * (size_t)n);
        for (int i = 0; i < n; i++) {
            const MVert *a = &m.verts.a[m.lines.a[i].v1], *b = &m.verts.a[m.lines.a[i].v2];
            pi->seg[i * 4 + 0] = (float)((a->x - cx) / span + 0.5);
            pi->seg[i * 4 + 1] = (float)(0.5 - (a->y - cy) / span);
            pi->seg[i * 4 + 2] = (float)((b->x - cx) / span + 0.5);
            pi->seg[i * 4 + 3] = (float)(0.5 - (b->y - cy) / span);
        }
        pi->nseg = n;
    }
    map_free(&m);
}

static void add_info(ProjectList *list, const char *file) {
    for (int i = 0; i < list->n; i++) if (!sf_stricmp(list->a[i].file, file)) return;
    ProjectInfo pi;
    memset(&pi, 0, sizeof pi);
    sf_strcpy(pi.file, sizeof pi.file, file);
    Project p;
    if (!project_load(&p, file)) {
        pi.missing = true;
        sf_strcpy(pi.name, sizeof pi.name, sf_basename(file));
        arr_push(*list, pi);
        return;
    }
    sf_strcpy(pi.name, sizeof pi.name, p.name);
    sf_strcpy(pi.dir, sizeof pi.dir, p.dir);
    pi.mtime = plat_mtime(file);
    pi.nmaps = p.nmaps;
    for (int i = 0; i < p.nmaps; i++) {
        char mp[1024];
        project_map_path(&p, p.maps[i].file, mp, sizeof mp);
        int64_t t = plat_mtime(mp);
        if (t > pi.mtime) pi.mtime = t;
        if (pi.nmaptitles < 4) sf_strcpy(pi.maptitles[pi.nmaptitles++], 64, p.maps[i].title);
    }
    char td[700];
    project_tex_dir(&p, td, sizeof td);
    PlatDirEnt *ents = NULL;
    int n = plat_list_dir(td, &ents);
    for (int i = 0; i < n; i++) if (!ents[i].is_dir) pi.ntex++;
    free(ents);
    build_thumb(&pi, &p);
    arr_push(*list, pi);
}

static int cmp_info(const void *a, const void *b) {
    const ProjectInfo *x = a, *y = b;
    if (x->missing != y->missing) return x->missing ? 1 : -1;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : 0;
}

void projects_free(ProjectList *list) {
    for (int i = 0; i < list->n; i++) free(list->a[i].seg);
    arr_free(*list);
}

void projects_scan(ProjectList *list) {
    projects_free(list);
    static char rec[128][600];
    int n = read_recent(rec, 128);
    for (int i = 0; i < n; i++) if (plat_exists(rec[i])) add_info(list, rec[i]);
    PlatDirEnt *ents = NULL;
    int m = plat_list_dir(g_settings.projects_dir, &ents);
    for (int i = 0; i < m; i++) {
        if (!ents[i].is_dir) continue;
        char d[700], f[800];
        sf_path_join(d, sizeof d, g_settings.projects_dir, ents[i].name);
        sf_path_join(f, sizeof f, d, "project.sfproj");
        if (plat_exists(f)) add_info(list, f);
    }
    free(ents);
    if (list->n > 1) qsort(list->a, (size_t)list->n, sizeof(ProjectInfo), cmp_info);
}

bool project_duplicate(const char *file, char *newfile, size_t cap) {
    Project p;
    if (!project_load(&p, file)) return false;
    char parent[512];
    sf_strcpy(parent, sizeof parent, p.dir);
    char *sl = strrchr(parent, '\\'), *sl2 = strrchr(parent, '/');
    if (sl2 > sl) sl = sl2;
    if (!sl) return false;
    *sl = 0;
    char dst[600], nm[80];
    for (int k = 2; k < 100; k++) {
        snprintf(nm, sizeof nm, "%s %d", sf_basename(p.dir), k);
        sf_path_join(dst, sizeof dst, parent, nm);
        if (!plat_exists(dst)) break;
    }
    if (!plat_copy_dir(p.dir, dst)) return false;
    Project q;
    char nf[700];
    sf_path_join(nf, sizeof nf, dst, "project.sfproj");
    if (!project_load(&q, nf)) return false;
    snprintf(q.name, sizeof q.name, "%s (copy)", p.name);
    project_save(&q);
    projects_remember(nf);
    if (newfile) sf_strcpy(newfile, cap, nf);
    return true;
}

/* ------------------------------------------------------------------ settings */
void settings_default_projects_dir(char *out, size_t cap) {
    char docs[512], sf[600];
    plat_documents_dir(docs, sizeof docs);
    sf_path_join(sf, sizeof sf, docs, "SectorForge");
    sf_path_join(out, cap, sf, "Projects");
}

static void settings_path(char *out, size_t cap) {
    char ad[512], dir[600];
    plat_appdata_dir(ad, sizeof ad);
    sf_path_join(dir, sizeof dir, ad, "SectorForge");
    plat_mkdirs(dir);
    sf_path_join(out, cap, dir, "settings.ini");
}

void settings_load(void) {
    Settings *s = &g_settings;
    s->mouse_sens = 1.0f; s->fov = 90; s->invert_y = false; s->show_fps = true; s->vsync = true;
    s->autosave = true; s->textured_2d = true; s->default_grid = 32;
    settings_default_projects_dir(s->projects_dir, sizeof s->projects_dir);
    char path[700];
    settings_path(path, sizeof path);
    char *txt = sf_read_file(path, NULL);
    if (txt) {
        char *line = txt;
        while (*line) {
            char *e = line;
            while (*e && *e != '\n') e++;
            char c = *e;
            *e = 0;
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                const char *k = line, *v = eq + 1;
                size_t vl = strlen(v);
                char val[512];
                sf_strcpy(val, sizeof val, v);
                if (vl && val[vl - 1] == '\r') val[vl - 1] = 0;
                if (!strcmp(k, "mouse_sens")) s->mouse_sens = (float)atof(val);
                else if (!strcmp(k, "fov")) s->fov = (float)atof(val);
                else if (!strcmp(k, "invert_y")) s->invert_y = atoi(val) != 0;
                else if (!strcmp(k, "show_fps")) s->show_fps = atoi(val) != 0;
                else if (!strcmp(k, "vsync")) s->vsync = atoi(val) != 0;
                else if (!strcmp(k, "autosave")) s->autosave = atoi(val) != 0;
                else if (!strcmp(k, "textured_2d")) s->textured_2d = atoi(val) != 0;
                else if (!strcmp(k, "default_grid")) s->default_grid = atoi(val);
                else if (!strcmp(k, "projects_dir") && val[0]) sf_strcpy(s->projects_dir, sizeof s->projects_dir, val);
            }
            if (!c) break;
            line = e + 1;
        }
        free(txt);
    }
    s->mouse_sens = SF_CLAMP(s->mouse_sens, 0.1f, 5.0f);
    s->fov = SF_CLAMP(s->fov, 60, 120);
    if (s->default_grid < 1 || s->default_grid > 512) s->default_grid = 32;
}

void settings_save(void) {
    char path[700];
    settings_path(path, sizeof path);
    FILE *f = plat_fopen(path, "wb");
    if (!f) return;
    Settings *s = &g_settings;
    fprintf(f, "mouse_sens=%g\nfov=%g\ninvert_y=%d\nshow_fps=%d\nvsync=%d\nautosave=%d\ntextured_2d=%d\ndefault_grid=%d\nprojects_dir=%s\n",
            s->mouse_sens, s->fov, s->invert_y, s->show_fps, s->vsync, s->autosave, s->textured_2d, s->default_grid, s->projects_dir);
    fclose(f);
}
