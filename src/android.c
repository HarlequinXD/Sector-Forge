/* Sector Forge - Android platform layer (NativeActivity + native_app_glue, EGL / OpenGL ES 3).
   Touch, mouse (DeX), keyboard and gamepad arrive as AInputEvents; Java helpers in MainActivity
   handle orientation, haptics, the text-entry dialog and the gallery picker over JNI. */
#include "plat.h"
#include "plat_posix.h"
#include "gles_compat.h"
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/looper.h>
#include <EGL/egl.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SectorForge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SectorForge", __VA_ARGS__)

static struct android_app *g_app;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLConfig g_cfg;
static bool g_app_ready, g_focused, g_paused;
static float g_density = 2.625f;
static Input in;
static double g_t0;

/* ---------------------------------------------------------------- JNI bridge */
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static int g_ins[4];
static bool g_txt_ready;
static int g_txt_req;
static char g_txt[256];
static bool g_img_ready;
static char g_img[1024];

JNIEXPORT void JNICALL Java_com_harlequinxd_sectorforge_MainActivity_nativeInsets(JNIEnv *env, jclass c, jint l, jint t, jint r, jint b) {
    (void)env; (void)c;
    pthread_mutex_lock(&g_mx);
    g_ins[0] = l; g_ins[1] = t; g_ins[2] = r; g_ins[3] = b;
    pthread_mutex_unlock(&g_mx);
}
JNIEXPORT void JNICALL Java_com_harlequinxd_sectorforge_MainActivity_nativeTextResult(JNIEnv *env, jclass c, jint req, jstring s) {
    (void)c;
    const char *u = (*env)->GetStringUTFChars(env, s, NULL);
    pthread_mutex_lock(&g_mx);
    g_txt_req = req;
    sf_strcpy(g_txt, sizeof g_txt, u ? u : "");
    g_txt_ready = true;
    pthread_mutex_unlock(&g_mx);
    if (u) (*env)->ReleaseStringUTFChars(env, s, u);
}
JNIEXPORT void JNICALL Java_com_harlequinxd_sectorforge_MainActivity_nativeImagePicked(JNIEnv *env, jclass c, jstring s) {
    (void)c;
    const char *u = (*env)->GetStringUTFChars(env, s, NULL);
    pthread_mutex_lock(&g_mx);
    sf_strcpy(g_img, sizeof g_img, u ? u : "");
    g_img_ready = u && u[0];
    pthread_mutex_unlock(&g_mx);
    if (u) (*env)->ReleaseStringUTFChars(env, s, u);
}

static JNIEnv *jenv(void) {
    JNIEnv *env = NULL;
    JavaVM *vm = g_app->activity->vm;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) (*vm)->AttachCurrentThread(vm, &env, NULL);
    return env;
}
static void jcall(const char *name, const char *sig, ...) {
    JNIEnv *env = jenv();
    if (!env) return;
    jobject act = g_app->activity->clazz;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, name, sig);
    if (m) {
        va_list ap;
        va_start(ap, sig);
        (*env)->CallVoidMethodV(env, act, m, ap);
        va_end(ap);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, cls);
}

void plat_set_orientation(int o) { jcall("setOrientation", "(I)V", (jint)o); }
void plat_haptic(int kind) { jcall("haptic", "(I)V", (jint)kind); }
void plat_open_url(const char *url) {
    JNIEnv *env = jenv();
    jstring s = (*env)->NewStringUTF(env, url);
    jcall("openUrl", "(Ljava/lang/String;)V", s);
    (*env)->DeleteLocalRef(env, s);
}
bool plat_text_input_begin(int req, const char *title, const char *initial, int kind) {
    JNIEnv *env = jenv();
    jstring t = (*env)->NewStringUTF(env, title), i = (*env)->NewStringUTF(env, initial ? initial : "");
    jcall("textInput", "(ILjava/lang/String;Ljava/lang/String;I)V", (jint)req, t, i, (jint)kind);
    (*env)->DeleteLocalRef(env, t);
    (*env)->DeleteLocalRef(env, i);
    return true;
}
bool plat_text_input_poll(int *req, char *out, size_t cap) {
    bool r = false;
    pthread_mutex_lock(&g_mx);
    if (g_txt_ready) { *req = g_txt_req; sf_strcpy(out, cap, g_txt); g_txt_ready = false; r = true; }
    pthread_mutex_unlock(&g_mx);
    return r;
}
bool plat_pick_image_begin(void) { jcall("pickImage", "()V"); return true; }
bool plat_pick_image_poll(char *path, size_t cap) {
    bool r = false;
    pthread_mutex_lock(&g_mx);
    if (g_img_ready) { sf_strcpy(path, cap, g_img); g_img_ready = false; r = true; }
    pthread_mutex_unlock(&g_mx);
    return r;
}

/* ---------------------------------------------------------------- misc platform */
void plat_open_folder(const char *p) { (void)p; }
bool plat_open_file_dialog(char *o, size_t c, const char *t, const char *f, const char *p) { (void)o; (void)c; (void)t; (void)f; (void)p; return false; }
bool plat_pick_folder(char *o, size_t c, const char *t) { (void)o; (void)c; (void)t; return false; }
void plat_minimize(void) {}
void plat_toggle_maximize(void) {}
void plat_request_close(void) { if (app_on_close()) ANativeActivity_finish(g_app->activity); }
void plat_quit(void) { app_suspend(); ANativeActivity_finish(g_app->activity); }
void plat_set_window_mode(int m) { (void)m; }
void plat_set_title(const char *t) { (void)t; }
void plat_set_cursor(int c) { (void)c; }
void plat_set_relative_mouse(bool on) { (void)on; }
void plat_set_vsync(bool on) { if (g_dpy != EGL_NO_DISPLAY) eglSwapInterval(g_dpy, on ? 1 : 0); }
double plat_time(void) { return posix_now() - g_t0; }
void plat_set_app_icon(const uint8_t *r, int w, int h) { (void)r; (void)w; (void)h; }

uint8_t *plat_load_image(const char *path, int *w, int *h) {
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *d = malloc((size_t)n);
    size_t got = fread(d, 1, (size_t)n, f);
    fclose(f);
    uint8_t *img = image_decode(d, got, w, h);
    free(d);
    return img;
}

/* fonts ship in the APK under assets/fonts; keep each file in memory once loaded */
static struct { char name[48]; uint8_t *data; size_t size; } g_fonts[8];
bool plat_font_raster(const char *face, float px, int weight, const uint32_t *cps, int ncp, FontRaster *out) {
    const char *file = font_file_for(face, weight);
    int slot = -1;
    for (int i = 0; i < 8; i++) if (g_fonts[i].data && !strcmp(g_fonts[i].name, file)) slot = i;
    if (slot < 0) {
        for (int i = 0; i < 8 && slot < 0; i++) if (!g_fonts[i].data) slot = i;
        if (slot < 0) return false;
        char path[80];
        snprintf(path, sizeof path, "fonts/%s", file);
        AAsset *a = AAssetManager_open(g_app->activity->assetManager, path, AASSET_MODE_BUFFER);
        if (!a) { LOGE("missing font asset %s", path); return false; }
        size_t n = (size_t)AAsset_getLength(a);
        g_fonts[slot].data = malloc(n);
        memcpy(g_fonts[slot].data, AAsset_getBuffer(a), n);
        g_fonts[slot].size = n;
        sf_strcpy(g_fonts[slot].name, sizeof g_fonts[slot].name, file);
        AAsset_close(a);
    }
    return font_raster_ttf(g_fonts[slot].data, g_fonts[slot].size, px, cps, ncp, out);
}

/* ---------------------------------------------------------------- EGL */
static bool egl_init_display(void) {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(g_dpy, NULL, NULL)) return false;
    EGLint base[] = { EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
                      EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4, EGL_NONE };
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, base, &g_cfg, 1, &n) || n < 1) {
        base[18] = 0; base[20] = 0;   /* no MSAA */
        if (!eglChooseConfig(g_dpy, base, &g_cfg, 1, &n) || n < 1) return false;
    }
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ca);
    return g_ctx != EGL_NO_CONTEXT;
}

static bool egl_make_surface(void) {
    if (!g_app->window) return false;
    g_surf = eglCreateWindowSurface(g_dpy, g_cfg, g_app->window, NULL);
    if (g_surf == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed %x", eglGetError()); return false; }
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) { LOGE("eglMakeCurrent failed %x", eglGetError()); return false; }
    eglSwapInterval(g_dpy, 1);
    return true;
}

static void egl_drop_surface(void) {
    if (g_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx);
    if (g_surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surf);
    g_surf = EGL_NO_SURFACE;
}

static void read_density(void) {
    AConfiguration *cfg = AConfiguration_new();
    AConfiguration_fromAssetManager(cfg, g_app->activity->assetManager);
    int d = AConfiguration_getDensity(cfg);
    AConfiguration_delete(cfg);
    if (d > 0 && d != ACONFIGURATION_DENSITY_ANY && d != ACONFIGURATION_DENSITY_NONE) g_density = d / 160.0f;
}

/* ---------------------------------------------------------------- input */
static TouchPt *touch_find(int id) {
    for (int i = 0; i < in.ntouch; i++) if (in.touches[i].id == id && !in.touches[i].released) return &in.touches[i];
    return NULL;
}
static void touch_down(int id, float x, float y) {
    if (in.ntouch >= 10) return;
    TouchPt *t = &in.touches[in.ntouch++];
    memset(t, 0, sizeof *t);
    t->id = id; t->x = t->sx = x; t->y = t->sy = y; t->t0 = plat_time(); t->pressed = true;
}

static int map_key(int k) {
    if (k >= AKEYCODE_A && k <= AKEYCODE_Z) return 'A' + (k - AKEYCODE_A);
    if (k >= AKEYCODE_0 && k <= AKEYCODE_9) return '0' + (k - AKEYCODE_0);
    if (k >= AKEYCODE_F1 && k <= AKEYCODE_F12) return KEY_F1 + (k - AKEYCODE_F1);
    switch (k) {
    case AKEYCODE_DEL: return KEY_BACK;
    case AKEYCODE_FORWARD_DEL: return KEY_DELETE;
    case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return KEY_ENTER;
    case AKEYCODE_ESCAPE: return KEY_ESC;
    case AKEYCODE_SPACE: return KEY_SPACE;
    case AKEYCODE_TAB: return KEY_TAB;
    case AKEYCODE_DPAD_UP: return KEY_UP;
    case AKEYCODE_DPAD_DOWN: return KEY_DOWN;
    case AKEYCODE_DPAD_LEFT: return KEY_LEFT;
    case AKEYCODE_DPAD_RIGHT: return KEY_RIGHT;
    case AKEYCODE_SHIFT_LEFT: case AKEYCODE_SHIFT_RIGHT: return KEY_SHIFT;
    case AKEYCODE_CTRL_LEFT: case AKEYCODE_CTRL_RIGHT: return KEY_CTRL;
    case AKEYCODE_ALT_LEFT: case AKEYCODE_ALT_RIGHT: return KEY_ALT;
    case AKEYCODE_PAGE_UP: return KEY_PGUP;
    case AKEYCODE_PAGE_DOWN: return KEY_PGDN;
    case AKEYCODE_MOVE_HOME: return KEY_HOME;
    case AKEYCODE_MOVE_END: return KEY_END;
    case AKEYCODE_INSERT: return KEY_INSERT;
    case AKEYCODE_MINUS: return KEY_MINUS;
    case AKEYCODE_EQUALS: case AKEYCODE_PLUS: return KEY_PLUS;
    case AKEYCODE_COMMA: return KEY_COMMA;
    case AKEYCODE_PERIOD: return KEY_PERIOD;
    case AKEYCODE_LEFT_BRACKET: return KEY_LBRACKET;
    case AKEYCODE_RIGHT_BRACKET: return KEY_RBRACKET;
    default: return 0;
    }
}

/* gamepad: buttons become virtual play buttons, sticks feed the virtual stick / look */
static struct { float lx, ly, rx, ry, rt; bool a, b, x, r1, start; bool a_edge, x_edge; } g_pad;

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    (void)app;
    int type = AInputEvent_getType(ev);
    int src = AInputEvent_getSource(ev);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int code = AKeyEvent_getKeyCode(ev), act = AKeyEvent_getAction(ev);
        bool dn = act == AKEY_EVENT_ACTION_DOWN;
        if (code == AKEYCODE_BACK) { if (dn && AKeyEvent_getRepeatCount(ev) == 0) in.back = true; return 1; }
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN || code == AKEYCODE_VOLUME_MUTE) return 0;
        switch (code) {   /* gamepad buttons */
        case AKEYCODE_BUTTON_A: if (dn && !g_pad.a) g_pad.a_edge = true; g_pad.a = dn; return 1;
        case AKEYCODE_BUTTON_B: g_pad.b = dn; return 1;
        case AKEYCODE_BUTTON_X: if (dn && !g_pad.x) g_pad.x_edge = true; g_pad.x = dn; return 1;
        case AKEYCODE_BUTTON_R1: case AKEYCODE_BUTTON_R2: g_pad.r1 = dn; return 1;
        case AKEYCODE_BUTTON_START: if (dn) in.back = true; return 1;
        default: break;
        }
        int k = map_key(code);
        if (!k) return 0;
        if (dn) { if (!in.kdown[k]) in.kpressed[k] = true; in.kdown[k] = true; }
        else if (act == AKEY_EVENT_ACTION_UP) { in.kdown[k] = false; in.kreleased[k] = true; }
        int meta = AKeyEvent_getMetaState(ev);
        in.ctrl = (meta & AMETA_CTRL_ON) != 0;
        in.shift = (meta & AMETA_SHIFT_ON) != 0;
        in.alt = (meta & AMETA_ALT_ON) != 0;
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(ev);
    int am = action & AMOTION_EVENT_ACTION_MASK;
    if ((src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
        g_pad.lx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
        g_pad.ly = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0);
        g_pad.rx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Z, 0);
        g_pad.ry = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RZ, 0);
        g_pad.rt = SF_MAX(AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RTRIGGER, 0), AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_GAS, 0));
        return 1;
    }
    float x = AMotionEvent_getX(ev, 0) / g_density, y = AMotionEvent_getY(ev, 0) / g_density;
    int tool = AMotionEvent_getToolType(ev, 0);
    if (tool == AMOTION_EVENT_TOOL_TYPE_MOUSE) {
        /* DeX / USB mouse: drive the desktop mouse model directly */
        in.touch = false;
        in.mdx += x - in.mx; in.mdy += y - in.my;
        in.mx = x; in.my = y;
        if (am == AMOTION_EVENT_ACTION_SCROLL) { in.wheel += AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_VSCROLL, 0); return 1; }
        int bs = AMotionEvent_getButtonState(ev);
        bool b[3] = { (bs & AMOTION_EVENT_BUTTON_PRIMARY) != 0, (bs & AMOTION_EVENT_BUTTON_SECONDARY) != 0, (bs & AMOTION_EVENT_BUTTON_TERTIARY) != 0 };
        for (int i = 0; i < 3; i++) {
            if (b[i] && !in.mdown[i]) in.mpressed[i] = true;
            if (!b[i] && in.mdown[i]) in.mreleased[i] = true;
            in.mdown[i] = b[i];
        }
        return 1;
    }
    in.touch = true;
    int idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    switch (am) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN:
        touch_down(AMotionEvent_getPointerId(ev, (size_t)idx), AMotionEvent_getX(ev, (size_t)idx) / g_density, AMotionEvent_getY(ev, (size_t)idx) / g_density);
        break;
    case AMOTION_EVENT_ACTION_MOVE: {
        size_t n = AMotionEvent_getPointerCount(ev);
        for (size_t i = 0; i < n; i++) {
            TouchPt *t = touch_find(AMotionEvent_getPointerId(ev, i));
            if (t) { t->x = AMotionEvent_getX(ev, i) / g_density; t->y = AMotionEvent_getY(ev, i) / g_density; }
        }
    } break;
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP: {
        TouchPt *t = touch_find(AMotionEvent_getPointerId(ev, (size_t)idx));
        if (t) { t->x = AMotionEvent_getX(ev, (size_t)idx) / g_density; t->y = AMotionEvent_getY(ev, (size_t)idx) / g_density; t->released = true; }
    } break;
    case AMOTION_EVENT_ACTION_CANCEL:
        for (int i = 0; i < in.ntouch; i++) in.touches[i].released = true;
        break;
    default: break;
    }
    return 1;
}

static void end_frame_input(void) {
    int k = 0;
    for (int i = 0; i < in.ntouch; i++) {
        if (in.touches[i].released) continue;
        in.touches[i].pressed = false;
        in.touches[k++] = in.touches[i];
    }
    in.ntouch = k;
    memset(in.kpressed, 0, sizeof in.kpressed);
    memset(in.kreleased, 0, sizeof in.kreleased);
    memset(in.mpressed, 0, sizeof in.mpressed);
    memset(in.mreleased, 0, sizeof in.mreleased);
    memset(in.mdouble, 0, sizeof in.mdouble);
    in.wheel = 0;
    in.mdx = in.mdy = 0;
    in.back = false;
    g_pad.a_edge = g_pad.x_edge = false;
}

static float dz(float v) { return fabsf(v) < 0.15f ? 0 : (v - (v > 0 ? 0.15f : -0.15f)) / 0.85f; }

static void frame(void) {
    if (!g_app_ready || g_surf == EGL_NO_SURFACE) return;
    EGLint w = 0, h = 0;
    eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &w);
    eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    sfgl_set_surface_size(w, h);
    double now = plat_time();
    in.dt = SF_CLAMP(now - in.time, 0.0, 0.1);
    in.time = now;
    in.scale = g_density;
    in.win_w = w / g_density;
    in.win_h = h / g_density;
    in.focused = g_focused;
    pthread_mutex_lock(&g_mx);
    in.inset_l = g_ins[0] / g_density; in.inset_t = g_ins[1] / g_density;
    in.inset_r = g_ins[2] / g_density; in.inset_b = g_ins[3] / g_density;
    pthread_mutex_unlock(&g_mx);
    /* gamepad */
    in.vmove_x = dz(g_pad.lx); in.vmove_y = -dz(g_pad.ly);
    in.vlook_x = dz(g_pad.rx) * 900.0f * (float)in.dt; in.vlook_y = dz(g_pad.ry) * 600.0f * (float)in.dt;
    in.vfire = g_pad.r1 || g_pad.rt > 0.4f;
    in.vcrouch = g_pad.b;
    in.vjump = g_pad.a_edge;
    in.vuse = g_pad.x_edge;
    app_frame(&in);
    sfgl_flush();
    if (!eglSwapBuffers(g_dpy, g_surf)) {
        EGLint e = eglGetError();
        LOGE("eglSwapBuffers failed %x", e);
        if (e == EGL_BAD_SURFACE || e == EGL_BAD_NATIVE_WINDOW) { egl_drop_surface(); egl_make_surface(); }
    }
    end_frame_input();
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (g_dpy == EGL_NO_DISPLAY && !egl_init_display()) { LOGE("EGL init failed"); ANativeActivity_finish(app->activity); return; }
        if (!egl_make_surface()) return;
        if (!g_app_ready) {
            sfgl_init();
            read_density();
            in.time = plat_time();
            g_app_ready = app_init();
        }
        break;
    case APP_CMD_TERM_WINDOW: egl_drop_surface(); break;
    case APP_CMD_GAINED_FOCUS: g_focused = true; break;
    case APP_CMD_LOST_FOCUS: g_focused = false; break;
    case APP_CMD_PAUSE: g_paused = true; if (g_app_ready) app_suspend(); break;
    case APP_CMD_RESUME: g_paused = false; in.time = plat_time(); break;
    case APP_CMD_SAVE_STATE: if (g_app_ready) app_suspend(); break;
    case APP_CMD_CONFIG_CHANGED: read_density(); break;
    case APP_CMD_DESTROY: if (g_app_ready) app_shutdown(); break;
    default: break;
    }
}

void android_main(struct android_app *app) {
    g_app = app;
    g_t0 = posix_now();
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    const char *ext = app->activity->externalDataPath, *intl = app->activity->internalDataPath;
    posix_set_dirs(ext && ext[0] ? ext : intl, intl);
    plat_mkdirs(ext && ext[0] ? ext : intl);
    plat_mkdirs(intl);
    LOGI("Sector Forge starting, data %s", ext ? ext : intl);
    for (;;) {
        int events;
        struct android_poll_source *src;
        int timeout = (g_app_ready && g_surf != EGL_NO_SURFACE && !g_paused) ? 0 : -1;
        for (;;) {
            int id = ALooper_pollOnce(timeout, NULL, &events, (void **)&src);
            if (id == ALOOPER_POLL_TIMEOUT || id == ALOOPER_POLL_ERROR) break;
            if (id >= 0 && src) src->process(app, src);
            if (app->destroyRequested) {
                egl_drop_surface();
                if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
                if (g_dpy != EGL_NO_DISPLAY) eglTerminate(g_dpy);
                g_ctx = EGL_NO_CONTEXT; g_dpy = EGL_NO_DISPLAY;
                return;
            }
            timeout = (g_app_ready && g_surf != EGL_NO_SURFACE && !g_paused) ? 0 : -1;
        }
        frame();
    }
}
