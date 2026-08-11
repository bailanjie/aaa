// Clipboard History Manager — Windows native clipboard manager
// Ponytail build: single .exe, Win32 + GDI+ + SQLite, zero runtime deps

#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>

#include "database.h"
#include "clipboard_monitor.h"
#include "card_panel.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

// ── constants ────────────────────────────────────────────────────────

#define APP_NAME       L"ClipboardHistory"
#define APP_TITLE      L"\u526A\u8D34\u677F\u5386\u53F2"
#define WM_TRAYICON    (WM_APP + 1)
#define ID_TRAY_ICON   1
#define IDM_SHOW       1001
#define IDM_AUTOSTART  1002
#define IDM_EXIT       1003
#define ID_CARD_PANEL  2001
#define HOTKEY_ALT_C   1

static const wchar_t* MUTEX_NAME    = L"Global\\ClipboardHistory_SingleInstance";
static const wchar_t* REG_KEY_APP   = L"Software\\ClipboardHistory";
static const wchar_t* REG_KEY_RUN   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// ── globals ──────────────────────────────────────────────────────────

static HWND       g_hMainWnd    = nullptr;
static HWND       g_hCardPanel  = nullptr;
static NOTIFYICONDATAW g_nid    = {};
static bool       g_autoStart   = false;
static ULONG_PTR  g_gdiToken    = 0;

// ── helpers ──────────────────────────────────────────────────────────

// Robust window activation: handles Windows SetForegroundWindow restrictions
static void activate_window(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);

    DWORD foreThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD myThread = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (foreThread && foreThread != myThread) {
        attached = AttachThreadInput(myThread, foreThread, TRUE);
    }

    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);

    if (attached) {
        AttachThreadInput(myThread, foreThread, FALSE);
    }
}

// Get app data directory: %APPDATA%\ClipboardHistory
static bool get_appdata_path(wchar_t* buf, size_t bufSize) {
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
        return false;
    swprintf_s(buf, bufSize, L"%s\\ClipboardHistory", appData);
    return true;
}

// Ensure directory exists
static void ensure_dir(const wchar_t* path) {
    SHCreateDirectoryExW(nullptr, path, nullptr);
}

// Read/write DWORD from registry
static DWORD reg_get_dword(HKEY hKeyRoot, const wchar_t* subKey, const wchar_t* value, DWORD def) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return def;
    DWORD data = def, size = sizeof(data);
    RegQueryValueExW(hKey, value, nullptr, nullptr, (BYTE*)&data, &size);
    RegCloseKey(hKey);
    return data;
}

static void reg_set_dword(HKEY hKeyRoot, const wchar_t* subKey, const wchar_t* value, DWORD data) {
    HKEY hKey;
    RegCreateKeyExW(hKeyRoot, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, value, 0, REG_DWORD, (BYTE*)&data, sizeof(data));
    RegCloseKey(hKey);
}

static void reg_delete_value(HKEY hKeyRoot, const wchar_t* subKey, const wchar_t* value) {
    HKEY hKey;
    if (RegOpenKeyExW(hKeyRoot, subKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, value);
        RegCloseKey(hKey);
    }
}

// Check/update auto-start registry entry
static void update_autostart(bool enable) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (enable) {
        HKEY hKey;
        RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_RUN, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        wchar_t cmdLine[MAX_PATH + 16];
        swprintf_s(cmdLine, L"\"%s\" --tray", exePath);
        RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)cmdLine,
            (DWORD)((wcslen(cmdLine) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    } else {
        reg_delete_value(HKEY_CURRENT_USER, REG_KEY_RUN, APP_NAME);
    }
    g_autoStart = enable;
    reg_set_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"AutoStart", enable ? 1 : 0);
}

// Save/restore window position
static void save_window_pos() {
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_hMainWnd, &wp)) {
        reg_set_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowX", wp.rcNormalPosition.left);
        reg_set_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowY", wp.rcNormalPosition.top);
        reg_set_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowW",
            wp.rcNormalPosition.right - wp.rcNormalPosition.left);
        reg_set_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowH",
            wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
    }
}

static void restore_window_pos() {
    int x = (int)reg_get_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowX", CW_USEDEFAULT);
    int y = (int)reg_get_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowY", CW_USEDEFAULT);
    int w = (int)reg_get_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowW", 520);
    int h = (int)reg_get_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"WindowH", 580);
    if (x == (int)CW_USEDEFAULT || y == (int)CW_USEDEFAULT) {
        // Center on screen
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        x = (sw - w) / 2;
        y = (sh - h) / 2;
    }
    SetWindowPos(g_hMainWnd, nullptr, x, y, w, h, SWP_NOZORDER);
}

// ── tray icon ────────────────────────────────────────────────────────

static void tray_add(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);  // ponytail: system icon, swap with custom later
    wcscpy_s(g_nid.szTip, APP_TITLE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_remove() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void tray_show_menu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW, L"\u663E\u793A\u7A97\u53E3 (&S)");
    AppendMenuW(hMenu, MF_STRING | (g_autoStart ? MF_CHECKED : 0), IDM_AUTOSTART, L"\u5F00\u673A\u81EA\u542F (&A)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"\u9000\u51FA (&X)");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // Required for tray menu to dismiss properly
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// ── clipboard handler ────────────────────────────────────────────────

static void on_clipboard_change() {
    // Skip if this change was triggered by our own clipboard writes
    if (clipboard_consume_self_change()) return;
    // Try text first
    std::wstring text = clipboard_read_text();
    if (!text.empty()) {
        // Simple dedup: skip if same as last entry
        ClipEntry latest = db_get_latest();
        if (latest.content_type == 0 && latest.text == text) return;

        db_insert_text(text);
        card_panel_reload(g_hCardPanel);
        return;
    }

    // Try image
    std::vector<uint8_t> thumb;
    std::vector<uint8_t> png = clipboard_read_image_png(&thumb);
    if (!png.empty()) {
        db_insert_image(png.data(), png.size(), thumb.data(), thumb.size());
        card_panel_reload(g_hCardPanel);
    }
}

// ── window proc ──────────────────────────────────────────────────────

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;

        // Init database
        wchar_t appDataPath[MAX_PATH];
        if (get_appdata_path(appDataPath, MAX_PATH)) {
            ensure_dir(appDataPath);
            wchar_t dbPath[MAX_PATH];
            swprintf_s(dbPath, L"%s\\history.db", appDataPath);
            db_init(dbPath);
        }

        // Create card panel
        card_panel_register_class((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));
        g_hCardPanel = card_panel_create(hwnd, ID_CARD_PANEL,
            (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));

        // Start clipboard monitoring
        clipboard_start(hwnd);

        // System tray
        tray_add(hwnd);

        // Global hotkey: Alt+C (try MOD_ALT|MOD_NOREPEAT for less conflicts)
        if (!RegisterHotKey(hwnd, HOTKEY_ALT_C, MOD_ALT | MOD_NOREPEAT, 'C')) {
            // Fallback: try without MOD_NOREPEAT
            RegisterHotKey(hwnd, HOTKEY_ALT_C, MOD_ALT, 'C');
        }

        // Restore auto-start setting
        g_autoStart = reg_get_dword(HKEY_CURRENT_USER, REG_KEY_APP, L"AutoStart", 0) != 0;

        // Restore window position
        restore_window_pos();

        // Initial data load
        card_panel_reload(g_hCardPanel);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (g_hCardPanel) {
            SetWindowPos(g_hCardPanel, nullptr, 0, 0, rc.right, rc.bottom, SWP_NOZORDER);
        }
        return 0;
    }

    case WM_CLOSE:
        // Hide to tray instead of closing
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        save_window_pos();
        clipboard_stop(hwnd);
        tray_remove();
        UnregisterHotKey(hwnd, HOTKEY_ALT_C);
        db_close();
        PostQuitMessage(0);
        return 0;

    case WM_CLIPBOARDUPDATE:
        on_clipboard_change();
        return 0;

    case WM_HOTKEY:
        if (wp == HOTKEY_ALT_C) {
            if (IsWindowVisible(hwnd)) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                activate_window(hwnd);
            }
        }
        return 0;

    case WM_TRAYICON:
        if (wp == ID_TRAY_ICON) {
            if (LOWORD(lp) == WM_LBUTTONUP) {
                // Left click: toggle window
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                } else {
                    activate_window(hwnd);
                }
            } else if (LOWORD(lp) == WM_RBUTTONUP) {
                tray_show_menu(hwnd);
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_SHOW:
            activate_window(hwnd);
            break;
        case IDM_AUTOSTART:
            update_autostart(!g_autoStart);
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_ENDSESSION:
        if (wp) {
            save_window_pos();
            db_close();
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── entry point ──────────────────────────────────────────────────────

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    bool startInTray = pCmdLine && wcsstr(pCmdLine, L"--tray") != nullptr;

    // Single instance check
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Find and show the existing window
        HWND hExisting = FindWindowW(L"ClipboardHistoryMain", APP_TITLE);
        if (hExisting) {
            activate_window(hExisting);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Init GDI+
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gdiInput, nullptr);

    // Init common controls (for potential future use)
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    // Register main window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ClipboardHistoryMain";
    RegisterClassExW(&wc);

    // Create main window — start hidden (tray mode)
    HWND hwnd = CreateWindowExW(0, L"ClipboardHistoryMain", APP_TITLE,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
        return 1;
    }

    if (!startInTray) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiToken);
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return (int)msg.wParam;
}
