/* scr.c — nightfall.scr: the whole Windows frontend of the screensaver.
 *
 * One raster as wide as the virtual desktop; a borderless GDI window per monitor shows its slice.
 * It does not touch the terminal code: it #includes src/gallery.c as it is to reach the static scene
 * composer (build_scene, director), so changes there are picked up automatically.
 *
 *   /s (or none)  run      /c  settings (none)      /p <hwnd>  preview pane (not drawn)
 *
 * Any key, mouse click or mouse movement quits; so does switching to another application. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef MAX_PATH                         /* world.h has its own */
#include "../src/gallery.c"
#include "../src/gen.h"
#include <time.h>

#define MAX_MON 16
#define SCENE_SECONDS 600.0f

static struct { HWND hwnd; RECT rc; } g_mon[MAX_MON];
static int g_nmon, g_quit, g_scale = 4;
static int g_vx, g_vy, g_vw, g_vh;                  /* virtual desktop, screen pixels */
static raster_t g_r;
static uint32_t *g_bits;                            /* BGRX copy of the raster */
static BITMAPINFO g_bmi;
static DWORD g_t0;
static POINT g_home;

static BOOL CALLBACK mon_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp)
{
    if (g_nmon < MAX_MON) g_mon[g_nmon++].rc = *rc;
    return TRUE;
}

static void draw_mon(int i)
{
    if (!g_bits) return;
    const RECT *m = &g_mon[i].rc;
    HDC dc = GetDC(g_mon[i].hwnd);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, m->right - m->left, m->bottom - m->top,
                  (m->left - g_vx) / g_scale, (m->top - g_vy) / g_scale,
                  (m->right - m->left) / g_scale, (m->bottom - m->top) / g_scale,
                  g_bits, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_mon[i].hwnd, dc);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int armed = GetTickCount() - g_t0 > 800;        /* ignore the input noise while the windows appear */
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL:
        if (armed) g_quit = 1;
        return 0;
    case WM_MOUSEMOVE: {
        POINT p;
        GetCursorPos(&p);
        if (armed && (abs(p.x - g_home.x) > 8 || abs(p.y - g_home.y) > 8)) g_quit = 1;
        return 0;
    }
    case WM_ACTIVATEAPP:
        if (!wp && armed) g_quit = 1;
        return 0;
    case WM_SETCURSOR: SetCursor(NULL); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        for (int i = 0; i < g_nmon; i++) if (g_mon[i].hwnd == h) draw_mon(i);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER) return 0;
        break;
    case WM_CLOSE: g_quit = 1; return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void present(void)
{
    for (int i = 0; i < g_r.w * g_r.h; i++) {
        rgb_t c = g_r.c[i];
        g_bits[i] = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
    }
    for (int i = 0; i < g_nmon; i++) draw_mon(i);
}

/* gallery.c calls this (main.c has it in the terminal build) */
int nf_load_library(library_t *lib, const char *assets_dir, uint64_t seed)
{
    lib_init(lib);
    gen_register_all(lib, seed);
    if (assets_dir) return lib_load_dir(lib, assets_dir);
    return lib_load_embedded(lib);
}

static void new_scene(world_t *w, library_t *lib, uint64_t seed)
{
    world_init(w, lib, g_r.w, g_r.h, seed);
    build_scene(w, seed);
    for (int i = 0; i < 3; i++) director(w, 100.0f);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char mode = 's';
    for (; *cmd; cmd++)
        if (*cmd == '/' || *cmd == '-') { mode = (char)(cmd[1] | 0x20); break; }
    if (mode == 'p') return 0;
    if (mode == 'c') {
        MessageBoxA(NULL, "Nightfall has no settings.", "Nightfall", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    HANDLE once = CreateMutexA(NULL, TRUE, "Local\\NightfallScreensaver");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDPIAware();
    EnumDisplayMonitors(NULL, NULL, mon_cb, 0);
    if (g_nmon == 0) return 1;
    g_vx = g_mon[0].rc.left; g_vy = g_mon[0].rc.top;
    int vr = g_mon[0].rc.right, vb = g_mon[0].rc.bottom;
    for (int i = 1; i < g_nmon; i++) {
        if (g_mon[i].rc.left < g_vx) g_vx = g_mon[i].rc.left;
        if (g_mon[i].rc.top < g_vy) g_vy = g_mon[i].rc.top;
        if (g_mon[i].rc.right > vr) vr = g_mon[i].rc.right;
        if (g_mon[i].rc.bottom > vb) vb = g_mon[i].rc.bottom;
    }
    g_vw = vr - g_vx; g_vh = vb - g_vy;
    /* art pixel size: about 270 art rows on the primary screen; the world is NF_MAX_W wide at most */
    g_scale = (GetSystemMetrics(SM_CYSCREEN) + 135) / 270;
    if (g_scale < 1) g_scale = 1;
    while (g_vw / g_scale > NF_MAX_W) g_scale++;

    library_t lib;
    uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)GetTickCount() << 20);
    nf_load_library(&lib, NULL, seed);

    raster_init(&g_r, g_vw / g_scale, g_vh / g_scale);
    g_r.fog = mkrgb(6, 12, 14);
    g_r.fog_near = 100;
    g_r.fog_far = 40;
    g_r.fog_max = 0.8f;
    g_bits = malloc((size_t)g_r.w * g_r.h * 4);
    world_t *w = malloc(sizeof *w);
    if (!g_bits || !w) return 1;
    memset(&g_bmi, 0, sizeof g_bmi);
    g_bmi.bmiHeader.biSize = sizeof g_bmi.bmiHeader;
    g_bmi.bmiHeader.biWidth = g_r.w;
    g_bmi.bmiHeader.biHeight = -g_r.h;              /* top-down */
    g_bmi.bmiHeader.biPlanes = 1;
    g_bmi.bmiHeader.biBitCount = 32;
    g_bmi.bmiHeader.biCompression = BI_RGB;
    new_scene(w, &lib, seed);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = "NightfallSaver";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);
    g_t0 = GetTickCount();
    GetCursorPos(&g_home);
    for (int i = 0; i < g_nmon; i++) {
        const RECT *m = &g_mon[i].rc;
        g_mon[i].hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, "Nightfall", WS_POPUP,
                                        m->left, m->top, m->right - m->left, m->bottom - m->top,
                                        NULL, NULL, inst, NULL);
        ShowWindow(g_mon[i].hwnd, SW_SHOW);
    }
    SetForegroundWindow(g_mon[0].hwnd);
    SetFocus(g_mon[0].hwnd);
    ShowCursor(FALSE);

    float age = 0;
    DWORD last = GetTickCount();
    while (!g_quit) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (g_quit) break;
        DWORD now = GetTickCount();
        float dt = (float)(now - last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;
        last = now;
        age += dt;
        if (age > SCENE_SECONDS) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            world_free(w);
            new_scene(w, &lib, seed);
            age = 0;
        }
        director(w, dt);
        world_update(w, dt);
        world_render(w, &g_r, 0);
        present();
        DWORD spent = GetTickCount() - now;
        MsgWaitForMultipleObjects(0, NULL, FALSE, spent < 83 ? 83 - spent : 1, QS_ALLINPUT);
    }
    ShowCursor(TRUE);
    for (int i = 0; i < g_nmon; i++) DestroyWindow(g_mon[i].hwnd);
    world_free(w);
    free(w);
    raster_free(&g_r);
    lib_free(&lib);
    CloseHandle(once);
    return 0;
}
