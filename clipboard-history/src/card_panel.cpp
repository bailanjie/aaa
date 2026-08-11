#define NOMINMAX  // prevent windows.h min/max macros from breaking std::min
#include "card_panel.h"
#include "database.h"
#include "clipboard_monitor.h"
#include <gdiplus.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

// ── constants ────────────────────────────────────────────────────────

static const int SEARCH_BAR_HEIGHT = 36;
static const int CARD_PADDING = 6;
static const int CARD_GAP = 4;
static const int TEXT_CARD_HEIGHT = 48;
static const int IMAGE_CARD_HEIGHT = 72;
static const int PIN_BTN_WIDTH = 28;
static const int DELETE_BTN_WIDTH = 22;
static const int THUMB_SIZE = 56;
static const int STATUS_BAR_H = 24;
static const int DATE_BAR_HEIGHT = 24;

// Colors
static const Gdiplus::Color BG_COLOR(240, 246, 251);
static const Gdiplus::Color CARD_BG(255, 255, 255);
static const Gdiplus::Color ACCENT(74, 158, 214);
static const Gdiplus::Color PINNED_COLOR(255, 193, 7);
static const Gdiplus::Color TEXT_COLOR(51, 51, 51);
static const Gdiplus::Color DIM_COLOR(150, 150, 150);
static const Gdiplus::Color DELETE_COLOR(220, 80, 80);
static const Gdiplus::Color SEPARATOR_COLOR(220, 225, 230);

// ── edit control subclass to forward mouse wheel to card panel ──────

static WNDPROC g_oldEditProc = nullptr;

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEWHEEL) {
        // Forward wheel to parent card panel so scrolling works without clicking the page first
        return SendMessage(GetParent(hwnd), msg, wp, lp);
    }
    return CallWindowProc(g_oldEditProc, hwnd, msg, wp, lp);
}

// ── per-card layout computed during paint ────────────────────────────

struct CardLayout {
    RECT cardRect;
    RECT pinRect;
    RECT deleteRect;
    int entryIndex;   // index into CardPanelData::filtered
};

// ── panel data ───────────────────────────────────────────────────────

struct CardPanelData {
    std::vector<ClipEntry> filtered;    // currently shown entries
    std::vector<CardLayout> layouts;    // computed during paint
    int scrollPos = 0;
    int totalContentH = 0;
    HWND hSearchEdit = nullptr;
    HFONT hFont = nullptr, hSmallFont = nullptr, hBoldFont = nullptr;
    bool showPinnedOnly = false;
    int panelWidth = 0, panelHeight = 0;
    RECT clearAllRect = {};  // "清空全部" button area in status bar
    int dateFilter = 0;      // 0=all, 1=today, 2=yesterday, 3=last7days
    RECT dateBtnRects[3] = {};  // click areas for date buttons
    int typeFilter = 0;      // 0=all, 1=text, 2=image, 3=files
    RECT typeBtnRects[3] = {};  // click areas for type buttons
};

// ── helpers ──────────────────────────────────────────────────────────

static void reload_data(CardPanelData* pd) {
    // Compute time range from date filter
    time_t timeFrom = 0, timeTo = 0;
    if (pd->dateFilter != 0) {
        time_t now = time(nullptr);
        struct tm tmNow;
        localtime_s(&tmNow, &now);
        tmNow.tm_hour = 0; tmNow.tm_min = 0; tmNow.tm_sec = 0;
        time_t todayStart = mktime(&tmNow);

        if (pd->dateFilter == 1) {        // 今天
            timeFrom = todayStart;
        } else if (pd->dateFilter == 2) { // 昨天
            timeFrom = todayStart - 86400;
            timeTo = todayStart - 1;
        } else if (pd->dateFilter == 3) { // 最近7天
            timeFrom = todayStart - 7 * 86400;
        }
    }

    if (pd->showPinnedOnly) {
        pd->filtered = db_get_history(L"", timeFrom, timeTo, pd->typeFilter);
        pd->filtered.erase(
            std::remove_if(pd->filtered.begin(), pd->filtered.end(),
                [](const ClipEntry& e) { return !e.pinned; }),
            pd->filtered.end());
    } else {
        wchar_t search[512];
        GetWindowTextW(pd->hSearchEdit, search, 512);
        pd->filtered = db_get_history(search, timeFrom, timeTo, pd->typeFilter);
    }
}

static int card_height(const ClipEntry& e) {
    if (e.content_type == 1) return IMAGE_CARD_HEIGHT;
    return TEXT_CARD_HEIGHT; // text and files share same height
}

// ── window proc ──────────────────────────────────────────────────────

static LRESULT CALLBACK CardPanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CardPanelData* pd = (CardPanelData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        pd = new CardPanelData();
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pd);

        // Create fonts
        pd->hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        pd->hSmallFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        pd->hBoldFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        // Create search edit
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(GetParent(hwnd), GWLP_HINSTANCE);
        pd->hSearchEdit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
            8, 6, 200, 24, hwnd, nullptr, hInst, nullptr);
        SendMessageW(pd->hSearchEdit, WM_SETFONT, (WPARAM)pd->hFont, TRUE);
        SendMessageW(pd->hSearchEdit, EM_SETCUEBANNER, TRUE,
            (LPARAM)L"\u641C\u7D22\u5386\u53F2\u8BB0\u5F55...");
        // Subclass edit to forward mouse wheel to card panel
        g_oldEditProc = (WNDPROC)SetWindowLongPtr(pd->hSearchEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);

        reload_data(pd);
        return 0;
    }

    case WM_SIZE: {
        pd->panelWidth = LOWORD(lp);
        pd->panelHeight = HIWORD(lp);
        // Resize search edit
        int editW = pd->panelWidth - 16;
        if (editW < 100) editW = 100;
        SetWindowPos(pd->hSearchEdit, nullptr, 8, 6, editW, 26, SWP_NOZORDER);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_COMMAND: {
        if ((HWND)lp == pd->hSearchEdit && HIWORD(wp) == EN_CHANGE) {
            reload_data(pd);
            pd->scrollPos = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        pd->scrollPos -= delta * 40;
        if (pd->scrollPos < 0) pd->scrollPos = 0;
        int visH = pd->panelHeight - SEARCH_BAR_HEIGHT - DATE_BAR_HEIGHT - STATUS_BAR_H;
        int maxScroll = pd->totalContentH - visH;
        if (maxScroll < 0) maxScroll = 0;
        if (pd->scrollPos > maxScroll) pd->scrollPos = maxScroll;
        SetScrollPos(hwnd, SB_VERT, pd->scrollPos, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_VSCROLL: {
        int visH = pd->panelHeight - SEARCH_BAR_HEIGHT - DATE_BAR_HEIGHT - STATUS_BAR_H;
        int maxScroll = pd->totalContentH - visH;
        if (maxScroll < 0) maxScroll = 0;

        switch (LOWORD(wp)) {
        case SB_LINEUP:        pd->scrollPos -= 24; break;
        case SB_LINEDOWN:      pd->scrollPos += 24; break;
        case SB_PAGEUP:        pd->scrollPos -= visH; break;
        case SB_PAGEDOWN:      pd->scrollPos += visH; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pd->scrollPos = HIWORD(wp); break;
        default: return 0;
        }

        if (pd->scrollPos < 0) pd->scrollPos = 0;
        if (pd->scrollPos > maxScroll) pd->scrollPos = maxScroll;
        SetScrollPos(hwnd, SB_VERT, pd->scrollPos, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);
        bool handled = false;

        // Check type filter buttons
        for (int i = 0; i < 3; i++) {
            RECT& r = pd->typeBtnRects[i];
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                pd->typeFilter = (pd->typeFilter == i + 1) ? 0 : (i + 1);
                reload_data(pd);
                pd->scrollPos = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Check date filter buttons
        for (int i = 0; i < 3; i++) {
            RECT& r = pd->dateBtnRects[i];
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                // Toggle: clicking active filter deselects it
                pd->dateFilter = (pd->dateFilter == i + 1) ? 0 : (i + 1);
                reload_data(pd);
                pd->scrollPos = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Check "清空全部" button in status bar
        {
            RECT& cr = pd->clearAllRect;
            if (mx >= cr.left && mx <= cr.right && my >= cr.top && my <= cr.bottom) {
                db_delete_all_unpinned();
                reload_data(pd);
                pd->scrollPos = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        for (auto& cl : pd->layouts) {
            // Convert card's absolute layout coords to screen coords (subtract scroll only)
            RECT cr = cl.cardRect;
            cr.top -= pd->scrollPos;
            cr.bottom -= pd->scrollPos;

            // Pin button area (top-left of card)
            RECT pr = { cr.left + 8, cr.top + 4, cr.left + 36, cr.top + 28 };
            // Delete button area (top-right of card, full height)
            int ch = cl.cardRect.bottom - cl.cardRect.top;
            int delX = cr.right - DELETE_BTN_WIDTH - CARD_PADDING;
            RECT dr = { delX, cr.top + 2, cr.right - CARD_PADDING, cr.top + ch - 2 };

            if (mx >= pr.left && mx <= pr.right && my >= pr.top && my <= pr.bottom) {
                db_toggle_pin(pd->filtered[cl.entryIndex].id);
                reload_data(pd);
                pd->scrollPos = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
                break;
            }
            if (mx >= dr.left && mx <= dr.right && my >= dr.top && my <= dr.bottom) {
                db_delete_entry(pd->filtered[cl.entryIndex].id);
                pd->filtered.erase(pd->filtered.begin() + cl.entryIndex);
                InvalidateRect(hwnd, nullptr, FALSE);
                handled = true;
                break;
            }
        }
        if (handled) return 0;
        break; // Let DefWindowProc handle (needed for double-click detection)
    }

    case WM_LBUTTONDBLCLK: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);

        for (auto& cl : pd->layouts) {
            // Convert card's absolute layout coords to screen coords (subtract scroll only)
            RECT cr = cl.cardRect;
            cr.top -= pd->scrollPos;
            cr.bottom -= pd->scrollPos;

            if (mx >= cr.left && mx <= cr.right &&
                my >= cr.top && my <= cr.bottom) {
                // Copy to clipboard
                ClipEntry& e = pd->filtered[cl.entryIndex];
                if (e.content_type == 0) {
                    clipboard_set_text(e.text);
                } else if (e.content_type == 2) {
                    std::vector<std::wstring> paths = db_get_file_paths(e.id);
                    if (!paths.empty()) {
                        clipboard_set_files(paths);
                    }
                } else {
                    // Load full image from DB on demand (list only has thumbnail)
                    std::vector<uint8_t> fullImg = db_get_image_blob(e.id);
                    if (!fullImg.empty()) {
                        clipboard_set_image(fullImg.data(), fullImg.size());
                    }
                }
                // Flash feedback: briefly change card color or show tooltip
                // ponytail: simple beep as feedback, visual flash if users complain
                MessageBeep(MB_OK);
                break;
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int w = clientRect.right;
        int h = clientRect.bottom;

        // Double-buffer to avoid flicker
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Fill background
        Gdiplus::SolidBrush bgBrush(BG_COLOR);
        g.FillRectangle(&bgBrush, 0, 0, w, h);

        // Draw search bar separator
        Gdiplus::Pen sepPen(SEPARATOR_COLOR, 1.0f);
        g.DrawLine(&sepPen, 0, SEARCH_BAR_HEIGHT, w, SEARCH_BAR_HEIGHT);

        // Pin filter toggle button
        Gdiplus::SolidBrush pinBtnBrush(pd->showPinnedOnly ? PINNED_COLOR : DIM_COLOR);
        g.FillRectangle(&pinBtnBrush, w - 40, 8, 32, 22);
        Gdiplus::Font pinFont(L"Segoe UI", 12);
        Gdiplus::SolidBrush whiteBrush(Gdiplus::Color::White);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"◆", -1, &pinFont, Gdiplus::RectF(w - 40, 8, 32, 22), &sf, &whiteBrush);

        // Card area starts below search bar + date buttons
        int startY = SEARCH_BAR_HEIGHT + DATE_BAR_HEIGHT + CARD_GAP;
        pd->layouts.clear();

        // Draw date filter buttons
        {
            const wchar_t* labels[] = { L"今天", L"昨天", L"最近7天" };
            int btnW = 64, btnH = 20;
            int btnY = SEARCH_BAR_HEIGHT + 2;
            Gdiplus::Font btnFont(L"Segoe UI", 10);

            for (int i = 0; i < 3; i++) {
                int btnX = 12 + i * (btnW + 6);
                bool active = (pd->dateFilter == i + 1);
                pd->dateBtnRects[i] = { btnX, btnY, btnX + btnW, btnY + btnH };

                Gdiplus::Color btnBg = active ? ACCENT : Gdiplus::Color(210, 220, 230);
                Gdiplus::SolidBrush btnBrush(btnBg);
                g.FillRectangle(&btnBrush, btnX, btnY, btnW, btnH);

                Gdiplus::Color textCol = active ? Gdiplus::Color::White : DIM_COLOR;
                Gdiplus::SolidBrush textBr(textCol);
                Gdiplus::StringFormat btnSf;
                btnSf.SetAlignment(Gdiplus::StringAlignmentCenter);
                btnSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(labels[i], -1, &btnFont,
                    Gdiplus::RectF((Gdiplus::REAL)btnX, (Gdiplus::REAL)btnY,
                        (Gdiplus::REAL)btnW, (Gdiplus::REAL)btnH), &btnSf, &textBr);
            }
        }

        // Draw type filter buttons (right side of date button row)
        {
            const wchar_t* typeLabels[] = { L"文本", L"图片", L"文件" };
            int btnW = 50, btnH = 20;
            int btnY = SEARCH_BAR_HEIGHT + 2;
            Gdiplus::Font btnFont(L"Segoe UI", 10);

            for (int i = 0; i < 3; i++) {
                int btnX = w - 16 - (3 - i) * (btnW + 5);
                bool active = (pd->typeFilter == i + 1);
                pd->typeBtnRects[i] = { btnX, btnY, btnX + btnW, btnY + btnH };

                Gdiplus::Color btnBg = active ? ACCENT : Gdiplus::Color(210, 220, 230);
                Gdiplus::SolidBrush btnBrush(btnBg);
                g.FillRectangle(&btnBrush, btnX, btnY, btnW, btnH);

                Gdiplus::Color textCol = active ? Gdiplus::Color::White : DIM_COLOR;
                Gdiplus::SolidBrush textBr(textCol);
                Gdiplus::StringFormat btnSf;
                btnSf.SetAlignment(Gdiplus::StringAlignmentCenter);
                btnSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(typeLabels[i], -1, &btnFont,
                    Gdiplus::RectF((Gdiplus::REAL)btnX, (Gdiplus::REAL)btnY,
                        (Gdiplus::REAL)btnW, (Gdiplus::REAL)btnH), &btnSf, &textBr);
            }
        }

        // Compute card positions (without scroll offset, for layout tracking)
        int cardW = w - 16; // 8px margin each side
        int y = startY;

        for (size_t i = 0; i < pd->filtered.size(); i++) {
            ClipEntry& e = pd->filtered[i];
            int ch = card_height(e);
            CardLayout cl;
            cl.entryIndex = (int)i;

            // Card rect
            cl.cardRect.left = 8;
            cl.cardRect.top = y;
            cl.cardRect.right = 8 + cardW;
            cl.cardRect.bottom = y + ch;
            y += ch + CARD_GAP;

            pd->layouts.push_back(cl);
        }
        pd->totalContentH = y - startY;

        // Update scrollbar with correct range
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = pd->totalContentH;
        int visH = h - startY - STATUS_BAR_H;
        if (visH < 1) visH = 1;
        si.nPage = visH;
        si.nPos = pd->scrollPos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        // Clamp scrollPos after range update
        int maxScroll = pd->totalContentH - visH;
        if (maxScroll < 0) maxScroll = 0;
        if (pd->scrollPos > maxScroll) pd->scrollPos = maxScroll;

        // Clip to card area and draw
        g.SetClip(Gdiplus::Rect(0, startY, w, h - startY - STATUS_BAR_H));

        // Draw only visible cards
        for (auto& cl : pd->layouts) {
            int drawY = cl.cardRect.top - pd->scrollPos;
            int cardBottomVisible = drawY + (cl.cardRect.bottom - cl.cardRect.top);

            if (cardBottomVisible < startY || drawY > h - STATUS_BAR_H) continue;
            if (drawY < startY) drawY = startY;

            ClipEntry& e = pd->filtered[cl.entryIndex];
            int ch = card_height(e);

            // Card background
            Gdiplus::Rect cardRect(cl.cardRect.left, drawY, cl.cardRect.right - cl.cardRect.left, ch);
            Gdiplus::SolidBrush cardBrush(CARD_BG);
            g.FillRectangle(&cardBrush, cardRect);

            // Left accent for pinned items
            if (e.pinned) {
                Gdiplus::SolidBrush pinAccent(PINNED_COLOR);
                g.FillRectangle(&pinAccent, cl.cardRect.left, drawY, 3, ch);
            }

            // Pin icon
            {
                Gdiplus::Font pf(L"Segoe UI", 14);
                Gdiplus::SolidBrush pinBrush(e.pinned ? PINNED_COLOR : DIM_COLOR);
                Gdiplus::StringFormat psf;
                psf.SetAlignment(Gdiplus::StringAlignmentCenter);
                psf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(e.pinned ? L"◆" : L"○", -1, &pf,
                    Gdiplus::RectF(cl.cardRect.left + 4, drawY + 4, 28, 24), &psf, &pinBrush);
            }

            // Timestamp
            {
                char timeBuf[32];
                struct tm tmInfo;
                localtime_s(&tmInfo, &e.created_at);
                strftime(timeBuf, sizeof(timeBuf), "%m-%d %H:%M", &tmInfo);
                wchar_t wtime[32];
                MultiByteToWideChar(CP_ACP, 0, timeBuf, -1, wtime, 32);

                Gdiplus::Font tf(L"Segoe UI", 10);
                Gdiplus::SolidBrush dimBrush(DIM_COLOR);
                g.DrawString(wtime, -1, &tf, Gdiplus::PointF(cl.cardRect.left + 42, drawY + 8), &dimBrush);
            }

            // Content preview
            if (e.content_type == 0) {
                // Text preview — show first 80 chars
                std::wstring preview = e.text.substr(0, 80);
                // Replace newlines with spaces for preview
                for (auto& c : preview) if (c == '\n' || c == '\r') c = ' ';
                if (e.text.size() > 80) preview += L"...";

                Gdiplus::Font cf(L"Segoe UI", 12);
                Gdiplus::SolidBrush textBrush(TEXT_COLOR);
                Gdiplus::RectF textArea(cl.cardRect.left + 42, drawY + 26,
                    cardW - 42 - DELETE_BTN_WIDTH - 16, 20);
                g.DrawString(preview.c_str(), -1, &cf, textArea, nullptr, &textBrush);
            } else if (e.content_type == 2) {
                // File paths — show first path as preview
                std::wstring preview;
                size_t nl = e.text.find(L'\n');
                if (nl != std::wstring::npos)
                    preview = e.text.substr(0, nl);
                else
                    preview = e.text;
                // Extract filename from path
                size_t bs = preview.rfind(L'\\');
                std::wstring fname = (bs != std::wstring::npos) ? preview.substr(bs + 1) : preview;
                if (fname.size() > 60) fname = fname.substr(0, 60) + L"...";

                Gdiplus::Font cf(L"Segoe UI", 12);
                Gdiplus::SolidBrush textBrush(TEXT_COLOR);
                Gdiplus::RectF textArea(cl.cardRect.left + 42 + 100, drawY + 26,
                    cardW - 42 - 100 - DELETE_BTN_WIDTH - 16, 20);
                g.DrawString(fname.c_str(), -1, &cf, textArea, nullptr, &textBrush);

                // File icon label — draw on content line, before filename
                Gdiplus::Font ff(L"Segoe UI", 10);
                Gdiplus::SolidBrush dimBrush(DIM_COLOR);
                size_t fileCount = 1;
                for (auto c : e.text) if (c == L'\n') fileCount++;
                wchar_t countLabel[32];
                swprintf_s(countLabel, L"[文件] %zu个", fileCount);
                Gdiplus::RectF labelArea(cl.cardRect.left + 42, drawY + 26,
                    100, 20);
                g.DrawString(countLabel, -1, &ff, labelArea, nullptr, &dimBrush);
            } else {
                // Image thumbnail \u2014 use thumb_data (small) for fast rendering
                if (!e.thumb_data.empty()) {
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, e.thumb_data.size());
                    CopyMemory(GlobalLock(hMem), e.thumb_data.data(), e.thumb_data.size());
                    GlobalUnlock(hMem);
                    IStream* pStr = nullptr;
                    CreateStreamOnHGlobal(hMem, TRUE, &pStr);
                    Gdiplus::Bitmap* thumb = Gdiplus::Bitmap::FromStream(pStr);
                    if (thumb && thumb->GetLastStatus() == Gdiplus::Ok) {
                        // Scale to fit thumbnail area (below timestamp)
                        float scaleW = (float)THUMB_SIZE / thumb->GetWidth();
                        float scaleH = (float)(ch - 26) / thumb->GetHeight();
                        float scale = (scaleW < scaleH) ? scaleW : scaleH;
                        int dw = (int)(thumb->GetWidth() * scale);
                        int dh = (int)(thumb->GetHeight() * scale);
                        g.DrawImage(thumb, cl.cardRect.left + 42, drawY + 22, dw, dh);
                    }
                    if (thumb) delete thumb;
                    pStr->Release();
                }
                Gdiplus::Font cf(L"Segoe UI", 10);
                Gdiplus::SolidBrush dimBrush(DIM_COLOR);
                g.DrawString(L"[\u56FE\u7247]", -1, &cf,
                    Gdiplus::PointF(cl.cardRect.left + 42 + THUMB_SIZE + 8, drawY + 30), &dimBrush);
            }

            // Delete button
            {
                Gdiplus::Font df(L"Segoe UI", 12, Gdiplus::FontStyleBold);
                Gdiplus::SolidBrush delBrush(DELETE_COLOR);
                int dx = cl.cardRect.right - DELETE_BTN_WIDTH - CARD_PADDING;
                Gdiplus::StringFormat dsf;
                dsf.SetAlignment(Gdiplus::StringAlignmentCenter);
                dsf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                g.DrawString(L"×", -1, &df,
                    Gdiplus::RectF((Gdiplus::REAL)dx, (Gdiplus::REAL)(drawY + 4),
                        (Gdiplus::REAL)DELETE_BTN_WIDTH, (Gdiplus::REAL)(ch - 8)), &dsf, &delBrush);
            }

            // Card shadow/border — bottom line only for simplicity
            Gdiplus::Pen cardBorder(SEPARATOR_COLOR, 0.5f);
            g.DrawLine(&cardBorder, cl.cardRect.left, drawY + ch,
                cl.cardRect.right, drawY + ch);
        }

        // Status bar
        g.SetClip(Gdiplus::Rect(0, 0, w, h));
        Gdiplus::Pen statusLine(SEPARATOR_COLOR, 1.0f);
        g.DrawLine(&statusLine, 0, h - STATUS_BAR_H, w, h - STATUS_BAR_H);

        wchar_t status[128];
        swprintf_s(status, L"\u5171 %zu \u6761\u8BB0\u5F55  |  \u53CC\u51FB\u5361\u7247\u590D\u5236\u5230\u526A\u8D34\u677F",
            pd->filtered.size());
        Gdiplus::Font statusFont(L"Segoe UI", 10);
        Gdiplus::SolidBrush statusBrush(DIM_COLOR);
        g.DrawString(status, -1, &statusFont, Gdiplus::PointF(12, h - STATUS_BAR_H + 4), &statusBrush);

        // "\u6E05\u7A7A\u5168\u90E8" button (right side of status bar)
        {
            const wchar_t* clearText = L"\u6E05\u7A7A\u5168\u90E8";
            Gdiplus::Font clearFont(L"Segoe UI", 10, Gdiplus::FontStyleUnderline);
            Gdiplus::SolidBrush clearBrush(DELETE_COLOR);
            Gdiplus::RectF clearLayout((Gdiplus::REAL)(w - 80), (Gdiplus::REAL)(h - STATUS_BAR_H + 3),
                (Gdiplus::REAL)70, (Gdiplus::REAL)(STATUS_BAR_H - 3));
            Gdiplus::StringFormat clearSf;
            clearSf.SetAlignment(Gdiplus::StringAlignmentFar);
            g.DrawString(clearText, -1, &clearFont, clearLayout, &clearSf, &clearBrush);
            // Track button area for click detection
            pd->clearAllRect = { w - 80, h - STATUS_BAR_H, w, h };
        }

        // Blit to screen
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        if (pd) {
            if (pd->hFont) DeleteObject(pd->hFont);
            if (pd->hSmallFont) DeleteObject(pd->hSmallFont);
            if (pd->hBoldFont) DeleteObject(pd->hBoldFont);
            delete pd;
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── public API ───────────────────────────────────────────────────────

static const wchar_t* CARD_PANEL_CLASS = L"ClipboardHistoryCardPanel";

void card_panel_register_class(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CardPanelWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Handled in WM_PAINT
    wc.lpszClassName = CARD_PANEL_CLASS;
    RegisterClassExW(&wc);
}

HWND card_panel_create(HWND parent, int id, HINSTANCE hInst) {
    return CreateWindowExW(0, CARD_PANEL_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        0, 0, 400, 300, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
}

void card_panel_reload(HWND hwnd) {
    CardPanelData* pd = (CardPanelData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (pd) {
        reload_data(pd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void card_panel_set_search(HWND hwnd, const wchar_t* text) {
    CardPanelData* pd = (CardPanelData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (pd && pd->hSearchEdit) {
        SetWindowTextW(pd->hSearchEdit, text);
    }
}
