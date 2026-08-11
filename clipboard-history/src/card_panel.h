#pragma once
#include <windows.h>

// Register the card panel window class. Call once before creating panels.
void card_panel_register_class(HINSTANCE hInst);

// Create a card panel child window.
// parent: parent window handle.
// id: child window ID.
// Returns the HWND of the card panel.
HWND card_panel_create(HWND parent, int id, HINSTANCE hInst);

// Tell the panel to reload data from the database and repaint.
void card_panel_reload(HWND hwnd);

// Set the search filter text.
void card_panel_set_search(HWND hwnd, const wchar_t* text);
