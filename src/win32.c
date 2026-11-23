/* Sector Forge - Win32 platform layer: borderless window, WGL, input, files, fonts (GDI), images (WIC) */
#define COBJMACROS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include "glinc.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <wincodec.h>
#include <dwmapi.h>
#include "plat.h"

/* ------------------------------------------------------------------ state */
static HWND g_hwnd;
static HDC g_hdc;
static HGLRC g_glrc;
static HINSTANCE g_inst;
static bool g_running = true;
static int g_mode = WINMODE_SPLASH;
static float g_scale = 1;
static Input g_in;
static HCURSOR g_cursors[CUR_NONE + 1];
static int g_cursor = CUR_ARROW;
static bool g_relative;
static POINT g_rel_restore;
static LARGE_INTEGER g_freq, g_t0;
static double g_last;
static bool g_in_sizemove;
static int g_swap_interval = 1;
typedef BOOL(WINAPI *PFN_SWAPINTERVAL)(int);
static PFN_SWAPINTERVAL p_wglSwapIntervalEXT;
static HICON g_icon_big, g_icon_small;

/* ------------------------------------------------------------------ utf8 <-> wide */
static void to_w(const char *s, wchar_t *out, int cap) {
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap)) out[0] = 0;
}
static void to_u8(const wchar_t *s, char *out, int cap) {
    if (!WideCharToMultiByte(CP_UTF8, 0, s, -1, out, cap, NULL, NULL)) out[0] = 0;
}

double plat_time(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart;
}

/* ------------------------------------------------------------------ files */
FILE *plat_fopen(const char *path, const char *mode) {
    wchar_t wp[1024], wm[16];
    to_w(path, wp, 1024);
    to_w(mode, wm, 16);
    return _wfopen(wp, wm);
}

bool plat_exists(const char *path) {
    wchar_t wp[1024];
    to_w(path, wp, 1024);
    return GetFileAttributesW(wp) != INVALID_FILE_ATTRIBUTES;
}

bool plat_is_dir(const char *path) {
    wchar_t wp[1024];
    to_w(path, wp, 1024);
    DWORD a = GetFileAttributesW(wp);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int64_t ft_to_unix(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

int64_t plat_mtime(const char *path) {
    wchar_t wp[1024];
    to_w(path, wp, 1024);
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(wp, GetFileExInfoStandard, &d)) return 0;
    return ft_to_unix(d.ftLastWriteTime);
}

bool plat_mkdirs(const char *path) {
    wchar_t wp[1024];
    to_w(path, wp, 1024);
    int r = SHCreateDirectoryExW(NULL, wp, NULL);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

int plat_list_dir(const char *dir, PlatDirEnt **out) {
    wchar_t wp[1100];
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    to_w(pat, wp, 1100);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wp, &fd);
    *out = NULL;
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0, cap = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        *out = sf_grow(*out, &cap, n + 1, sizeof(PlatDirEnt));
        PlatDirEnt *e = &(*out)[n++];
        to_u8(fd.cFileName, e->name, sizeof e->name);
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->mtime = ft_to_unix(fd.ftLastWriteTime);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static bool shell_op(UINT func, const char *from, const char *to, FILEOP_FLAGS flags) {
    static wchar_t wf[1100], wt[1100];
    memset(wf, 0, sizeof wf);
    memset(wt, 0, sizeof wt);
    to_w(from, wf, 1090);
    if (to) to_w(to, wt, 1090);
    SHFILEOPSTRUCTW op;
    memset(&op, 0, sizeof op);
    op.hwnd = g_hwnd;
    op.wFunc = func;
    op.pFrom = wf;
    op.pTo = to ? wt : NULL;
    op.fFlags = flags;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool plat_recycle(const char *path) { return shell_op(FO_DELETE, path, NULL, FOF_ALLOWUNDO | FOF_NOCONFIRMATION); }
bool plat_copy_dir(const char *src, const char *dst) { return shell_op(FO_COPY, src, dst, FOF_NOCONFIRMMKDIR); }

bool plat_copy_file(const char *src, const char *dst) {
    wchar_t a[1024], b[1024];
    to_w(src, a, 1024);
    to_w(dst, b, 1024);
    return CopyFileW(a, b, FALSE) != 0;
}

bool plat_delete_file(const char *path) {
    wchar_t a[1024];
    to_w(path, a, 1024);
    return DeleteFileW(a) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
}

void plat_documents_dir(char *out, size_t cap) {
    wchar_t w[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, w) == S_OK) to_u8(w, out, (int)cap);
    else sf_strcpy(out, cap, ".");
}

void plat_appdata_dir(char *out, size_t cap) {
    wchar_t w[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, w) == S_OK) to_u8(w, out, (int)cap);
    else sf_strcpy(out, cap, ".");
}

void plat_open_folder(const char *path) {
    wchar_t w[1024];
    to_w(path, w, 1024);
    ShellExecuteW(g_hwnd, L"open", w, NULL, NULL, SW_SHOWNORMAL);
}

bool plat_open_file_dialog(char *out, size_t cap, const char *title, const char *filter_name, const char *patterns) {
    wchar_t file[1024] = { 0 }, wt[256], filt[512];
    to_w(title, wt, 256);
    /* "Name\0patterns\0All files\0*.*\0\0" */
    wchar_t wn[200], wpat[200];
    to_w(filter_name, wn, 200);
    to_w(patterns, wpat, 200);
    int k = 0;
    for (int i = 0; wn[i] && k < 500; i++) filt[k++] = wn[i];
    filt[k++] = 0;
    for (int i = 0; wpat[i] && k < 500; i++) filt[k++] = wpat[i];
    filt[k++] = 0;
    const wchar_t *all = L"All files";
    for (int i = 0; all[i]; i++) filt[k++] = all[i];
    filt[k++] = 0;
    filt[k++] = L'*'; filt[k++] = L'.'; filt[k++] = L'*'; filt[k++] = 0;
    filt[k++] = 0;
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = filt;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.lpstrTitle = wt;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    bool ok = GetOpenFileNameW(&ofn) != 0;
    if (ok) to_u8(file, out, (int)cap);
    memset(&g_in.kdown, 0, sizeof g_in.kdown);
    memset(&g_in.mdown, 0, sizeof g_in.mdown);
    return ok;
}

bool plat_pick_folder(char *out, size_t cap, const char *title) {
    wchar_t wt[256], path[MAX_PATH];
    to_w(title, wt, 256);
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = wt;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    bool ok = false;
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) { to_u8(path, out, (int)cap); ok = true; }
        CoTaskMemFree(pidl);
    }
    memset(&g_in.kdown, 0, sizeof g_in.kdown);
    memset(&g_in.mdown, 0, sizeof g_in.mdown);
    return ok;
}

/* ------------------------------------------------------------------ images via WIC */
uint8_t *plat_load_image(const char *path, int *w, int *h) {
    wchar_t wp[1024];
    to_w(path, wp, 1024);
    IWICImagingFactory *fac = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapSource *conv = NULL;
    uint8_t *px = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&fac))) return NULL;
    if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(fac, wp, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) &&
        SUCCEEDED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppRGBA, (IWICBitmapSource *)frame, &conv))) {
        UINT iw = 0, ih = 0;
        IWICBitmapSource_GetSize(conv, &iw, &ih);
        if (iw > 0 && ih > 0 && iw <= 8192 && ih <= 8192) {
            px = malloc((size_t)iw * ih * 4);
            if (FAILED(IWICBitmapSource_CopyPixels(conv, NULL, iw * 4, iw * ih * 4, px))) { free(px); px = NULL; }
            else { *w = (int)iw; *h = (int)ih; }
        }
    }
    if (conv) IWICBitmapSource_Release(conv);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(fac);
    return px;
}

/* ------------------------------------------------------------------ fonts via GDI */
bool plat_font_raster(const char *face, float px, int weight, const uint32_t *cps, int ncp, FontRaster *out) {
    wchar_t wface[64];
    to_w(face, wface, 64);
    HDC dc = CreateCompatibleDC(NULL);
    HFONT font = CreateFontW(-(int)(px + 0.5f), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wface);
    if (!font) { DeleteDC(dc); return false; }
    HGDIOBJ oldf = SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int cell_h = tm.tmHeight + 2;
    /* measure */
    int *gw = malloc(sizeof(int) * (size_t)ncp), *ga = malloc(sizeof(int) * (size_t)ncp), *gadv = malloc(sizeof(int) * (size_t)ncp);
    for (int i = 0; i < ncp; i++) {
        ABC abc;
        WCHAR wc = (WCHAR)cps[i];
        if (GetCharABCWidthsW(dc, wc, wc, &abc)) { ga[i] = abc.abcA; gw[i] = (int)abc.abcB; gadv[i] = abc.abcA + (int)abc.abcB + abc.abcC; }
        else { SIZE sz; GetTextExtentPoint32W(dc, &wc, 1, &sz); ga[i] = 0; gw[i] = sz.cx; gadv[i] = sz.cx; }
        if (cps[i] == ' ') gw[i] = 0;
    }
    int W = 256, H = 0;
    for (;;) {
        int x = 1, y = 1;
        for (int i = 0; i < ncp; i++) {
            int cw = gw[i] + 4;
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
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ oldb = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)W * H * 4);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    out->glyphs = calloc((size_t)ncp, sizeof(GlyphInfo));
    out->nglyphs = ncp;
    int x = 1, y = 1;
    for (int i = 0; i < ncp; i++) {
        int cw = gw[i] + 4;
        if (x + cw > W) { x = 1; y += cell_h + 2; }
        WCHAR wc = (WCHAR)cps[i];
        if (gw[i] > 0) TextOutW(dc, x + 2 - ga[i], y, &wc, 1);
        GlyphInfo *g = &out->glyphs[i];
        g->cp = cps[i];
        g->x = x; g->y = y; g->w = gw[i] > 0 ? gw[i] + 4 : 0; g->h = cell_h;
        g->xoff = (float)(ga[i] - 2); g->yoff = 0; g->adv = (float)gadv[i];
        x += cw;
    }
    GdiFlush();
    out->w = W; out->h = H;
    out->alpha = malloc((size_t)W * H);
    const uint8_t *src = bits;
    for (int i = 0; i < W * H; i++) {
        int v = SF_MAX(src[i * 4], SF_MAX(src[i * 4 + 1], src[i * 4 + 2]));
        out->alpha[i] = (uint8_t)v;
    }
    out->ascent = (float)tm.tmAscent;
    out->height = (float)tm.tmHeight;
    SelectObject(dc, oldb);
    SelectObject(dc, oldf);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
    free(gw); free(ga); free(gadv);
    return true;
}

void plat_set_app_icon(const uint8_t *rgba, int w, int h) {
    if (g_icon_small) return; /* the embedded multi-size resource icon is already in use */
    BITMAPV5HEADER bh;
    memset(&bh, 0, sizeof bh);
    bh.bV5Size = sizeof bh;
    bh.bV5Width = w;
    bh.bV5Height = -h;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    HDC dc = GetDC(NULL);
    void *bits = NULL;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bh, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color) return;
    uint8_t *d = bits;
    for (int i = 0; i < w * h; i++) { d[i * 4] = rgba[i * 4 + 2]; d[i * 4 + 1] = rgba[i * 4 + 1]; d[i * 4 + 2] = rgba[i * 4]; d[i * 4 + 3] = rgba[i * 4 + 3]; }
    HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii;
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!ic) return;
    if (g_icon_big) DestroyIcon(g_icon_big);
    g_icon_big = ic;
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)ic);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ic);
}

/* ------------------------------------------------------------------ window control */
static float dpi_of(HWND h) {
    UINT d = h ? GetDpiForWindow(h) : 0;
    return d ? d / 96.0f : 1.0f;
}

static void center_window(int w, int h) {
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    RECT wa = mi.rcWork;
    int x = wa.left + ((wa.right - wa.left) - w) / 2, y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    SetWindowPos(g_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void plat_set_window_mode(int mode) {
    g_mode = mode;
    if (mode == WINMODE_SPLASH) {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX | WS_SYSMENU);
        center_window((int)(720 * g_scale), (int)(450 * g_scale));
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(mon, &mi);
        int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
        int w = SF_MIN((int)(1440 * g_scale), (int)(aw * 0.9f)), h = SF_MIN((int)(900 * g_scale), (int)(ah * 0.9f));
        center_window(w, h);
    }
}

void plat_minimize(void) { ShowWindow(g_hwnd, SW_MINIMIZE); }
void plat_toggle_maximize(void) { ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); }
void plat_request_close(void) { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
void plat_quit(void) { g_running = false; DestroyWindow(g_hwnd); }
void plat_set_cursor(int c) {
    if (c == g_cursor) return;
    g_cursor = c;
    POINT p;
    GetCursorPos(&p);
    if (WindowFromPoint(p) == g_hwnd && !g_relative) SetCursor(c == CUR_NONE ? NULL : g_cursors[c]);
}
void plat_set_title(const char *t) { wchar_t w[256]; to_w(t, w, 256); SetWindowTextW(g_hwnd, w); }
void plat_set_vsync(bool on) { g_swap_interval = on ? 1 : 0; if (p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(g_swap_interval); }

void plat_set_relative_mouse(bool on) {
    if (on == g_relative) return;
    g_relative = on;
    if (on) {
        GetCursorPos(&g_rel_restore);
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
        ClientToScreen(g_hwnd, &c);
        RECT clip = { c.x, c.y, c.x + 1, c.y + 1 };
        ClipCursor(&clip);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(NULL);
        SetCursorPos(g_rel_restore.x, g_rel_restore.y);
        while (ShowCursor(TRUE) < 0) {}
    }
}

/* ------------------------------------------------------------------ GL setup */
typedef BOOL(WINAPI *PFN_CHOOSEPF)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

static int choose_msaa_format(void) {
    /* dummy window to query WGL_ARB_pixel_format */
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_inst;
    wc.lpszClassName = L"SFDummy";
    RegisterClassW(&wc);
    HWND dw = CreateWindowW(L"SFDummy", L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, NULL, NULL, g_inst, NULL);
    if (!dw) return 0;
    HDC ddc = GetDC(dw);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(ddc, &pfd);
    SetPixelFormat(ddc, pf, &pfd);
    HGLRC rc = wglCreateContext(ddc);
    wglMakeCurrent(ddc, rc);
    PFN_CHOOSEPF choose = (PFN_CHOOSEPF)(void *)wglGetProcAddress("wglChoosePixelFormatARB");
    int result = 0;
    if (choose) {
        const int attr[] = {
            0x2001 /*DRAW_TO_WINDOW*/, 1, 0x2010 /*SUPPORT_OPENGL*/, 1, 0x2011 /*DOUBLE_BUFFER*/, 1,
            0x2013 /*PIXEL_TYPE*/, 0x202B /*TYPE_RGBA*/, 0x2014 /*COLOR_BITS*/, 32, 0x2022 /*DEPTH_BITS*/, 24,
            0x2023 /*STENCIL_BITS*/, 8, 0x2041 /*SAMPLE_BUFFERS*/, 1, 0x2042 /*SAMPLES*/, 4, 0 };
        UINT num = 0;
        int f = 0;
        if (choose(ddc, attr, NULL, 1, &f, &num) && num > 0) result = f;
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(dw, ddc);
    DestroyWindow(dw);
    UnregisterClassW(L"SFDummy", g_inst);
    return result;
}

static bool init_gl(void) {
    int msaa = choose_msaa_format();
    g_hdc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = msaa ? msaa : ChoosePixelFormat(g_hdc, &pfd);
    if (msaa) DescribePixelFormat(g_hdc, pf, sizeof pfd, &pfd);
    if (!SetPixelFormat(g_hdc, pf, &pfd)) {
        pf = ChoosePixelFormat(g_hdc, &pfd);
        if (!SetPixelFormat(g_hdc, pf, &pfd)) return false;
    }
    g_glrc = wglCreateContext(g_hdc);
    if (!g_glrc || !wglMakeCurrent(g_hdc, g_glrc)) return false;
    p_wglSwapIntervalEXT = (PFN_SWAPINTERVAL)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(1);
    if (msaa) glEnable(0x809D /* GL_MULTISAMPLE */);
    return true;
}

/* ------------------------------------------------------------------ frame */
static void update_size(void) {
    RECT r;
    GetClientRect(g_hwnd, &r);
    g_in.win_w = (r.right - r.left) / g_scale;
    g_in.win_h = (r.bottom - r.top) / g_scale;
}

static void do_frame(void) {
    double now = plat_time();
    g_in.dt = SF_CLAMP(now - g_last, 0.0, 0.1);
    g_last = now;
    g_in.time = now;
    g_in.scale = g_scale;
    g_in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    g_in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    g_in.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    g_in.focused = GetForegroundWindow() == g_hwnd;
    g_in.maximized = IsZoomed(g_hwnd) != 0;
    update_size();
    if (!g_relative) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(g_hwnd, &p);
        g_in.mx = p.x / g_scale;
        g_in.my = p.y / g_scale;
    }
    app_frame(&g_in);
    SwapBuffers(g_hdc);
    memset(g_in.kpressed, 0, sizeof g_in.kpressed);
    memset(g_in.kreleased, 0, sizeof g_in.kreleased);
    memset(g_in.mpressed, 0, sizeof g_in.mpressed);
    memset(g_in.mreleased, 0, sizeof g_in.mreleased);
    memset(g_in.mdouble, 0, sizeof g_in.mdouble);
    g_in.nchars = 0;
    g_in.wheel = 0;
    g_in.mdx = g_in.mdy = 0;
}

static void mouse_btn(int b, bool down, bool dbl) {
    if (down) {
        if (!g_in.mdown[b]) g_in.mpressed[b] = true;
        g_in.mdown[b] = true;
        if (dbl) g_in.mdouble[b] = true;
        SetCapture(g_hwnd);
    } else {
        if (g_in.mdown[b]) g_in.mreleased[b] = true;
        g_in.mdown[b] = false;
        if (!g_in.mdown[0] && !g_in.mdown[1] && !g_in.mdown[2]) ReleaseCapture();
    }
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp == TRUE && g_mode == WINMODE_MAIN) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
            if (IsZoomed(h)) {
                /* a maximised borderless window spills over the monitor edges by the frame size */
                int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        return DefWindowProcW(h, msg, wp, -1); /* stop the default frame repaint */
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        RECT r;
        GetClientRect(h, &r);
        float lx = pt.x / g_scale, ly = pt.y / g_scale;
        if (g_mode == WINMODE_MAIN && !IsZoomed(h)) {
            int b = (int)(6 * g_scale);
            bool L = pt.x < b, R = pt.x >= r.right - b, T = pt.y < b, B = pt.y >= r.bottom - b;
            if (T && L) return HTTOPLEFT;
            if (T && R) return HTTOPRIGHT;
            if (B && L) return HTBOTTOMLEFT;
            if (B && R) return HTBOTTOMRIGHT;
            if (L) return HTLEFT;
            if (R) return HTRIGHT;
            if (T) return HTTOP;
            if (B) return HTBOTTOM;
        }
        if (app_hit_test(lx, ly) == 1) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        if (g_mode == WINMODE_MAIN) { mm->ptMinTrackSize.x = (LONG)(980 * g_scale); mm->ptMinTrackSize.y = (LONG)(620 * g_scale); }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(g_relative || g_cursor == CUR_NONE ? NULL : g_cursors[g_cursor]); return TRUE; }
        break;
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0f;
        RECT *nr = (RECT *)lp;
        SetWindowPos(h, NULL, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_ENTERSIZEMOVE: g_in_sizemove = true; SetTimer(h, 1, 15, NULL); return 0;
    case WM_EXITSIZEMOVE: g_in_sizemove = false; KillTimer(h, 1); return 0;
    case WM_TIMER: if (g_in_sizemove) do_frame(); return 0;
    case WM_SIZE: if (g_glrc && g_in_sizemove) do_frame(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: if (app_on_close()) { g_running = false; DestroyWindow(h); } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ACTIVATEAPP:
        if (!wp) {
            memset(g_in.kdown, 0, sizeof g_in.kdown);
            memset(g_in.mdown, 0, sizeof g_in.mdown);
            if (g_relative) { ClipCursor(NULL); while (ShowCursor(TRUE) < 0) {} }
        } else if (g_relative) {
            RECT r;
            GetClientRect(h, &r);
            POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
            ClientToScreen(h, &c);
            RECT clip = { c.x, c.y, c.x + 1, c.y + 1 };
            ClipCursor(&clip);
            while (ShowCursor(FALSE) >= 0) {}
        }
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (wp < 256) { if (!g_in.kdown[wp] || (lp & (1 << 30))) g_in.kpressed[wp] = true; g_in.kdown[wp] = true; }
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (wp < 256) { g_in.kdown[wp] = false; g_in.kreleased[wp] = true; }
        return 0;
    case WM_SYSCHAR: return 0;
    case WM_CHAR:
        if (g_in.nchars < 32 && wp >= 32) g_in.chars[g_in.nchars++] = (uint32_t)wp;
        return 0;
    case WM_LBUTTONDOWN: mouse_btn(0, true, false); return 0;
    case WM_LBUTTONDBLCLK: mouse_btn(0, true, true); return 0;
    case WM_LBUTTONUP: mouse_btn(0, false, false); return 0;
    case WM_RBUTTONDOWN: mouse_btn(1, true, false); return 0;
    case WM_RBUTTONDBLCLK: mouse_btn(1, true, true); return 0;
    case WM_RBUTTONUP: mouse_btn(1, false, false); return 0;
    case WM_MBUTTONDOWN: mouse_btn(2, true, false); return 0;
    case WM_MBUTTONDBLCLK: mouse_btn(2, true, true); return 0;
    case WM_MBUTTONUP: mouse_btn(2, false, false); return 0;
    case WM_MOUSEWHEEL: g_in.wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.0f; return 0;
    case WM_INPUT: {
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE) {
            if (!(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) { g_in.mdx += (float)ri.data.mouse.lLastX; g_in.mdy += (float)ri.data.mouse.lLastY; }
        }
        break;
    }
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* DPI awareness (per-monitor v2) comes from the embedded application manifest. */
static void dwm_setup(void) {
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);
    DWORD corner = 2; /* DWMWCP_ROUND (Windows 11, ignored elsewhere) */
    DwmSetWindowAttribute(g_hwnd, 33, &corner, sizeof corner);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof dark); /* immersive dark mode */
    COLORREF border = RGB(0x2B, 0x2F, 0x37);
    DwmSetWindowAttribute(g_hwnd, 34, &border, sizeof border);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)cmd; (void)show;
    g_inst = inst;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    g_cursors[CUR_ARROW] = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    g_cursors[CUR_HAND] = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
    g_cursors[CUR_CROSS] = LoadCursorW(NULL, (LPCWSTR)IDC_CROSS);
    g_cursors[CUR_IBEAM] = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM);
    g_cursors[CUR_MOVE] = LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL);
    g_cursors[CUR_SIZEWE] = LoadCursorW(NULL, (LPCWSTR)IDC_SIZEWE);
    g_cursors[CUR_SIZENS] = LoadCursorW(NULL, (LPCWSTR)IDC_SIZENS);
    g_cursors[CUR_NONE] = NULL;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = NULL;
    wc.hbrBackground = CreateSolidBrush(RGB(0x0B, 0x0C, 0x0F));
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    g_icon_small = wc.hIconSm;
    wc.lpszClassName = L"SectorForgeWindow";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Sector Forge", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, 720, 450, NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;
    g_scale = dpi_of(g_hwnd);
    dwm_setup();
    plat_set_window_mode(WINMODE_SPLASH);
    if (!init_gl()) {
        MessageBoxW(NULL, L"Could not create an OpenGL context.\nPlease update your graphics drivers.", L"Sector Forge", MB_ICONERROR);
        return 1;
    }
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01; rid.usUsage = 0x02; rid.dwFlags = 0; rid.hwndTarget = g_hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof rid);
    ShowWindow(g_hwnd, SW_SHOW);
    g_last = plat_time();
    update_size();
    if (!app_init()) return 1;
    MSG m;
    while (g_running) {
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) g_running = false;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (!g_running) break;
        if (IsIconic(g_hwnd)) { Sleep(30); continue; }
        float sc = dpi_of(g_hwnd);
        if (fabsf(sc - g_scale) > 0.01f) g_scale = sc;
        do_frame();
        if (GetForegroundWindow() != g_hwnd) Sleep(12);
    }
    app_shutdown();
    if (g_relative) plat_set_relative_mouse(false);
    wglMakeCurrent(NULL, NULL);
    if (g_glrc) wglDeleteContext(g_glrc);
    CoUninitialize();
    return 0;
}
