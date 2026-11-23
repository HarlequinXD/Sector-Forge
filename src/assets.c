/* Sector Forge - asset library */
#include "assets.h"
#include "plat.h"
#include <ctype.h>

Assets g_assets;

static void add_tex(const char *name, Tex t, int cat, bool builtin, const char *src) {
    TexEntry e;
    memset(&e, 0, sizeof e);
    sf_strcpy(e.name, TEXNAME, name);
    e.tex = t; e.cat = cat; e.builtin = builtin;
    e.hash = sf_hash(e.name, 0);
    if (src) sf_strcpy(e.src_path, sizeof e.src_path, src);
    arr_push(g_assets.tex, e);
}

int tex_find(const char *name) {
    if (!name || !name[0]) return -1;
    uint32_t h = sf_hash(name, 0);
    for (int i = 0; i < g_assets.tex.n; i++)
        if (g_assets.tex.a[i].hash == h && !strcmp(g_assets.tex.a[i].name, name)) return i;
    return -1;
}

const Tex *tex_get(const char *name) {
    int i = tex_find(name);
    return i >= 0 ? &g_assets.tex.a[i].tex : &g_assets.missing;
}

bool tex_is_sky(const char *name) { return name && !strcmp(name, "F_SKY"); }
bool tex_is_liquid(const char *name) {
    int i = tex_find(name);
    return i >= 0 && g_assets.tex.a[i].cat == TEXCAT_LIQUID;
}

void assets_make_logo(uint8_t *rgba, int S) {
    memset(rgba, 0, (size_t)S * S * 4);
    /* rounded dark tile with the forge-orange hex mark */
    int isz = (int)(S * 0.78f);
    uint8_t *ia = calloc((size_t)isz * isz, 1);
    icons_raster_single(IC_logo, isz, ia);
    int off = (S - isz) / 2;
    float r = S * 0.22f;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = SF_MAX(SF_MAX(r - x, x - (S - 1 - r)), 0), dy = SF_MAX(SF_MAX(r - y, y - (S - 1 - r)), 0);
            float d = sqrtf(dx * dx + dy * dy) - r;
            float a = SF_CLAMP(0.5f - d, 0, 1);
            uint8_t *p = rgba + (y * S + x) * 4;
            p[0] = 0x1D; p[1] = 0x14; p[2] = 0x0D; p[3] = (uint8_t)(a * 255);
            int u = x - off, v = y - off;
            if (u >= 0 && v >= 0 && u < isz && v < isz) {
                float k = ia[v * isz + u] / 255.f;
                p[0] = (uint8_t)(p[0] + (0xFF - p[0]) * k);
                p[1] = (uint8_t)(p[1] + (0x7A - p[1]) * k);
                p[2] = (uint8_t)(p[2] + (0x1A - p[2]) * k);
            }
        }
    free(ia);
}

float assets_load_step(int *stage, char *status, size_t cap) {
    int nt = texgen_count();
    int total = 3 + nt + TH_COUNT + 1;
    int s = *stage;
    if (s == 0) {
        snprintf(status, cap, "Rasterising icon set \xC2\xB7 %d icons", IC_COUNT);
        uint8_t *ic = calloc(ICON_ATLAS_W * ICON_ATLAS_H * 4, 1);
        icons_raster(ic, 0, IC_COUNT);
        g_assets.icons = gfx_tex_create(ic, ICON_ATLAS_W, ICON_ATLAS_H, true, false, false);
        gfx_set_icon_atlas(g_assets.icons);
        free(ic);
        uint8_t logo[64 * 64 * 4];
        assets_make_logo(logo, 64);
        plat_set_app_icon(logo, 64, 64);
    } else if (s == 1) {
        snprintf(status, cap, "Preparing renderer");
        uint8_t chk[64 * 64 * 4];
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            bool c = ((x / 16) + (y / 16)) & 1;
            uint8_t *p = chk + (y * 64 + x) * 4;
            p[0] = c ? 255 : 30; p[1] = c ? 0 : 30; p[2] = c ? 220 : 30; p[3] = 255;
        }
        g_assets.missing = gfx_tex_create(chk, 64, 64, true, true, true);
    } else if (s < 2 + nt) {
        int i = s - 2;
        snprintf(status, cap, "Generating texture library \xC2\xB7 %d / %d", i + 1, nt);
        uint8_t *px = malloc(TEXGEN_SIZE * TEXGEN_SIZE * 4);
        texgen_make(i, px);
        add_tex(texgen_name(i), gfx_tex_create(px, TEXGEN_SIZE, TEXGEN_SIZE, true, true, false), texgen_category(i), true, NULL);
        free(px);
    } else if (s < 2 + nt + TH_COUNT) {
        int k = s - 2 - nt;
        snprintf(status, cap, "Building sprites \xC2\xB7 %s", g_thing_info[k].name);
        uint8_t *px = malloc(SPRITE_W * SPRITE_H * 4);
        sprite_make(k, px);
        g_assets.sprites[k] = gfx_tex_create(px, SPRITE_W, SPRITE_H, true, false, false);
        free(px);
    } else if (s == 2 + nt + TH_COUNT) {
        snprintf(status, cap, "Painting the sky");
        uint8_t *px = malloc(SKY_W * SKY_H * 4);
        texgen_sky(px);
        g_assets.sky = gfx_tex_create(px, SKY_W, SKY_H, true, true, false);
        free(px);
        px = malloc(WEAPON_W * WEAPON_H * 4);
        weapon_make(px, false);
        g_assets.weapon = gfx_tex_create(px, WEAPON_W, WEAPON_H, true, false, false);
        weapon_make(px, true);
        g_assets.weapon_flash = gfx_tex_create(px, WEAPON_W, WEAPON_H, true, false, false);
        free(px);
        g_assets.ready = true;
    }
    (*stage)++;
    return (float)*stage / (float)total;
}

void assets_clear_project(void) {
    int n = 0;
    for (int i = 0; i < g_assets.tex.n; i++) {
        if (g_assets.tex.a[i].builtin) g_assets.tex.a[n++] = g_assets.tex.a[i];
        else gfx_tex_free(&g_assets.tex.a[i].tex);
    }
    g_assets.tex.n = n;
}

static int pow2_floor(int v) { int p = 1; while (p * 2 <= v) p *= 2; return p; }

static Tex make_from_image(uint8_t *px, int w, int h) {
    /* resample to power-of-two (max 512) for GL 1.1 + repeat wrapping */
    int tw = SF_MIN(512, pow2_floor(w)), th = SF_MIN(512, pow2_floor(h));
    if (tw < 8) tw = 8;
    if (th < 8) th = 8;
    uint8_t *o = malloc((size_t)tw * th * 4);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            /* box filter */
            int x0 = x * w / tw, x1 = SF_MAX(x0 + 1, (x + 1) * w / tw), y0 = y * h / th, y1 = SF_MAX(y0 + 1, (y + 1) * h / th);
            unsigned acc[4] = { 0 }, cnt = 0;
            for (int yy = y0; yy < y1 && yy < h; yy++)
                for (int xx = x0; xx < x1 && xx < w; xx++) {
                    const uint8_t *p = px + ((size_t)yy * w + xx) * 4;
                    for (int k = 0; k < 4; k++) acc[k] += p[k];
                    cnt++;
                }
            for (int k = 0; k < 4; k++) o[(y * tw + x) * 4 + k] = (uint8_t)(cnt ? acc[k] / cnt : 0);
        }
    Tex t = gfx_tex_create(o, tw, th, true, true, false);
    free(o);
    return t;
}

static void name_from_file(const char *file, char *out) {
    char tmp[260];
    sf_strcpy(tmp, sizeof tmp, sf_basename(file));
    sf_strip_ext(tmp);
    int n = 0;
    for (int i = 0; tmp[i] && n < TEXNAME - 1; i++) {
        char c = (char)toupper((unsigned char)tmp[i]);
        if (!isalnum((unsigned char)c)) c = '_';
        out[n++] = c;
    }
    out[n] = 0;
    if (!n) sf_strcpy(out, TEXNAME, "TEXTURE");
}

int assets_load_project_textures(const char *dir) {
    PlatDirEnt *ents = NULL;
    int n = plat_list_dir(dir, &ents), count = 0;
    for (int i = 0; i < n; i++) {
        if (ents[i].is_dir) continue;
        const char *ext = strrchr(ents[i].name, '.');
        if (!ext || !(sf_stricmp(ext, ".png") == 0 || sf_stricmp(ext, ".jpg") == 0 || sf_stricmp(ext, ".jpeg") == 0 ||
                      sf_stricmp(ext, ".bmp") == 0 || sf_stricmp(ext, ".tga") == 0 || sf_stricmp(ext, ".gif") == 0)) continue;
        char path[1024], name[TEXNAME];
        sf_path_join(path, sizeof path, dir, ents[i].name);
        name_from_file(ents[i].name, name);
        if (tex_find(name) >= 0) continue;
        int w, h;
        uint8_t *px = plat_load_image(path, &w, &h);
        if (!px) continue;
        add_tex(name, make_from_image(px, w, h), TEXCAT_WALL, false, path);
        free(px);
        count++;
    }
    free(ents);
    return count;
}

bool assets_import_texture(const char *src, const char *dest_dir, char *out_name) {
    char name[TEXNAME];
    name_from_file(src, name);
    int w, h;
    uint8_t *px = plat_load_image(src, &w, &h);
    if (!px) return false;
    plat_mkdirs(dest_dir);
    char dst[1024];
    sf_path_join(dst, sizeof dst, dest_dir, sf_basename(src));
    if (sf_stricmp(src, dst) != 0) plat_copy_file(src, dst);
    int existing = tex_find(name);
    if (existing >= 0 && !g_assets.tex.a[existing].builtin) {
        gfx_tex_free(&g_assets.tex.a[existing].tex);
        g_assets.tex.a[existing].tex = make_from_image(px, w, h);
    } else {
        if (existing >= 0) { /* clashes with a built-in name: suffix it */
            size_t l = strlen(name);
            if (l > TEXNAME - 3) l = TEXNAME - 3;
            name[l] = '_'; name[l + 1] = '2'; name[l + 2] = 0;
        }
        add_tex(name, make_from_image(px, w, h), TEXCAT_WALL, false, dst);
    }
    free(px);
    if (out_name) sf_strcpy(out_name, TEXNAME, name);
    return true;
}
