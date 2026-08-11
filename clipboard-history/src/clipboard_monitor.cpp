#include "clipboard_monitor.h"
#include <gdiplus.h>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

// ── clipboard change listener ────────────────────────────────────────

bool clipboard_start(HWND hwnd) {
    return AddClipboardFormatListener(hwnd) != FALSE;
}

void clipboard_stop(HWND hwnd) {
    RemoveClipboardFormatListener(hwnd);
}

// ── read from clipboard ──────────────────────────────────────────────

std::wstring clipboard_read_text() {
    std::wstring result;
    if (!OpenClipboard(nullptr)) return result;

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pData = (wchar_t*)GlobalLock(hData);
            if (pData) {
                result = pData;
                GlobalUnlock(hData);
            }
        }
    }
    CloseClipboard();
    return result;
}

// Helper: get CLSID for an image encoder from its MIME type
static int get_encoder_clsid(const wchar_t* mimeType, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<uint8_t> buf(size);
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(num, size, codecs);

    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0) {
            *pClsid = codecs[i].Clsid;
            return 0;
        }
    }
    return -1;
}

// Save a Gdiplus::Bitmap to PNG bytes in memory
static std::vector<uint8_t> bitmap_to_png(Gdiplus::Bitmap* bmp) {
    std::vector<uint8_t> result;
    CLSID pngClsid;
    if (get_encoder_clsid(L"image/png", &pngClsid) != 0) return result;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 0);
    if (!hMem) return result;

    IStream* pngStream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pngStream) != S_OK) {
        GlobalFree(hMem);
        return result;
    }

    if (bmp->Save(pngStream, &pngClsid, nullptr) != Gdiplus::Ok) {
        pngStream->Release();
        return result;
    }

    STATSTG stat;
    pngStream->Stat(&stat, STATFLAG_NONAME);
    ULONG pngSize = stat.cbSize.LowPart;

    LARGE_INTEGER li = {};
    pngStream->Seek(li, STREAM_SEEK_SET, nullptr);

    result.resize(pngSize);
    pngStream->Read(result.data(), pngSize, nullptr);
    pngStream->Release();
    return result;
}

std::vector<uint8_t> clipboard_read_image_png(std::vector<uint8_t>* out_thumb) {
    std::vector<uint8_t> result;
    if (!OpenClipboard(nullptr)) return result;

    // Try CF_DIB first (most common), then CF_BITMAP
    HBITMAP hBmpOwned = nullptr;
    bool needDelete = false;

    if (IsClipboardFormatAvailable(CF_DIB)) {
        HANDLE hData = GetClipboardData(CF_DIB);
        if (hData) {
            LPBITMAPINFO pDib = (LPBITMAPINFO)GlobalLock(hData);
            if (pDib) {
                // Calculate color table offset
                // For BI_BITFIELDS (common on 32-bit), 3 DWORD masks follow the header
                int colorCount = 0;
                if (pDib->bmiHeader.biBitCount <= 8) {
                    colorCount = pDib->bmiHeader.biClrUsed
                        ? pDib->bmiHeader.biClrUsed
                        : (1 << pDib->bmiHeader.biBitCount);
                } else if (pDib->bmiHeader.biCompression == BI_BITFIELDS) {
                    colorCount = 3; // 3 DWORD bit masks
                }
                int paletteSize = colorCount * (int)sizeof(RGBQUAD);
                BYTE* pPixelData = (BYTE*)pDib + pDib->bmiHeader.biSize + paletteSize;

                HDC hdc = GetDC(nullptr);
                hBmpOwned = CreateDIBitmap(hdc, &pDib->bmiHeader, CBM_INIT,
                    pPixelData, pDib, DIB_RGB_COLORS);
                ReleaseDC(nullptr, hdc);
                needDelete = true;

                GlobalUnlock(hData);
            }
        }
    }

    // Fallback: CF_BITMAP (GDI bitmap handle)
    if (!hBmpOwned && IsClipboardFormatAvailable(CF_BITMAP)) {
        HBITMAP hClipBmp = (HBITMAP)GetClipboardData(CF_BITMAP);
        if (hClipBmp) {
            // Make a copy since clipboard owns the original
            BITMAP bm;
            GetObject(hClipBmp, sizeof(bm), &bm);
            HDC hdc = GetDC(nullptr);
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hClipBmp);
            hBmpOwned = CreateCompatibleBitmap(hdc, bm.bmWidth, bm.bmHeight);
            HDC hdcCopy = CreateCompatibleDC(hdc);
            HBITMAP hOldCopy = (HBITMAP)SelectObject(hdcCopy, hBmpOwned);
            BitBlt(hdcCopy, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcCopy, hOldCopy);
            DeleteDC(hdcCopy);
            SelectObject(hdcMem, hOld);
            DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdc);
            needDelete = true;
        }
    }

    CloseClipboard();

    if (!hBmpOwned) return result;

    // Convert HBITMAP → Gdi+ Bitmap → PNG bytes
    Gdiplus::Bitmap* gdiBmp = Gdiplus::Bitmap::FromHBITMAP(hBmpOwned, nullptr);
    if (gdiBmp && gdiBmp->GetLastStatus() == Gdiplus::Ok) {
        // Generate thumbnail while we have the GDI+ bitmap
        if (out_thumb) {
            int tw = gdiBmp->GetWidth();
            int th = gdiBmp->GetHeight();
            const int MAX_THUMB = 120;
            if (tw > MAX_THUMB) {
                th = th * MAX_THUMB / tw;
                tw = MAX_THUMB;
            }
            Gdiplus::Bitmap* thumbBmp = new Gdiplus::Bitmap(tw, th,
                gdiBmp->GetPixelFormat());
            Gdiplus::Graphics thumbG(thumbBmp);
            thumbG.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            thumbG.DrawImage(gdiBmp, 0, 0, tw, th);
            *out_thumb = bitmap_to_png(thumbBmp);
            delete thumbBmp;
        }
        result = bitmap_to_png(gdiBmp);
    }

    if (gdiBmp) delete gdiBmp;
    if (needDelete) DeleteObject(hBmpOwned);
    return result;
}

// ── self-change guard ────────────────────────────────────────────────

// ponytail: simple flag, atomic if multiple clipboard watchers ever added
static bool g_self_change = false;

bool clipboard_consume_self_change() {
    bool was = g_self_change;
    g_self_change = false;
    return was;
}

// ── write to clipboard ───────────────────────────────────────────────

bool clipboard_set_text(const std::wstring& text) {
    g_self_change = true;
    if (!OpenClipboard(nullptr)) { g_self_change = false; return false; }
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) { g_self_change = false; CloseClipboard(); return false; }

    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    wcscpy_s(pMem, text.size() + 1, text.c_str());
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem); // System didn't take ownership
        g_self_change = false;
    }
    CloseClipboard();
    return true;
}

bool clipboard_set_image(const uint8_t* png_data, size_t size) {
    g_self_change = true;
    if (!OpenClipboard(nullptr)) { g_self_change = false; return false; }
    EmptyClipboard();

    // Load PNG from memory into Gdi+ Bitmap
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) { g_self_change = false; CloseClipboard(); return false; }
    CopyMemory(GlobalLock(hMem), png_data, size);
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hMem, TRUE, &pStream);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
        if (bmp) delete bmp;
        pStream->Release();
        g_self_change = false;
        CloseClipboard();
        return false;
    }

    // Convert Gdi+ Bitmap → HBITMAP → DIB for clipboard
    HBITMAP hBmp = nullptr;
    Gdiplus::Color bg(0, 0, 0, 0); // transparent background
    if (bmp->GetHBITMAP(bg, &hBmp) != Gdiplus::Ok) {
        delete bmp; pStream->Release();
        g_self_change = false;
        CloseClipboard();
        return false;
    }

    BITMAP bm;
    GetObject(hBmp, sizeof(bm), &bm);
    int pixelBytes = bm.bmWidth * bm.bmHeight * 4; // 32-bit
    int dibSize = sizeof(BITMAPINFOHEADER) + pixelBytes;
    HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    if (!hDib) {
        DeleteObject(hBmp); delete bmp; pStream->Release();
        g_self_change = false;
        CloseClipboard();
        return false;
    }

    BYTE* pDib = (BYTE*)GlobalLock(hDib);
    BITMAPINFOHEADER* pBI = (BITMAPINFOHEADER*)pDib;
    pBI->biSize = sizeof(BITMAPINFOHEADER);
    pBI->biWidth = bm.bmWidth;
    pBI->biHeight = bm.bmHeight;
    pBI->biPlanes = 1;
    pBI->biBitCount = 32;
    pBI->biCompression = BI_RGB;
    pBI->biSizeImage = pixelBytes;

    HDC hdc = GetDC(nullptr);
    GetDIBits(hdc, hBmp, 0, bm.bmHeight, pDib + sizeof(BITMAPINFOHEADER),
        (BITMAPINFO*)pDib, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    GlobalUnlock(hDib);

    if (!SetClipboardData(CF_DIB, hDib)) {
        GlobalFree(hDib);
        g_self_change = false;
    }

    DeleteObject(hBmp);
    delete bmp;
    pStream->Release();
    CloseClipboard();
    return true;
}
