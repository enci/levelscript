#include "platform_titlebar.hpp"

#include <SDL3/SDL.h>
#include <windows.h>
#include <dwmapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace ls::platform {

void set_titlebar(SDL_Window* w, float r, float g, float b, bool dark_appearance) {
    if (!w) return;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) return;

    BOOL dark = dark_appearance ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    auto to_byte = [](float v) -> BYTE {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return (BYTE)(v * 255.0f + 0.5f);
    };
    COLORREF color = RGB(to_byte(r), to_byte(g), to_byte(b));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color));
}

void set_app_icon(char const*) {}   // the window icon covers it (SDL_SetWindowIcon)

}  // namespace ls::platform
