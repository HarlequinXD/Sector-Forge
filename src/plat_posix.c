/* Sector Forge - POSIX file helpers shared by the Android platform layer and the Linux test shells */
#define _GNU_SOURCE
#include "plat.h"
#include "plat_posix.h"
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char g_docs[512] = "/tmp", g_appdata[512] = "/tmp";

void posix_set_dirs(const char *documents, const char *appdata) {
    sf_strcpy(g_docs, sizeof g_docs, documents);
    sf_strcpy(g_appdata, sizeof g_appdata, appdata);
}

static void fix(const char *p, char *q, size_t cap) {
    sf_strcpy(q, cap, p);
    for (char *c = q; *c; c++) if (*c == '\\') *c = '/';
}

FILE *plat_fopen(const char *path, const char *mode) {
    char q[1024];
    fix(path, q, sizeof q);
    return fopen(q, mode);
}
bool plat_exists(const char *p) { char q[1024]; fix(p, q, sizeof q); struct stat s; return stat(q, &s) == 0; }
bool plat_is_dir(const char *p) { char q[1024]; fix(p, q, sizeof q); struct stat s; return stat(q, &s) == 0 && S_ISDIR(s.st_mode); }
int64_t plat_mtime(const char *p) { char q[1024]; fix(p, q, sizeof q); struct stat s; return stat(q, &s) == 0 ? (int64_t)s.st_mtime : 0; }

bool plat_mkdirs(const char *p) {
    char q[1024];
    fix(p, q, sizeof q);
    size_t n = strlen(q);
    if (!n) return false;
    for (size_t i = 1; i <= n; i++) {
        if (q[i] == '/' || q[i] == 0) {
            char c = q[i];
            q[i] = 0;
            if (mkdir(q, 0775) != 0 && errno != EEXIST) { q[i] = c; return false; }
            q[i] = c;
        }
    }
    return true;
}

int plat_list_dir(const char *d, PlatDirEnt **out) {
    char q[1024];
    fix(d, q, sizeof q);
    *out = NULL;
    DIR *dir = opendir(q);
    if (!dir) return 0;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        *out = sf_grow(*out, &cap, n + 1, sizeof(PlatDirEnt));
        sf_strcpy((*out)[n].name, 260, e->d_name);
        char f[1400];
        snprintf(f, sizeof f, "%s/%s", q, e->d_name);
        (*out)[n].is_dir = plat_is_dir(f);
        (*out)[n].mtime = plat_mtime(f);
        n++;
    }
    closedir(dir);
    return n;
}

static bool rm_rf(const char *q) {
    struct stat s;
    if (lstat(q, &s) != 0) return true;
    if (S_ISDIR(s.st_mode)) {
        DIR *dir = opendir(q);
        if (dir) {
            struct dirent *e;
            while ((e = readdir(dir))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                char f[1400];
                snprintf(f, sizeof f, "%s/%s", q, e->d_name);
                rm_rf(f);
            }
            closedir(dir);
        }
        return rmdir(q) == 0;
    }
    return unlink(q) == 0;
}

/* there is no recycle bin on Android: projects are moved to a .trash folder next to them so a
   mistaken delete can still be recovered over USB; the trash is emptied of anything older */
bool plat_recycle(const char *p) {
    char q[1024];
    fix(p, q, sizeof q);
    char parent[1024];
    sf_strcpy(parent, sizeof parent, q);
    char *sl = strrchr(parent, '/');
    if (!sl) return rm_rf(q);
    *sl = 0;
    char trash[1100];
    snprintf(trash, sizeof trash, "%s/.trash", parent);
    mkdir(trash, 0775);
    char dst[1400];
    snprintf(dst, sizeof dst, "%s/%s.%ld", trash, sl + 1, (long)time(NULL));
    if (rename(q, dst) == 0) return true;
    return rm_rf(q);
}

bool plat_copy_file(const char *a, const char *b) {
    char q[1024], r[1024];
    fix(a, q, sizeof q);
    fix(b, r, sizeof r);
    FILE *in = fopen(q, "rb");
    if (!in) return false;
    FILE *out = fopen(r, "wb");
    if (!out) { fclose(in); return false; }
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

bool plat_copy_dir(const char *a, const char *b) {
    char q[1024], r[1024];
    fix(a, q, sizeof q);
    fix(b, r, sizeof r);
    if (!plat_mkdirs(r)) return false;
    DIR *dir = opendir(q);
    if (!dir) return false;
    struct dirent *e;
    bool ok = true;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sa[1400], sb[1400];
        snprintf(sa, sizeof sa, "%s/%s", q, e->d_name);
        snprintf(sb, sizeof sb, "%s/%s", r, e->d_name);
        ok &= plat_is_dir(sa) ? plat_copy_dir(sa, sb) : plat_copy_file(sa, sb);
    }
    closedir(dir);
    return ok;
}

bool plat_delete_file(const char *p) { char q[1024]; fix(p, q, sizeof q); return unlink(q) == 0; }
void plat_documents_dir(char *o, size_t c) { sf_strcpy(o, c, g_docs); }
void plat_appdata_dir(char *o, size_t c) { sf_strcpy(o, c, g_appdata); }

double posix_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}
