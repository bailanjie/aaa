#include "database.h"
#include "../vendor/sqlite3.h"
#include <windows.h>
#include <cstdio>

// ponytail: single global handle, per-connection pool if contention ever matters
static sqlite3* g_db = nullptr;

// ── helpers ──────────────────────────────────────────────────────────

static int count_callback(void* data, int argc, char** argv, char**) {
    if (argc > 0 && argv[0]) *(int*)data = atoi(argv[0]);
    return 0;
}

static int exists_callback(void* data, int argc, char** argv, char**) {
    *(bool*)data = (argc > 0 && argv[0]);
    return 0;
}

// ── public API ───────────────────────────────────────────────────────

bool db_init(const wchar_t* dbPath) {
    char pathUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, dbPath, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);

    int rc = sqlite3_open(pathUtf8, &g_db);
    if (rc != SQLITE_OK) return false;

    // WAL mode for better concurrent read performance
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    const char* createSql =
        "CREATE TABLE IF NOT EXISTS clipboard_history ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  content_type INTEGER NOT NULL,"
        "  text_content TEXT,"
        "  image_blob   BLOB,"
        "  thumb_blob   BLOB,"
        "  created_at   INTEGER NOT NULL,"
        "  pinned       INTEGER DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_created_at ON clipboard_history(created_at);";

    rc = sqlite3_exec(g_db, createSql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) return false;

    // Migration: add thumb_blob column for existing databases
    sqlite3_exec(g_db,
        "ALTER TABLE clipboard_history ADD COLUMN thumb_blob BLOB;",
        nullptr, nullptr, nullptr);  // ignore error if column already exists

    // Clean entries older than 30 days (unpinned only)
    char expireSql[256];
    snprintf(expireSql, sizeof(expireSql),
        "DELETE FROM clipboard_history WHERE created_at < %lld AND pinned = 0;",
        (long long)(time(nullptr) - 30 * 24 * 3600));
    sqlite3_exec(g_db, expireSql, nullptr, nullptr, nullptr);

    return true;
}

int db_insert_text(const std::wstring& text) {
    if (!g_db) return -1;

    // Convert to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], len, nullptr, nullptr);

    // Skip empty text
    if (utf8.empty()) return -1;

    const char* sql = "INSERT INTO clipboard_history (content_type, text_content, created_at) VALUES (0, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, utf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (long long)time(nullptr));

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return id;
}

int db_insert_files(const std::vector<std::wstring>& paths) {
    if (!g_db) return -1;
    if (paths.empty()) return -1;

    // Join paths with newlines for storage, first path for dedup preview
    std::wstring joined;
    for (size_t i = 0; i < paths.size(); i++) {
        if (i > 0) joined += L'\n';
        joined += paths[i];
    }

    // Convert to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, joined.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, joined.c_str(), -1, &utf8[0], len, nullptr, nullptr);

    const char* sql = "INSERT INTO clipboard_history (content_type, text_content, created_at) VALUES (2, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, utf8.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (long long)time(nullptr));

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return id;
}

int db_insert_image(const uint8_t* data, size_t size,
                    const uint8_t* thumb_data, size_t thumb_size) {
    if (!g_db) return -1;
    if (size == 0) return -1;

    // Dedup: skip if same size as latest image (ponytail: simple size check, hash if collisions matter)
    sqlite3_stmt* checkStmt;
    const char* checkSql = "SELECT image_blob FROM clipboard_history WHERE content_type=1 ORDER BY id DESC LIMIT 1";
    if (sqlite3_prepare_v2(g_db, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            int blobSize = sqlite3_column_bytes(checkStmt, 0);
            if (blobSize == (int)size) {
                sqlite3_finalize(checkStmt);
                return -1; // likely same image, skip
            }
        }
        sqlite3_finalize(checkStmt);
    }

    const char* sql = "INSERT INTO clipboard_history (content_type, image_blob, thumb_blob, created_at)"
                      " VALUES (1, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_blob(stmt, 1, data, (int)size, SQLITE_TRANSIENT);
    if (thumb_data && thumb_size > 0)
        sqlite3_bind_blob(stmt, 2, thumb_data, (int)thumb_size, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_int64(stmt, 3, (long long)time(nullptr));

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
        id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<ClipEntry> db_get_history(const std::wstring& search,
    time_t timeFrom, time_t timeTo, int typeFilter) {
    std::vector<ClipEntry> results;
    if (!g_db) return results;

    // Build query: pinned first, then by id desc (newest first)
    // IMPORTANT: load thumb_blob (small) for list display, NOT image_blob (large).
    // Full image is loaded on-demand via db_get_image_blob() when user double-clicks.

    // Build WHERE clauses
    std::string where;
    if (!search.empty()) {
        char searchUtf8[512];
        WideCharToMultiByte(CP_UTF8, 0, search.c_str(), -1, searchUtf8, sizeof(searchUtf8), nullptr, nullptr);
        char escaped[1024];
        char* q = escaped;
        *q++ = '%';
        for (char* p = searchUtf8; *p; p++) {
            if (*p == '\'' || *p == '%' || *p == '_') *q++ = '\\';
            *q++ = *p;
        }
        *q++ = '%';
        *q = '\0';
        char clause[1536];
        snprintf(clause, sizeof(clause), "text_content LIKE '%s' ESCAPE '\\'", escaped);
        where = clause;
    }
    if (timeFrom > 0) {
        if (!where.empty()) where += " AND ";
        char clause[64];
        snprintf(clause, sizeof(clause), "created_at >= %lld", (long long)timeFrom);
        where += clause;
    }
    if (timeTo > 0) {
        if (!where.empty()) where += " AND ";
        char clause[64];
        snprintf(clause, sizeof(clause), "created_at <= %lld", (long long)timeTo);
        where += clause;
    }
    if (typeFilter > 0) {
        if (!where.empty()) where += " AND ";
        // typeFilter: 1=text, 2=image, 3=files
        char clause[32];
        snprintf(clause, sizeof(clause), "content_type = %d", typeFilter - 1);
        where += clause;
    }

    char buf[2560];
    if (where.empty()) {
        snprintf(buf, sizeof(buf),
            "SELECT id, content_type, text_content, thumb_blob, created_at, pinned "
            "FROM clipboard_history ORDER BY pinned DESC, id DESC LIMIT 2000;");
    } else {
        snprintf(buf, sizeof(buf),
            "SELECT id, content_type, text_content, thumb_blob, created_at, pinned "
            "FROM clipboard_history WHERE %s "
            "ORDER BY pinned DESC, id DESC LIMIT 500;",
            where.c_str());
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, buf, -1, &stmt, nullptr) != SQLITE_OK) return results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClipEntry e;
        e.id = sqlite3_column_int(stmt, 0);
        e.content_type = sqlite3_column_int(stmt, 1);

        if (e.content_type == 0 || e.content_type == 2) {
            const char* txt = (const char*)sqlite3_column_text(stmt, 2);
            if (txt) {
                // Convert UTF-8 back to wide string
                int wlen = MultiByteToWideChar(CP_UTF8, 0, txt, -1, nullptr, 0);
                e.text.resize(wlen - 1);
                MultiByteToWideChar(CP_UTF8, 0, txt, -1, &e.text[0], wlen);
            }
        }
        if (e.content_type == 1) {
            const uint8_t* blob = (const uint8_t*)sqlite3_column_blob(stmt, 3);
            int blobSize = sqlite3_column_bytes(stmt, 3);
            if (blob && blobSize > 0)
                e.thumb_data.assign(blob, blob + blobSize);
        }

        e.created_at = (time_t)sqlite3_column_int64(stmt, 4);
        e.pinned = sqlite3_column_int(stmt, 5) != 0;
        results.push_back(std::move(e));
    }

    sqlite3_finalize(stmt);
    return results;
}

void db_delete_all_unpinned() {
    if (!g_db) return;
    sqlite3_exec(g_db, "DELETE FROM clipboard_history WHERE pinned = 0;",
        nullptr, nullptr, nullptr);
}

void db_delete_entry(int id) {
    if (!g_db) return;
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM clipboard_history WHERE id=%d;", id);
    sqlite3_exec(g_db, sql, nullptr, nullptr, nullptr);
}

void db_toggle_pin(int id) {
    if (!g_db) return;
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE clipboard_history SET pinned = 1 - pinned WHERE id=%d;", id);
    sqlite3_exec(g_db, sql, nullptr, nullptr, nullptr);
}

ClipEntry db_get_latest() {
    ClipEntry e = {};
    if (!g_db) return e;

    const char* sql = "SELECT id, content_type, text_content, created_at, pinned "
                      "FROM clipboard_history ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            e.id = sqlite3_column_int(stmt, 0);
            e.content_type = sqlite3_column_int(stmt, 1);
            if (e.content_type == 0) {
                const char* txt = (const char*)sqlite3_column_text(stmt, 2);
                if (txt) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, txt, -1, nullptr, 0);
                    e.text.resize(wlen - 1);
                    MultiByteToWideChar(CP_UTF8, 0, txt, -1, &e.text[0], wlen);
                }
            }
            e.created_at = (time_t)sqlite3_column_int64(stmt, 3);
            e.pinned = sqlite3_column_int(stmt, 4) != 0;
        }
        sqlite3_finalize(stmt);
    }
    return e;
}

std::vector<uint8_t> db_get_image_blob(int id) {
    std::vector<uint8_t> result;
    if (!g_db) return result;

    const char* sql = "SELECT image_blob FROM clipboard_history WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const uint8_t* blob = (const uint8_t*)sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        if (blob && blobSize > 0)
            result.assign(blob, blob + blobSize);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::wstring> db_get_file_paths(int id) {
    std::vector<std::wstring> result;
    if (!g_db) return result;

    const char* sql = "SELECT text_content FROM clipboard_history WHERE id=? AND content_type=2;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = (const char*)sqlite3_column_text(stmt, 0);
        if (txt) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, txt, -1, nullptr, 0);
            std::wstring joined(wlen - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, txt, -1, &joined[0], wlen);
            // Split by newlines
            size_t pos = 0;
            while (pos < joined.size()) {
                size_t end = joined.find(L'\n', pos);
                if (end == std::wstring::npos) end = joined.size();
                result.push_back(joined.substr(pos, end - pos));
                pos = end + 1;
            }
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

void db_close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}
