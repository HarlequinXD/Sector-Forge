/* OpenGL includes. The renderer targets the GL 1.1 + GLU API that Windows ships;
   on OpenGL ES (Android) gles_compat.h emulates that subset. */
#ifndef SF_GLINC_H
#define SF_GLINC_H
#if defined(SF_GLES)
#include "gles_compat.h"
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#if defined(_WIN32)
#define SF_USE_GLU
#endif
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#define SF_GL_MULTISAMPLE 0x809D
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#endif
