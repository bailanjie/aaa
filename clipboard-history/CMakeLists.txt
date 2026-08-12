cmake_minimum_required(VERSION 3.16)
project(ClipboardHistory LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SQLite amalgamation — compiled as C
add_library(sqlite3 STATIC vendor/sqlite3.c)
target_include_directories(sqlite3 PUBLIC vendor)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
)

# Configure SQLite for Windows
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
endif()

# Main application sources
set(SOURCES
    src/main.cpp
    src/database.cpp
    src/clipboard_monitor.cpp
    src/card_panel.cpp
    resource.rc
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    vendor
    .
)

# Unicode app: make unsuffixed Win32 APIs (DefWindowProc, LoadIcon, …) resolve
# to the wide (W) variants to match the L"" strings used everywhere.
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

target_link_libraries(${PROJECT_NAME} PRIVATE
    sqlite3
    gdiplus
    comctl32
    shlwapi
    shell32
    ole32
)

# Windows-specific settings
if(MSVC)
    # /utf-8: treat source files as UTF-8, emit UTF-8 string literals
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    # Static CRT: no VC++ redistributable needed, truly single-file exe
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
    # Link as Windows GUI app (no console)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# Install rule (optional, for packaging)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
