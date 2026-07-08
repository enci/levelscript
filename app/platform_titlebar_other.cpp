#include "platform_titlebar.hpp"

namespace ls::platform {

void set_titlebar(SDL_Window*, float, float, float, bool) {
    // Linux + other: WM owns the title bar appearance, no portable way to tint it.
}

}  // namespace ls::platform
