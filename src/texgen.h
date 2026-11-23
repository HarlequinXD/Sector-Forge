/* Sector Forge - procedural texture & sprite generation */
#ifndef SF_TEXGEN_H
#define SF_TEXGEN_H
#include "base.h"

#define TEXGEN_SIZE 128
enum { TEXCAT_WALL, TEXCAT_FLAT, TEXCAT_LIQUID, TEXCAT_SKY };

int texgen_count(void);
const char *texgen_name(int i);
int texgen_category(int i);
void texgen_make(int i, uint8_t *rgba);             /* TEXGEN_SIZE^2 * 4 bytes */

#define SKY_W 512
#define SKY_H 256
void texgen_sky(uint8_t *rgba);                     /* SKY_W x SKY_H */

#define SPRITE_W 64
#define SPRITE_H 128
void sprite_make(int thing_type, uint8_t *rgba);    /* SPRITE_W x SPRITE_H, bottom-aligned */

#define WEAPON_W 128
#define WEAPON_H 128
void weapon_make(uint8_t *rgba, bool flash);        /* WEAPON_W x WEAPON_H */

#endif
