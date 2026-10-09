#include "platform_titlebar.hpp"

#include <SDL3/SDL.h>
#import <Cocoa/Cocoa.h>

namespace ls::platform {

void set_titlebar(SDL_Window* w, float r, float g, float b, bool dark_appearance) {
    if (!w) return;
    SDL_PropertiesID props = SDL_GetWindowProperties(w);
    void* ns = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!ns) return;
    NSWindow* window = (__bridge NSWindow*)ns;

    window.titlebarAppearsTransparent = YES;
    window.backgroundColor = [NSColor colorWithSRGBRed:r green:g blue:b alpha:1.0];
    window.appearance = [NSAppearance appearanceNamed:
        dark_appearance ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

void set_app_icon(char const* image_path) {
    if (!image_path || !*image_path) return;
    NSImage* icon = [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:image_path]];
    if (icon) [NSApp setApplicationIconImage:icon];
}

}  // namespace ls::platform
