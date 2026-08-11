#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

struct ClipEntry {
    int id;
    int content_type;       // 0 = text, 1 = image, 2 = files
    std::wstring text;      // file paths stored as semicolon-separated text, loaded on demand
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

// Insert file paths. paths are joined and stored as text.
int db_insert_files(const std::vector<std::wstring>& paths);

// Get history entries, newest first, pinned on top.
// search: optional text filter. timeFrom/timeTo: Unix timestamp range (0 = no limit).
// typeFilter: 0=all, 1=text, 2=image, 3=files.
std::vector<ClipEntry> db_get_history(const std::wstring& search = L"",
    time_t timeFrom = 0, time_t timeTo = 0, int typeFilter = 0);

// Delete a single entry by id.
void db_delete_entry(int id);

// Delete all unpinned entries. Pinned entries are kept.
void db_delete_all_unpinned();

// Toggle pin state of an entry.
void db_toggle_pin(int id);

// Get the most recent entry (for dedup check).
ClipEntry db_get_latest();

// Load the full image blob for a single entry (on-demand, for copy-to-clipboard).
std::vector<uint8_t> db_get_image_blob(int id);

// Load file paths for a single entry (on-demand, for copy-to-clipboard).
std::vector<std::wstring> db_get_file_paths(int id);

// Close database.
void db_close();
