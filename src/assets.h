/* Sector Forge - GPU asset library: textures (built-in + project), sprites, sky, icons */
#ifndef SF_ASSETS_H
#define SF_ASSETS_H
#include "gfx.h"
#include "map.h"
#include "texgen.h"

typedef struct {
    char name[TEXNAME];
    Tex tex;
    int cat;
    bool builtin;
    uint32_t hash;
    char src_path[260];
} TexEntry;

typedef struct {
    ARR(TexEntry) tex;
    Tex missing;
    Tex sky;
    Tex sprites[TH_COUNT];
    Tex weapon, weapon_flash;
    Tex icons;
    bool ready;
} Assets;

extern Assets g_assets;

/* staged startup: returns progress 0..1 after doing one chunk of work; call until it returns >= 1 */
float assets_load_step(int *stage, char *status, size_t cap);
int tex_find(const char *name);
const Tex *tex_get(const char *name);        /* never NULL; returns checker for unknown names */
bool tex_is_sky(const char *name);
bool tex_is_liquid(const char *name);
void assets_clear_project(void);
int assets_load_project_textures(const char *dir);   /* returns count */
bool assets_import_texture(const char *src, const char *dest_dir, char *out_name);
void assets_make_logo(uint8_t *rgba, int size);

#endif
