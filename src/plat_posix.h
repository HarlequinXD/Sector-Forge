/* Sector Forge - helpers shared by POSIX platform layers (Android, Linux test shells) */
#ifndef SF_PLAT_POSIX_H
#define SF_PLAT_POSIX_H
#include "plat.h"

void posix_set_dirs(const char *documents, const char *appdata);
double posix_now(void);

/* stb_truetype rasteriser producing the same atlas layout as the GDI path in win32.c */
bool font_raster_ttf(const uint8_t *ttf, size_t size, float px, const uint32_t *cps, int ncp, FontRaster *out);
/* maps a Windows face name + weight to a bundled font file name ("Inter-SemiBold.otf") */
const char *font_file_for(const char *face, int weight);
/* stb_image decode to RGBA8 (caller frees) */
uint8_t *image_decode(const uint8_t *data, size_t size, int *w, int *h);

#endif
