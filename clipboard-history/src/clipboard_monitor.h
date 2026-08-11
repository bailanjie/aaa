#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

// Start listening for clipboard changes on the given window.
// hwnd: window that will receive WM_CLIPBOARDUPDATE.
bool clipboard_start(HWND hwnd);

// Stop listening.
void clipboard_stop(HWND hwnd);

// Read text from clipboard. Returns empty string if no text available.
std::wstring clipboard_read_text();

// Read image from clipboard as PNG bytes.
// If out_thumb is provided, also fills it with a small thumbnail PNG (max 120px wide).
// Returns empty vector if no image available.
std::vector<uint8_t> clipboard_read_image_png(std::vector<uint8_t>* out_thumb = nullptr);

// Read file paths from clipboard (CF_HDROP). Returns empty vector if no files.
std::vector<std::wstring> clipboard_read_files();

// Restore content to clipboard.
// After calling this, the content will be available for Ctrl+V.
// These automatically suppress the next WM_CLIPBOARDUPDATE to avoid re-logging.
bool clipboard_set_text(const std::wstring& text);
bool clipboard_set_image(const uint8_t* png_data, size_t size);
bool clipboard_set_files(const std::vector<std::wstring>& paths);

// Check/skip: called by the main window before processing WM_CLIPBOARDUPDATE.
// Returns true if this change was triggered by ourselves and should be skipped.
bool clipboard_consume_self_change();
