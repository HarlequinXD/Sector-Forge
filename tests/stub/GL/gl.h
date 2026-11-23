/* generated stub GL header for the headless test harness */
#pragma once
#include <stdint.h>
typedef unsigned int GLenum; typedef unsigned char GLboolean; typedef unsigned int GLbitfield; typedef int GLint; typedef int GLsizei;
typedef unsigned int GLuint; typedef float GLfloat; typedef double GLdouble; typedef unsigned char GLubyte; typedef double GLclampd; typedef float GLclampf;
typedef struct GLUtesselator GLUtesselator;
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_REPEAT 0x2901
#define GL_LESS 0x0201
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_LINE 0x1B01
#define GL_REPLACE 0x1E01

#define GLU_TESS_BEGIN 100100
#define GLU_TESS_COMBINE 100105
#define GLU_TESS_EDGE_FLAG 100104
#define GLU_TESS_ERROR 100103
#define GLU_TESS_VERTEX 100101
#define GLU_TESS_WINDING_ODD 100130
#define GLU_TESS_WINDING_RULE 100140
#define GL_ALPHA_TEST 0x0BC0
#define GL_BLEND 0x0BE2
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_DEPTH_TEST 0x0B71
#define GL_DITHER 0x0BD0
#define GL_EQUAL 0x0202
#define GL_EXP2 0x0801
#define GL_EXTENSIONS 0x1F03
#define GL_FALSE 0
#define GL_FILL 0x1B02
#define GL_FOG 0x0B60
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_HINT 0x0C54
#define GL_FOG_MODE 0x0B65
#define GL_FRONT_AND_BACK 0x0408
#define GL_GREATER 0x0204
#define GL_LEQUAL 0x0203
#define GL_LINE 0x1B01
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_MODELVIEW 0x1700
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_MODULATE 0x2100
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_NICEST 0x1102
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_PROJECTION 0x1701
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_QUADS 0x0007
#define GL_REPEAT 0x2901
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_FAN 0x0006
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRUE 1
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VIEWPORT 0x0BA2
const GLubyte * glGetString();
GLUtesselator * gluNewTess();
void glMatrixMode(GLenum m);
void glLoadIdentity(void);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void gluPerspective(GLdouble fovy, GLdouble a, GLdouble n, GLdouble f);
void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz, GLdouble ux, GLdouble uy, GLdouble uz);
void glGetDoublev(GLenum e, GLdouble *d);
void glGetFloatv(GLenum e, GLfloat *d);
void glGetIntegerv(GLenum e, GLint *d);
GLint gluProject(GLdouble ox, GLdouble oy, GLdouble oz, const GLdouble *mv, const GLdouble *pr, const GLint *vp, GLdouble *wx, GLdouble *wy, GLdouble *wz);
const GLubyte *glGetString(GLenum e);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glEnable(GLenum e);
void glDisable(GLenum e);
void glAlphaFunc(GLenum f, GLclampf r);
void glBlendFunc(GLenum a, GLenum b);
void glDepthFunc(GLenum f);
void glDepthMask(GLboolean m);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glPolygonMode(GLenum f, GLenum m);
void glTexEnvi(GLenum t, GLenum p, GLint v);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepth(GLclampd d);
void glClear(GLbitfield m);
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p);
void glGenTextures(GLsizei n, GLuint *t);
void glDeleteTextures(GLsizei n, const GLuint *t);
void glBindTexture(GLenum t, GLuint id);
void glTexParameteri(GLenum t, GLenum p, GLint v);
void glTexParameterf(GLenum t, GLenum p, GLfloat v);
void glPixelStorei(GLenum p, GLint v);
void glTexImage2D(GLenum tg, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum ty, const void *p);
void glHint(GLenum a, GLenum b);
void glFogf(GLenum a, GLfloat b);
void glFogi(GLenum a, GLint b);
void glFogfv(GLenum a, const GLfloat *b);
void glLineWidth(GLfloat w);
void glPolygonOffset(GLfloat a, GLfloat b);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glTexCoord2f(GLfloat u, GLfloat v);
void glBegin(GLenum p);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glEnd(void);
GLUtesselator *gluNewTess(void);
void gluDeleteTess(GLUtesselator *t);
void gluTessProperty(GLUtesselator *t, GLenum w, GLdouble v);
void gluTessNormal(GLUtesselator *t, GLdouble x, GLdouble y, GLdouble z);
void gluTessBeginPolygon(GLUtesselator *t, void *d);
void gluTessBeginContour(GLUtesselator *t);
void gluTessVertex(GLUtesselator *t, GLdouble *c, void *d);
void gluTessEndContour(GLUtesselator *t);
void gluTessEndPolygon(GLUtesselator *t);
void gluTessCallback(GLUtesselator *t, GLenum w, void (*fn)());
extern int g_sw_enabled;
void sw_dump(const char *path);
