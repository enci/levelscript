#pragma once

struct SDL_Window;

namespace ls::platform {

// Tint the OS-drawn title bar / window border to match the app theme.
// rgb in [0,1]; dark_appearance picks light/dark caption text and border style.
// macOS: NSWindow backgroundColor + appearance.
// Windows: DwmSetWindowAttribute (Win10 1809+ for dark mode, Win11 22000+ for color).
// Linux/other: no-op (the WM owns the title bar appearance).
void set_titlebar(SDL_Window* w, float r, float g, float b, bool dark_appearance);

}  // namespace ls::platform
