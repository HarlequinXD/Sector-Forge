#include "base.h"
#include "plat.h"
#include <stdarg.h>
#include <ctype.h>

void *sf_grow(void *ptr, int *cap, int need, size_t elem) {
    if (need <= *cap) return ptr;
    int nc = *cap ? *cap * 2 : 16;
    while (nc < need) nc *= 2;
    void *p = realloc(ptr, (size_t)nc * elem);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    *cap = nc;
    return p;
}

void buf_append(Buf *b, const char *s, int len) {
    if (len < 0) len = (int)strlen(s);
    b->s = sf_grow(b->s, &b->cap, b->n + len + 1, 1);
    memcpy(b->s + b->n, s, (size_t)len);
    b->n += len;
    b->s[b->n] = 0;
}

void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n < (int)sizeof tmp) { buf_append(b, tmp, n); return; }
    char *big = malloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_append(b, big, n);
    free(big);
}

void buf_free(Buf *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }

void sf_strcpy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

char *sf_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

int sf_stricmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

bool sf_stristr(const char *hay, const char *needle) {
    if (!*needle) return true;
    size_t nl = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

void sf_upper(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

uint32_t sf_hash(const char *s, uint32_t seed) {
    uint32_t h = 2166136261u ^ seed;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

const char *sf_basename(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

void sf_path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t la = strlen(a);
    if (la && (a[la - 1] == '/' || a[la - 1] == '\\')) snprintf(out, cap, "%s%s", a, b);
    else snprintf(out, cap, "%s\\%s", a, b);
}

void sf_strip_ext(char *s) {
    char *dot = strrchr(s, '.');
    char *sl = strrchr(s, '\\');
    char *sl2 = strrchr(s, '/');
    if (sl2 > sl) sl = sl2;
    if (dot && (!sl || dot > sl)) *dot = 0;
}

void sf_sanitize_name(char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || strchr("<>:\"/\\|?*", c)) *s = '_';
    }
}

char *sf_read_file(const char *path, int *len) {
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *d = malloc((size_t)n + 1);
    size_t got = fread(d, 1, (size_t)n, f);
    fclose(f);
    d[got] = 0;
    if (len) *len = (int)got;
    return d;
}
