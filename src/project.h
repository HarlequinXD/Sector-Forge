/* Sector Forge - projects on disk, recent list, settings */
#ifndef SF_PROJECT_H
#define SF_PROJECT_H
#include "map.h"

#define MAX_MAPS 64

typedef struct { char file[64]; char title[64]; } ProjMap;

typedef struct {
    char name[64];
    char dir[512];
    char file[600];
    ProjMap maps[MAX_MAPS];
    int nmaps;
    char last_map[64];
    MapDefaults def;
    int64_t created;
} Project;

typedef struct {
    char name[64];
    char file[600];
    char dir[512];
    int64_t mtime;
    int nmaps, ntex, nsectors, nthings;
    float *seg;    /* x0,y0,x1,y1 normalised to 0..1 */
    int nseg;
    bool missing;
    char maptitles[4][64];
    int nmaptitles;
} ProjectInfo;
typedef ARR(ProjectInfo) ProjectList;

typedef struct {
    float mouse_sens, fov;
    bool invert_y, show_fps, vsync, autosave, textured_2d;
    int default_grid;
    char projects_dir[512];
} Settings;
extern Settings g_settings;

bool project_create(Project *p, const char *parent_dir, const char *name, int tpl, char *err, size_t errcap);
bool project_load(Project *p, const char *file);
bool project_save(const Project *p);
void project_map_path(const Project *p, const char *mapfile, char *out, size_t cap);
void project_tex_dir(const Project *p, char *out, size_t cap);
int project_add_map(Project *p, const char *title, int tpl);   /* returns index or -1 */
bool project_delete_map(Project *p, int idx);
int project_find_map(const Project *p, const char *file);

void projects_scan(ProjectList *list);
void projects_free(ProjectList *list);
void projects_remember(const char *file);
void projects_forget(const char *file);
bool project_duplicate(const char *file, char *newfile, size_t cap);

void settings_load(void);
void settings_save(void);
void settings_default_projects_dir(char *out, size_t cap);

#endif
