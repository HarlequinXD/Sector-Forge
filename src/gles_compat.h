/* Sector Forge - OpenGL 1.1 / GLU subset emulated on OpenGL ES 3.0.
   The renderer is written against the fixed-function API Windows ships; on
   Android these macros route those calls into a small batching emulator
   (one shader, a streamed vertex buffer, software matrix stacks). */
#ifndef SF_GLES_COMPAT_H
#define SF_GLES_COMPAT_H
#include <GLES3/gl3.h>
#include <stdbool.h>

typedef double GLdouble;
typedef double GLclampd;

#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_ALPHA_TEST 0x0BC0
#define GL_FOG 0x0B60
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_FOG_HINT 0x0C54
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_LINE_SMOOTH 0x0B20
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE

/* immediate mode */
void sfgl_Begin(GLenum mode);
void sfgl_End(void);
void sfgl_Vertex2f(float x, float y);
void sfgl_Vertex3f(float x, float y, float z);
void sfgl_TexCoord2f(float u, float v);
void sfgl_Color3f(float r, float g, float b);
void sfgl_Color4f(float r, float g, float b, float a);
void sfgl_Color3ub(GLubyte r, GLubyte g, GLubyte b);
void sfgl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
/* matrices */
void sfgl_MatrixMode(GLenum m);
void sfgl_LoadIdentity(void);
void sfgl_PushMatrix(void);
void sfgl_PopMatrix(void);
void sfgl_Ortho(double l, double r, double b, double t, double n, double f);
void sfgl_Translatef(float x, float y, float z);
void sfgl_Scalef(float x, float y, float z);
void sfgl_Rotatef(float a, float x, float y, float z);
void sfgl_GetDoublev(GLenum p, GLdouble *v);
void sfgl_Perspective(double fovy, double aspect, double zn, double zf);
void sfgl_LookAt(double ex, double ey, double ez, double cx, double cy, double cz, double ux, double uy, double uz);
GLint sfgl_Project(double ox, double oy, double oz, const double mv[16], const double pr[16], const GLint vp[4], double *wx, double *wy, double *wz);
/* fixed-function state */
void sfgl_Enable(GLenum cap);
void sfgl_Disable(GLenum cap);
void sfgl_TexEnvi(GLenum t, GLenum p, GLint v);
void sfgl_AlphaFunc(GLenum f, float ref);
void sfgl_Fogi(GLenum p, GLint v);
void sfgl_Fogf(GLenum p, float v);
void sfgl_Fogfv(GLenum p, const float *v);
void sfgl_PolygonMode(GLenum face, GLenum mode);
void sfgl_Hint(GLenum t, GLenum m);
void sfgl_ClearDepth(double d);
/* native calls that must flush pending geometry first */
void sfgl_BindTexture(GLenum t, GLuint id);
void sfgl_BlendFunc(GLenum s, GLenum d);
void sfgl_DepthFunc(GLenum f);
void sfgl_DepthMask(GLboolean m);
void sfgl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void sfgl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h);
void sfgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void sfgl_Clear(GLbitfield m);
void sfgl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p);
void sfgl_PolygonOffset(float f, float u);
void sfgl_LineWidth(float w);
void sfgl_TexImage2D(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p);
void sfgl_TexParameteri(GLenum t, GLenum p, GLint v);
void sfgl_TexParameterf(GLenum t, GLenum p, float v);
void sfgl_DeleteTextures(GLsizei n, const GLuint *ids);

/* lifecycle / helpers (not part of GL) */
bool sfgl_init(void);           /* after a context is current; safe to call again after context loss */
void sfgl_flush(void);          /* draw pending batch (call before swapping buffers) */
void sfgl_set_surface_size(int w, int h); /* default framebuffer size in pixels */
void sfgl_set_default_fbo(GLuint fbo);      /* headless tests render into an FBO */
void sfgl_offscreen(bool on);  /* single-sample surface-sized target for ID picking */
int sfgl_draw_calls(void);      /* draws since last call (stats) */

#ifndef SF_GLES_COMPAT_IMPL
#define glBegin sfgl_Begin
#define glEnd sfgl_End
#define glVertex2f sfgl_Vertex2f
#define glVertex3f sfgl_Vertex3f
#define glTexCoord2f sfgl_TexCoord2f
#define glColor3f sfgl_Color3f
#define glColor4f sfgl_Color4f
#define glColor3ub sfgl_Color3ub
#define glColor4ub sfgl_Color4ub
#define glMatrixMode sfgl_MatrixMode
#define glLoadIdentity sfgl_LoadIdentity
#define glPushMatrix sfgl_PushMatrix
#define glPopMatrix sfgl_PopMatrix
#define glOrtho sfgl_Ortho
#define glTranslatef sfgl_Translatef
#define glScalef sfgl_Scalef
#define glRotatef sfgl_Rotatef
#define glGetDoublev sfgl_GetDoublev
#define gluPerspective sfgl_Perspective
#define gluLookAt sfgl_LookAt
#define gluProject sfgl_Project
#define glEnable sfgl_Enable
#define glDisable sfgl_Disable
#define glTexEnvi sfgl_TexEnvi
#define glAlphaFunc sfgl_AlphaFunc
#define glFogi sfgl_Fogi
#define glFogf sfgl_Fogf
#define glFogfv sfgl_Fogfv
#define glPolygonMode sfgl_PolygonMode
#define glHint sfgl_Hint
#define glClearDepth sfgl_ClearDepth
#define glBindTexture sfgl_BindTexture
#define glBlendFunc sfgl_BlendFunc
#define glDepthFunc sfgl_DepthFunc
#define glDepthMask sfgl_DepthMask
#define glColorMask sfgl_ColorMask
#define glScissor sfgl_Scissor
#define glViewport sfgl_Viewport
#define glClear sfgl_Clear
#define glReadPixels sfgl_ReadPixels
#define glPolygonOffset sfgl_PolygonOffset
#define glLineWidth sfgl_LineWidth
#define glTexImage2D sfgl_TexImage2D
#define glTexParameteri sfgl_TexParameteri
#define glTexParameterf sfgl_TexParameterf
#define glDeleteTextures sfgl_DeleteTextures
#endif

#endif
