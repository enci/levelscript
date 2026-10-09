#pragma once

struct SDL_Window;

namespace ls::platform {

// Tint the OS-drawn title bar / window border to match the app theme.
// rgb in [0,1]; dark_appearance picks light/dark caption text and border style.
// macOS: NSWindow backgroundColor + appearance.
// Windows: DwmSetWindowAttribute (Win10 1809+ for dark mode, Win11 22000+ for color).
// Linux/other: no-op (the WM owns the title bar appearance).
void set_titlebar(SDL_Window* w, float r, float g, float b, bool dark_appearance);

// Set the application (Dock) icon from an image file, as early as possible:
// the debugger is a plain executable, not an .app bundle, so until this runs
// macOS shows the generic executable icon. Call after SDL_Init.
// macOS: NSApp applicationIconImage. Elsewhere: no-op (the window icon is
// set with SDL_SetWindowIcon once the window exists).
void set_app_icon(char const* image_path);

}  // namespace ls::platform
