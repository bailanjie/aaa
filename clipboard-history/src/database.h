#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

struct ClipEntry {
    int id;
    int content_type;       // 0 = text, 1 = image
    std::wstring text;
    std::vector<uint8_t> image_data;  // full PNG — loaded only on demand via db_get_image_blob()
    std::vector<uint8_t> thumb_data;  // small thumbnail PNG — loaded in list queries
    time_t created_at;
    bool pinned;
};

// Initialize database, create tables, clean expired entries.
// dbPath: full path to the SQLite file.
// Returns true on success.
bool db_init(const wchar_t* dbPath);

// Insert a text entry. Returns new row id, or -1 on failure.
int db_insert_text(const std::wstring& text);

// Insert an image entry (full PNG + thumbnail PNG). Returns new row id, or -1 on failure.
int db_insert_image(const uint8_t* data, size_t size,
                    const uint8_t* thumb_data, size_t thumb_size);

// Get history entries, newest first, pinned on top.
// search: optional filter (case-insensitive, matches text_content).
// Returns all matching entries.
std::vector<ClipEntry> db_get_history(const std::wstring& search = L"");

// Delete a single entry by id.
void db_delete_entry(int id);

// Toggle pin state of an entry.
void db_toggle_pin(int id);

// Get the most recent entry (for dedup check).
ClipEntry db_get_latest();

// Load the full image blob for a single entry (on-demand, for copy-to-clipboard).
std::vector<uint8_t> db_get_image_blob(int id);

// Close database.
void db_close();
