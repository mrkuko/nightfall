/* scr.c — nightfall.scr: the whole Windows frontend of the screensaver.
 *
 * One scene as wide as the desktop; a borderless GDI window per monitor shows its part, anchored at the
 * bottom of the scene (bottom of the screen = bottom of the scene).
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

typedef struct {
    HWND hwnd;
    RECT rc;                                        /* the monitor, screen pixels */
} mon_t;
static mon_t g_mon[MAX_MON];
static int g_nmon, g_quit;
static int g_vx;                                    /* left edge of the desktop */
static float g_sx = 4, g_sy = 4;                    /* screen pixels per art pixel */
static raster_t g_r;                                /* the one scene for all monitors */
static uint32_t *g_bits;                            /* BGRX copy of the raster, bottom-up */
static BITMAPINFO g_bmi;
static DWORD g_t0;
static POINT g_home;

/* settings (HKCU\Software\Nightfall): mode 0 = auto (about 540 art rows, whole art pixels),
 * mode 1 = manual art-pixel size (width 0 = follow the screen's aspect). Default: manual, 300 rows.
 * The rest are the scene options of the terminal build: --grass-density/-spread/-zspread, --biome, --spawn-rate. */
static struct {
    DWORD mode, w, h;
    char density[16], spread[16], zspread[16];      /* foreground plants, 1 = default */
    char biome[48];                                 /* auto | off | NAME */
    char rates[160];                                /* "rabbit=0 dhole=0.2": spawn chance per species */
} g_cfg = { 1, 0, 300, "1", "1", "1", "auto", "" };

static const char REG_KEY[] = "Software\\Nightfall";

static DWORD reg_get(HKEY k, const char *name, DWORD def)
{
    DWORD v, sz = sizeof v, t;
    return RegQueryValueExA(k, name, NULL, &t, (BYTE *)&v, &sz) == 0 && t == REG_DWORD ? v : def;
}

static void reg_get_str(HKEY k, const char *name, char *dst, DWORD cap)
{
    char b[256];
    DWORD sz = sizeof b - 1, t;
    if (RegQueryValueExA(k, name, NULL, &t, (BYTE *)b, &sz) != 0 || t != REG_SZ) return;
    b[sz] = 0;
    lstrcpynA(dst, b, (int)cap);
}

static void reg_set_str(HKEY k, const char *name, const char *v)
{
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)v, (DWORD)strlen(v) + 1);
}

static void load_cfg(void)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) != 0) return;
    g_cfg.mode = reg_get(k, "Mode", g_cfg.mode);
    g_cfg.w = reg_get(k, "Width", g_cfg.w);
    g_cfg.h = reg_get(k, "Height", g_cfg.h);
    reg_get_str(k, "GrassDensity", g_cfg.density, sizeof g_cfg.density);
    reg_get_str(k, "GrassSpread", g_cfg.spread, sizeof g_cfg.spread);
    reg_get_str(k, "GrassZSpread", g_cfg.zspread, sizeof g_cfg.zspread);
    reg_get_str(k, "Biome", g_cfg.biome, sizeof g_cfg.biome);
    reg_get_str(k, "SpawnRates", g_cfg.rates, sizeof g_cfg.rates);
    RegCloseKey(k);
}

static void save_cfg(void)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != 0) return;
    RegSetValueExA(k, "Mode", 0, REG_DWORD, (const BYTE *)&g_cfg.mode, 4);
    RegSetValueExA(k, "Width", 0, REG_DWORD, (const BYTE *)&g_cfg.w, 4);
    RegSetValueExA(k, "Height", 0, REG_DWORD, (const BYTE *)&g_cfg.h, 4);
    reg_set_str(k, "GrassDensity", g_cfg.density);
    reg_set_str(k, "GrassSpread", g_cfg.spread);
    reg_set_str(k, "GrassZSpread", g_cfg.zspread);
    reg_set_str(k, "Biome", g_cfg.biome);
    reg_set_str(k, "SpawnRates", g_cfg.rates);
    RegCloseKey(k);
}

/* Debug overrides on the command line, they beat the saved settings and are never saved. Same names as
 * the terminal build:  --auto | --width N | --height N (these two imply manual) | --grass-density F |
 * --grass-spread F | --grass-zspread F | --biome NAME | --spawn-rate NAME=F [NAME=F ...] */
static void cfg_from_cmdline(const char *cmd)
{
    char buf[512], *tok[64];
    int n = 0;
    lstrcpynA(buf, cmd, sizeof buf);
    for (char *p = strtok(buf, " \t"); p && n < 64; p = strtok(NULL, " \t")) tok[n++] = p;
    for (int i = 0; i < n; i++) {
        const char *a = tok[i], *v = i + 1 < n ? tok[i + 1] : "";
        if (!strcmp(a, "--auto")) g_cfg.mode = 0;
        else if (!strcmp(a, "--width")) { g_cfg.w = (DWORD)atoi(v); g_cfg.mode = 1; i++; }
        else if (!strcmp(a, "--height")) { g_cfg.h = (DWORD)atoi(v); g_cfg.mode = 1; i++; }
        else if (!strcmp(a, "--grass-density")) { lstrcpynA(g_cfg.density, v, sizeof g_cfg.density); i++; }
        else if (!strcmp(a, "--grass-spread")) { lstrcpynA(g_cfg.spread, v, sizeof g_cfg.spread); i++; }
        else if (!strcmp(a, "--grass-zspread")) { lstrcpynA(g_cfg.zspread, v, sizeof g_cfg.zspread); i++; }
        else if (!strcmp(a, "--biome")) { lstrcpynA(g_cfg.biome, v, sizeof g_cfg.biome); i++; }
        else if (!strcmp(a, "--spawn-rate")) {
            g_cfg.rates[0] = 0;
            while (i + 1 < n && strchr(tok[i + 1], '=') && strncmp(tok[i + 1], "--", 2) != 0) {
                if (g_cfg.rates[0]) lstrcatA(g_cfg.rates, " ");
                if (lstrlenA(g_cfg.rates) + lstrlenA(tok[i + 1]) + 2 < (int)sizeof g_cfg.rates) lstrcatA(g_cfg.rates, tok[i + 1]);
                i++;
            }
        }
    }
}

static float fval(const char *s, float def, float lo)
{
    if (!*s) return def;
    float f = (float)atof(s);
    return f < lo ? lo : f;
}

/* hands the scene options to the engine (the same globals the terminal build sets from its arguments) */
static void apply_scene_options(void)
{
    g_fg_density = fval(g_cfg.density, 1.0f, 0.05f);
    g_fg_spread = fval(g_cfg.spread, 1.0f, 0.0f);
    g_fg_zspread = fval(g_cfg.zspread, 1.0f, 0.0f);
    g_biome_arg = g_cfg.biome[0] ? g_cfg.biome : "auto";
    char buf[sizeof g_cfg.rates];
    lstrcpynA(buf, g_cfg.rates, sizeof buf);
    for (char *p = strtok(buf, " ,;\t"); p; p = strtok(NULL, " ,;\t")) spawn_rate_set(p);
}

/* --- /c: settings dialog (built by hand, no resource file) --- */
enum { ID_AUTO = 10, ID_MANUAL, ID_W, ID_H, ID_GD, ID_GS, ID_GZ, ID_BIOME, ID_RATES };
static int g_dpi = 96;
#define PX(v) ((v) * g_dpi / 96)

static LRESULT CALLBACK cfgproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            char b[16];
            GetDlgItemTextA(h, ID_W, b, sizeof b); g_cfg.w = (DWORD)atoi(b);
            GetDlgItemTextA(h, ID_H, b, sizeof b); g_cfg.h = (DWORD)atoi(b);
            g_cfg.mode = IsDlgButtonChecked(h, ID_MANUAL) ? 1 : 0;
            GetDlgItemTextA(h, ID_GD, g_cfg.density, sizeof g_cfg.density);
            GetDlgItemTextA(h, ID_GS, g_cfg.spread, sizeof g_cfg.spread);
            GetDlgItemTextA(h, ID_GZ, g_cfg.zspread, sizeof g_cfg.zspread);
            GetDlgItemTextA(h, ID_BIOME, g_cfg.biome, sizeof g_cfg.biome);
            GetDlgItemTextA(h, ID_RATES, g_cfg.rates, sizeof g_cfg.rates);
            save_cfg();
            DestroyWindow(h);
        } else if (LOWORD(wp) == IDCANCEL) DestroyWindow(h);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void run_settings(HINSTANCE inst)
{
    load_cfg();
    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = cfgproc;
    wc.hInstance = inst;
    wc.lpszClassName = "NightfallCfg";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName, "Nightfall settings",
                             WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, PX(430), PX(450),
                             NULL, NULL, inst, NULL);
#define CTL(cls, txt, style, x, y, w, hh, id) CreateWindowExA(0, cls, txt, WS_CHILD | WS_VISIBLE | (style), \
        PX(x), PX(y), PX(w), PX(hh), h, (HMENU)(INT_PTR)(id), inst, NULL)
#define TXT (WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP)
    CTL("BUTTON", "Auto: about 540 art rows, crisp whole-pixel scaling", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 14, 14, 390, 22, ID_AUTO);
    CTL("BUTTON", "Manual size of the main monitor (art pixels):", BS_AUTORADIOBUTTON, 14, 44, 390, 22, ID_MANUAL);
    CTL("STATIC", "Width", 0, 36, 76, 44, 20, 0);
    CTL("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 82, 73, 60, 24, ID_W);
    CTL("STATIC", "Height", 0, 160, 76, 44, 20, 0);
    CTL("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 208, 73, 60, 24, ID_H);
    CTL("STATIC", "Width 0 = follow the screen's aspect. Default 0 x 300. Bigger = finer pixels.", 0, 36, 104, 380, 20, 0);

    CTL("STATIC", "Foreground plants (the grass, ferns and flowers in front). 1 = default:", 0, 14, 140, 400, 20, 0);
    CTL("STATIC", "Density", 0, 36, 168, 50, 20, 0);
    CTL("EDIT", "", TXT, 90, 165, 50, 24, ID_GD);
    CTL("STATIC", "Spread", 0, 156, 168, 50, 20, 0);
    CTL("EDIT", "", TXT, 210, 165, 50, 24, ID_GS);
    CTL("STATIC", "Depth", 0, 276, 168, 44, 20, 0);
    CTL("EDIT", "", TXT, 324, 165, 50, 24, ID_GZ);
    CTL("STATIC", "Density: how many plants. 0.5 = half as many, 2 = twice as many.\nSpread: how uneven the gaps are. 0 = evenly spaced, 2 = clumpy.\nDepth: how many layers in front. 0 = one flat layer, 2 = deeper.", 0, 36, 194, 380, 50, 0);

    CTL("STATIC", "Biome", 0, 14, 258, 70, 20, 0);
    CTL("EDIT", "", TXT, 90, 255, 120, 24, ID_BIOME);
    CTL("STATIC", "auto | off | a name (nightfall --biome list)", 0, 220, 258, 200, 20, 0);

    CTL("STATIC", "Spawn rates (same as --spawn-rate, species chance 0 = never):", 0, 14, 296, 400, 20, 0);
    CTL("EDIT", "", TXT, 14, 320, 400, 24, ID_RATES);
    CTL("STATIC", "e.g.  rabbit=0 dhole=0.2 fox=0.3   (separate with spaces)", 0, 14, 348, 400, 20, 0);

    CTL("BUTTON", "OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 250, 380, 76, 26, IDOK);
    CTL("BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, 334, 380, 76, 26, IDCANCEL);
    char b[16];
    wsprintfA(b, "%u", (unsigned)g_cfg.w); SetDlgItemTextA(h, ID_W, b);
    wsprintfA(b, "%u", (unsigned)g_cfg.h); SetDlgItemTextA(h, ID_H, b);
    SetDlgItemTextA(h, ID_GD, g_cfg.density);
    SetDlgItemTextA(h, ID_GS, g_cfg.spread);
    SetDlgItemTextA(h, ID_GZ, g_cfg.zspread);
    SetDlgItemTextA(h, ID_BIOME, g_cfg.biome);
    SetDlgItemTextA(h, ID_RATES, g_cfg.rates);
    CheckRadioButton(h, ID_AUTO, ID_MANUAL, g_cfg.mode ? ID_MANUAL : ID_AUTO);
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0)
        if (!IsDialogMessageA(h, &m)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

static BOOL CALLBACK mon_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp)
{
    if (g_nmon < MAX_MON) g_mon[g_nmon++].rc = *rc;
    return TRUE;
}

/* A monitor shows the part of the scene under it. Horizontally it is its place on the desktop; vertically
 * every monitor is anchored at the bottom of the scene (bottom of the screen = bottom of the scene), so
 * monitors of different heights or offsets all get the ground at the bottom. Bottom-up DIB: ySrc 0 = bottom. */
static void draw_mon(int i)
{
    mon_t *m = &g_mon[i];
    if (!g_bits) return;
    int x0 = (int)lroundf((float)(m->rc.left - g_vx) / g_sx), x1 = (int)lroundf((float)(m->rc.right - g_vx) / g_sx);
    int rows = (int)lroundf((float)(m->rc.bottom - m->rc.top) / g_sy);
    if (x1 > g_r.w) x1 = g_r.w;
    if (rows > g_r.h) rows = g_r.h;
    HDC dc = GetDC(m->hwnd);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, m->rc.right - m->rc.left, m->rc.bottom - m->rc.top,
                  x0, 0, x1 - x0, rows, g_bits, &g_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(m->hwnd, dc);
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

/* raster -> BGRX, rows bottom-up (a bottom-up DIB has an unambiguous origin) */
static void present(void)
{
    for (int y = 0; y < g_r.h; y++) {
        const rgb_t *src = g_r.c + (size_t)y * g_r.w;
        uint32_t *dst = g_bits + (size_t)(g_r.h - 1 - y) * g_r.w;
        for (int x = 0; x < g_r.w; x++) dst[x] = ((uint32_t)src[x].r << 16) | ((uint32_t)src[x].g << 8) | src[x].b;
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
    const char *args = cmd;
    for (; *cmd; cmd++)
        if (*cmd == '/' || *cmd == '-') { mode = (char)(cmd[1] | 0x20); break; }
    if (mode == 'p') return 0;
    if (mode == 'c') {
        SetProcessDPIAware();
        run_settings(inst);
        return 0;
    }
    HANDLE once = CreateMutexA(NULL, TRUE, "Local\\NightfallScreensaver");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDPIAware();
    EnumDisplayMonitors(NULL, NULL, mon_cb, 0);
    if (g_nmon == 0) return 1;
    int prim = 0;                                   /* the primary monitor (contains 0,0) sets the art pixel size */
    g_vx = g_mon[0].rc.left;
    int vr = g_mon[0].rc.right, tallest = 0;
    for (int i = 0; i < g_nmon; i++) {
        const RECT *r = &g_mon[i].rc;
        if (r->left <= 0 && r->top <= 0 && r->right > 0 && r->bottom > 0) prim = i;
        if (r->left < g_vx) g_vx = r->left;
        if (r->right > vr) vr = r->right;
        if (r->bottom - r->top > tallest) tallest = r->bottom - r->top;
    }
    int pw = g_mon[prim].rc.right - g_mon[prim].rc.left, ph = g_mon[prim].rc.bottom - g_mon[prim].rc.top;

    /* One scene for all monitors: as wide as the desktop, as tall as the tallest monitor, anchored at the
     * bottom. Manual: the primary monitor is W x H art pixels (width 0 = its aspect); auto: about 540 rows. */
    load_cfg();
    cfg_from_cmdline(args);
    apply_scene_options();
    if (g_cfg.mode == 1) {
        int ch = clampi((int)g_cfg.h, 48, 1200);
        int cw = g_cfg.w > 0 ? (int)g_cfg.w : (int)((long long)ch * pw / ph);
        cw = clampi(cw, 64, NF_MAX_W);
        g_sx = (float)pw / (float)cw;
        g_sy = (float)ph / (float)ch;
    } else {
        int sc = (ph + 270) / 540;
        g_sx = g_sy = (float)(sc < 1 ? 1 : sc);
    }
    while ((float)(vr - g_vx) / g_sx > (float)NF_MAX_W) g_sx += 0.5f;      /* the world is NF_MAX_W wide at most */
    int rw = (int)lroundf((float)(vr - g_vx) / g_sx);
    int rh = clampi((int)lroundf((float)tallest / g_sy), 48, 1200);

    if (strstr(args, "--info")) {                    /* debug: show the geometry and quit */
        char t[1024];
        int n = wsprintfA(t, "mode %u  w %u  h %u\nscene %dx%d art px  (1 art px = %d x %d screen px)\n", (unsigned)g_cfg.mode,
                          (unsigned)g_cfg.w, (unsigned)g_cfg.h, rw, rh, (int)(g_sx + 0.5f), (int)(g_sy + 0.5f));
        for (int i = 0; i < g_nmon; i++)
            n += wsprintfA(t + n, "monitor %d%s: %d,%d  %dx%d px\n", i, i == prim ? " (primary)" : "",
                           (int)g_mon[i].rc.left, (int)g_mon[i].rc.top,
                           (int)(g_mon[i].rc.right - g_mon[i].rc.left), (int)(g_mon[i].rc.bottom - g_mon[i].rc.top));
        MessageBoxA(NULL, t, "Nightfall --info", MB_OK);
        return 0;
    }

    library_t lib;
    uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)GetTickCount() << 20);
    nf_load_library(&lib, NULL, seed);
    raster_init(&g_r, rw, rh);
    g_r.fog = mkrgb(6, 12, 14);
    g_r.fog_near = 100;
    g_r.fog_far = 40;
    g_r.fog_max = 0.8f;
    g_bits = malloc((size_t)rw * rh * 4);
    world_t *w = malloc(sizeof *w);
    if (!g_bits || !w) return 1;
    memset(&g_bmi, 0, sizeof g_bmi);
    g_bmi.bmiHeader.biSize = sizeof g_bmi.bmiHeader;
    g_bmi.bmiHeader.biWidth = rw;
    g_bmi.bmiHeader.biHeight = rh;                  /* bottom-up */
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
    SetForegroundWindow(g_mon[prim].hwnd);
    SetFocus(g_mon[prim].hwnd);
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
    free(g_bits);
    raster_free(&g_r);
    lib_free(&lib);
    CloseHandle(once);
    return 0;
}
