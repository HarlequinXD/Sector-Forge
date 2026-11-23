/* Sector Forge - font atlases via stb_truetype and image decoding via stb_image (non-Windows builds) */
#include "plat_posix.h"

#define STB_TRUETYPE_IMPLEMENTATION

#include "../third_party/stb/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "../third_party/stb/stb_image.h"

const char *font_file_for(const char *face, int weight) {
    if (face && (!sf_stricmp(face, "Consolas") || strstr(face, "Mono")))
        return weight >= 600 ? "DejaVuSansMono-Bold.ttf" : "DejaVuSansMono.ttf";
    if (weight >= 800) return "Inter-ExtraBold.otf";
    if (weight >= 700) return "Inter-Bold.otf";
    if (weight >= 600) return "Inter-SemiBold.otf";
    if (weight >= 500) return "Inter-Medium.otf";
    return "Inter-Regular.otf";
}

/* Layout matches the GDI rasteriser: each glyph cell spans the full line height
   (top of cell = top of line), xoff is the pen-relative left edge, adv in pixels. */
bool font_raster_ttf(const uint8_t *ttf, size_t size, float px, const uint32_t *cps, int ncp, FontRaster *out) {
    (void)size;
    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return false;
    float sc = stbtt_ScaleForMappingEmToPixels(&fi, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    float ascent = asc * sc, height = (asc - desc) * sc;
    int base = (int)ceilf(ascent);
    int cell_h = (int)ceilf(height) + 3;
    int *bx0 = calloc((size_t)ncp, sizeof(int)), *by0 = calloc((size_t)ncp, sizeof(int));
    int *bw = calloc((size_t)ncp, sizeof(int)), *bh = calloc((size_t)ncp, sizeof(int));
    for (int i = 0; i < ncp; i++) {
        int x0, y0, x1, y1;
        int g = stbtt_FindGlyphIndex(&fi, (int)cps[i]);
        if (cps[i] == ' ' || !g) { bw[i] = 0; continue; }
        stbtt_GetGlyphBitmapBox(&fi, g, sc, sc, &x0, &y0, &x1, &y1);
        bx0[i] = x0; by0[i] = y0; bw[i] = x1 - x0; bh[i] = y1 - y0;
    }
    int W = 256, H;
    for (;;) {
        int x = 1, y = 1;
        for (int i = 0; i < ncp; i++) {
            int cw = bw[i] + 4;
            if (x + cw > W) { x = 1; y += cell_h + 2; }
            x += cw;
        }
        H = y + cell_h + 2;
        if (H <= W) break;
        W *= 2;
    }
    int AH = 1;
    while (AH < H) AH *= 2;
    H = AH;
    out->w = W; out->h = H;
    out->alpha = calloc((size_t)W * H, 1);
    out->glyphs = calloc((size_t)ncp, sizeof(GlyphInfo));
    out->nglyphs = ncp;
    int x = 1, y = 1;
    for (int i = 0; i < ncp; i++) {
        int cw = bw[i] + 4;
        if (x + cw > W) { x = 1; y += cell_h + 2; }
        GlyphInfo *gi = &out->glyphs[i];
        int g = stbtt_FindGlyphIndex(&fi, (int)cps[i]);
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&fi, g, &adv, &lsb);
        gi->cp = cps[i];
        gi->adv = adv * sc;
        if (bw[i] > 0) {
            int gy = y + base + by0[i];
            if (gy < y) gy = y;
            int hh = SF_MIN(bh[i], y + cell_h - gy);
            if (hh > 0) stbtt_MakeGlyphBitmap(&fi, out->alpha + (size_t)gy * W + x + 2, bw[i], hh, W, sc, sc, g);
            gi->x = x; gi->y = y; gi->w = bw[i] + 4; gi->h = cell_h;
            gi->xoff = (float)(bx0[i] - 2);
        }
        gi->yoff = 0;
        x += cw;
    }
    out->ascent = ascent;
    out->height = height;
    free(bx0); free(by0); free(bw); free(bh);
    return true;
}

uint8_t *image_decode(const uint8_t *data, size_t size, int *w, int *h) {
    int n;
    return stbi_load_from_memory(data, (int)size, w, h, &n, 4);
}
