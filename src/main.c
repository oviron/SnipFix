/*
 * SnipFix - exact colours in Windows screenshots with HDR on.
 * Windows layer: display SDR white level, Snipping Tool overlay watch, PrtScn keys,
 * tray icon, autostart. Built without the C runtime.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include "logic.h"

extern IMAGE_DOS_HEADER __ImageBase;            /* this module: no GetModuleHandle import */
#define HINST        ((HINSTANCE)&__ImageBase)

#define WM_APP_TRAY  (WM_APP + 1)
#define TIMER_RESTORE 1                         /* overlay shown: its frame is frozen, restore soon */
#define TIMER_SAFETY 2                          /* never leave SDR white lowered */
#define TIMER_TRAY   3
#define SHOWN_DELAY_MS 50
#define SAFETY_MS    2000                       /* overlay path: it shows within ~0.4 s */
#define KEY_HOLD_MS  250                        /* Win+PrtScn / Alt+PrtScn: capture is immediate */
#define MAX_TARGETS  8
#define OVERLAY_CLASS L"SnipOverlayRootWindow"  /* Snipping Tool's screen overlay */
#define CHROME_CLASS L"Chrome_WidgetWin_"       /* Chromium/Electron top-level windows */
#define CFG_KEY      L"Software\\SnipFix"
#define RUN_KEY      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define KB_KEY       L"Control Panel\\Keyboard"

/* Private: the same call the "SDR content brightness" slider makes (also used by
 * Twinkle Tray). Level is a multiple of 80 nits times 1000, like the documented getter. */
#define SET_SDR_WHITE_LEVEL ((DISPLAYCONFIG_DEVICE_INFO_TYPE)0xFFFFFFEE)
typedef struct {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 SDRWhiteLevel;
    BYTE finalValue;
} SetSdrWhite;

typedef struct { LUID adapter; UINT32 id; UINT32 level; } Target;

enum { ID_AUTOSTART = 1, ID_EXIT };

typedef struct {
    const wchar_t *autostart, *exit, *tip;
} Strings;

static const Strings EN = { L"Start with Windows", L"Exit", L"SnipFix: exact screenshot colours with HDR" };
static const Strings RU = {
    L"\x0417\x0430\x043F\x0443\x0441\x043A\x0430\x0442\x044C \x0432\x043C\x0435\x0441\x0442\x0435 \x0441 Windows",
    L"\x0412\x044B\x0445\x043E\x0434",
    L"SnipFix: \x0442\x043E\x0447\x043D\x044B\x0435 \x0446\x0432\x0435\x0442\x0430 \x0441\x043A\x0440\x0438\x043D\x0448\x043E\x0442\x043E\x0432 \x0441 HDR"
};

static struct {
    SfState st;
    HWND hwnd;
    HHOOK kb;
    HWINEVENTHOOK ev;
    HANDLE mutex;
    HICON icon;
    UINT msg_taskbar;
    BOOL tray_added;
    Target saved[MAX_TARGETS];
    int nsaved;
    const Strings *s;
    wchar_t self[MAX_PATH];
} g;

static wchar_t buf[MAX_PATH + 8];

/* ---------- displays ---------- */

/* Active displays in HDR mode whose SDR white is above 80 nits. */
static int hdr_targets(Target *out, int max)
{
    static DISPLAYCONFIG_PATH_INFO paths[32];
    static DISPLAYCONFIG_MODE_INFO modes[64];
    UINT32 np = 32, nm = 64;
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, NULL) != ERROR_SUCCESS) return 0;
    int n = 0;
    for (UINT32 i = 0; i < np && n < max; i++) {
        const DISPLAYCONFIG_PATH_TARGET_INFO *t = &paths[i].targetInfo;
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ac = { { DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(ac), t->adapterId, t->id } };
        if (DisplayConfigGetDeviceInfo(&ac.header) != ERROR_SUCCESS) continue;
        if (!ac.advancedColorEnabled || ac.wideColorEnforced) continue; /* HDR only, not SDR auto colour management */
        DISPLAYCONFIG_SDR_WHITE_LEVEL w = { { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(w), t->adapterId, t->id } };
        if (DisplayConfigGetDeviceInfo(&w.header) != ERROR_SUCCESS || !sf_needs_adjust(w.SDRWhiteLevel)) continue;
        out[n].adapter = t->adapterId;
        out[n].id = t->id;
        out[n].level = w.SDRWhiteLevel;
        n++;
    }
    return n;
}

static void set_level(const Target *t, UINT32 level)
{
    SetSdrWhite s = { { SET_SDR_WHITE_LEVEL, sizeof(s), t->adapter, t->id }, level, 1 };
    DisplayConfigSetDeviceInfo(&s.header);
}

/* Chromium reads the SDR white level when it starts and on WM_DISPLAYCHANGE only:
 * tell its windows, so they repaint with the new multiplier before the capture. */
static BOOL CALLBACK notify_one(HWND h, LPARAM lp)
{
    wchar_t cls[24];
    DWORD_PTR r;
    (void)lp;
    if (GetClassNameW(h, cls, ARRAYSIZE(cls)) && CompareStringOrdinal(cls, ARRAYSIZE(CHROME_CLASS) - 1,
                                                                    CHROME_CLASS, ARRAYSIZE(CHROME_CLASS) - 1, FALSE) == CSTR_EQUAL)
        SendMessageTimeoutW(h, WM_DISPLAYCHANGE, 32, MAKELPARAM(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)),
                            SMTO_ABORTIFHUNG, 100, &r);
    return TRUE;
}

static void notify_chromium(void) { EnumWindows(notify_one, 0); }

/* Remembered in HKCU while lowered: if SnipFix dies in those few hundred ms, the
 * next start puts the user's level back. */
static void save_pending(void)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"Pending", REG_BINARY, g.saved, (DWORD)(g.nsaved * sizeof(Target)));
}

static BOOL lower(void)
{
    g.nsaved = hdr_targets(g.saved, MAX_TARGETS);
    if (!g.nsaved) return FALSE;
    save_pending();
    for (int i = 0; i < g.nsaved; i++) set_level(&g.saved[i], SF_LEVEL_80_NITS);
    notify_chromium();
    return TRUE;
}

static void restore(void)
{
    for (int i = 0; i < g.nsaved; i++) set_level(&g.saved[i], g.saved[i].level);
    notify_chromium();
    g.nsaved = 0;
    RegDeleteKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"Pending");
}

/* After a crash or a kill mid-capture. Adapter LUIDs change across reboots, so
 * match by target id, and only touch a display still sitting at exactly 80 nits. */
static void restore_pending(void)
{
    Target old[MAX_TARGETS];
    DWORD size = sizeof(old);
    if (RegGetValueW(HKEY_CURRENT_USER, CFG_KEY, L"Pending", RRF_RT_REG_BINARY, NULL, old, &size) != ERROR_SUCCESS) return;
    static DISPLAYCONFIG_PATH_INFO paths[32];
    static DISPLAYCONFIG_MODE_INFO modes[64];
    UINT32 np = 32, nm = 64;
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths, &nm, modes, NULL) == ERROR_SUCCESS)
        for (UINT32 i = 0; i < np; i++) {
            const DISPLAYCONFIG_PATH_TARGET_INFO *t = &paths[i].targetInfo;
            DISPLAYCONFIG_SDR_WHITE_LEVEL w = { { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(w), t->adapterId, t->id } };
            if (DisplayConfigGetDeviceInfo(&w.header) != ERROR_SUCCESS || w.SDRWhiteLevel != SF_LEVEL_80_NITS) continue;
            for (DWORD k = 0; k < size / sizeof(Target); k++)
                if (old[k].id == t->id) {
                    Target cur = { t->adapterId, t->id, old[k].level };
                    set_level(&cur, cur.level);
                }
        }
    notify_chromium();
    RegDeleteKeyValueW(HKEY_CURRENT_USER, CFG_KEY, L"Pending");
}

/* ---------- events ---------- */

static void act(SfEvent e)
{
    switch (sf_on_event(&g.st, e)) {
    case SF_ACT_BEGIN:
        if (!lower()) { sf_cancel(&g.st); return; }   /* no HDR display above 80 nits */
        SetTimer(g.hwnd, TIMER_SAFETY, e == SF_EV_DIRECT_CAPTURE ? KEY_HOLD_MS : SAFETY_MS, NULL);
        break;
    case SF_ACT_END_SOON:
        SetTimer(g.hwnd, TIMER_RESTORE, SHOWN_DELAY_MS, NULL);
        break;
    case SF_ACT_END:
        KillTimer(g.hwnd, TIMER_RESTORE);
        KillTimer(g.hwnd, TIMER_SAFETY);
        restore();
        break;
    }
}

/* The overlay window is created ~0.15 s before Snipping Tool grabs the screen:
 * lowering takes ~30 ms, well inside that. */
static void CALLBACK on_win_event(HWINEVENTHOOK h, DWORD ev, HWND w, LONG obj, LONG child, DWORD tid, DWORD ms)
{
    wchar_t cls[ARRAYSIZE(OVERLAY_CLASS)];
    (void)h; (void)tid; (void)ms;
    if (!w || obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (!GetClassNameW(w, cls, ARRAYSIZE(cls)) || lstrcmpW(cls, OVERLAY_CLASS) != 0) return;
    act(ev == EVENT_OBJECT_CREATE ? SF_EV_OVERLAY_CREATED : ev == EVENT_OBJECT_SHOW ? SF_EV_OVERLAY_SHOWN : SF_EV_OVERLAY_GONE);
}

static int held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static int prtscn_opens_snipping(void)
{
    DWORD v = 1, size = sizeof(v); /* on by default in current Windows 11 */
    RegGetValueW(HKEY_CURRENT_USER, KB_KEY, L"PrintScreenKeyForSnippingEnabled", RRF_RT_REG_DWORD, NULL, &v, &size);
    return v != 0;
}

/* Only PrtScn is looked at. Lowering happens here, before the key reaches Windows:
 * Win+PrtScn and Alt+PrtScn capture the moment the key goes down. */
static LRESULT CALLBACK kb_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lp;
        if (k->vkCode == SF_VK_SNAPSHOT &&
            sf_direct_capture(k->vkCode, wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN, held(VK_LWIN) || held(VK_RWIN),
                              (k->flags & LLKHF_ALTDOWN) != 0, prtscn_opens_snipping()))
            act(SF_EV_DIRECT_CAPTURE);
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

/* ---------- tray ---------- */

static HICON make_icon(int cx, int cy)
{
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB } };
    DWORD *px = NULL;
    HICON icon = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, NULL);
    if (!dc || !color || !px || !mask) goto done;
    HGDIOBJ old = SelectObject(dc, color);
    int r = cx / 4; /* corner radius */
    for (int y = 0; y < cy; y++)
        for (int x = 0; x < cx; x++) {
            int dx = x < r ? r - x : x >= cx - r ? x - (cx - r - 1) : 0;
            int dy = y < r ? r - y : y >= cy - r ? y - (cy - r - 1) : 0;
            px[y * cx + x] = dx * dx + dy * dy <= r * r ? 0xFFE07A10u : 0; /* amber */
        }
    HFONT font = CreateFontW(-cy * 3 / 4, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldf = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = { 0, 0, cx, cy };
    GdiFlush();
    DrawTextW(dc, L"H", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();
    for (int i = 0; i < cx * cy; i++) /* GDI clears alpha where it draws: text is inside the fill */
        if (px[i] && !(px[i] >> 24)) px[i] |= 0xFF000000u;
    SelectObject(dc, oldf);
    DeleteObject(font);
    SelectObject(dc, old);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    icon = CreateIconIndirect(&ii);
done:
    if (color) DeleteObject(color);
    if (mask) DeleteObject(mask);
    if (dc) DeleteDC(dc);
    return icon;
}

static void make_tray_icon(void)
{
    HWND bar = FindWindowW(L"Shell_TrayWnd", NULL);
    UINT dpi = bar ? GetDpiForWindow(bar) : 0;
    int cx = dpi ? GetSystemMetricsForDpi(SM_CXSMICON, dpi) : GetSystemMetrics(SM_CXSMICON);
    int cy = dpi ? GetSystemMetricsForDpi(SM_CYSMICON, dpi) : GetSystemMetrics(SM_CYSMICON);
    if (g.icon) DestroyIcon(g.icon);
    g.icon = make_icon(cx, cy);
}

static void tray(DWORD op)
{
    NOTIFYICONDATAW n = { sizeof(n) };
    n.hWnd = g.hwnd;
    n.uID = 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_APP_TRAY;
    n.hIcon = g.icon;
    lstrcpynW(n.szTip, g.s->tip, ARRAYSIZE(n.szTip));
    BOOL ok = Shell_NotifyIconW(op, &n);
    if (op == NIM_DELETE) g.tray_added = FALSE;
    else if (op == NIM_ADD) g.tray_added = ok;
}

/* At logon the taskbar may not exist yet: retry until it does (or TaskbarCreated arrives). */
static void add_tray(void)
{
    if (!g.tray_added) tray(NIM_ADD);
    if (!g.tray_added) SetTimer(g.hwnd, TIMER_TRAY, 2000, NULL);
}

static BOOL autostart_on(void)
{
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, L"SnipFix", RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static void set_autostart(BOOL on)
{
    if (on) {
        wsprintfW(buf, L"\"%s\"", g.self);
        RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"SnipFix", REG_SZ, buf, (DWORD)(lstrlenW(buf) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, L"SnipFix");
    }
}

static void show_menu(void)
{
    BOOL run = autostart_on();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (run ? MF_CHECKED : 0), ID_AUTOSTART, g.s->autostart);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, g.s->exit);
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(g.hwnd);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, g.hwnd, NULL);
    DestroyMenu(m);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    if (id == ID_AUTOSTART) set_autostart(!run);
    else if (id == ID_EXIT) DestroyWindow(g.hwnd);
}

/* ---------- window ---------- */

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_TRAY) { KillTimer(h, TIMER_TRAY); add_tray(); }
        else act(SF_EV_TIMER);
        return 0;
    case WM_APP_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_menu();
        return 0;
    case WM_CLOSE:          /* only the tray menu may close SnipFix */
        return 0;
    case WM_DESTROY:
        if (g.st.active) { sf_cancel(&g.st); restore(); }
        tray(NIM_DELETE);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g.msg_taskbar && msg) { /* Explorer restarted, or the taskbar DPI changed */
        tray(NIM_DELETE);
        make_tray_icon();
        add_tray();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int run(void)
{
    DWORD len = GetModuleFileNameW(NULL, g.self, MAX_PATH);
    if (!len || len >= MAX_PATH) return 1;
    g.s = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? &RU : &EN;

    g.mutex = CreateMutexW(NULL, FALSE, L"SnipFix");
    if (!g.mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0; /* already running */

    restore_pending();
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); /* lower in time, every time */
    make_tray_icon();
    g.msg_taskbar = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = HINST;
    wc.lpszClassName = L"SnipFixWindow";
    RegisterClassW(&wc);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"SnipFix", 0, 0, 0, 0, 0, NULL, NULL, HINST, NULL);
    if (!g.hwnd) return 1;

    g.ev = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, on_win_event, 0, 0,
                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g.kb = SetWindowsHookExW(WH_KEYBOARD_LL, kb_proc, HINST, 0);
    add_tray();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (g.kb) UnhookWindowsHookEx(g.kb);
    if (g.ev) UnhookWinEvent(g.ev);
    return 0;
}

/* System DLLs from System32 only; all static imports are KnownDLLs. */
void WINAPI start(void)
{
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    ExitProcess((UINT)run());
}
