#pragma once
#include "ls.hpp"
#include <string>

namespace ls {

// Render a generated level to a PNG the way the debugger composites it, from
// the script's sidecar (<ls_path>.json): layers in script order, each by its
// mode, opacity and tileset, over grid_bg_color. Only the Tile and Color modes
// are drawn (the others need a font or are overlays); without a sidecar every
// tag layer is drawn in Color mode. Each cell is `scale` source pixels per
// tile pixel, or 8x8 pixels per cell for a layer without a tileset.
// Returns "" on success, else an error message.
std::string render_png(level const& lv, std::string const& ls_path,
                       std::string const& out_path, int scale);

}  // namespace ls
