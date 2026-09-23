#include "project_config.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace ls {

// ── serialisation helpers ─────────────────────────────────────────────────────
//
// All reads are type-checked and default-valued (and parsing runs in
// non-throwing mode), so a malformed or stale sidecar degrades to defaults —
// the app never faults on bad input, per the project error philosophy.

using json = nlohmann::json;

static float jget(json const& j, char const* key, float def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? (float)*it : def;
}
static int jget(json const& j, char const* key, int def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_integer()) ? (int)*it : def;
}
static bool jget(json const& j, char const* key, bool def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? (bool)*it : def;
}
static std::string jget(json const& j, char const* key, std::string def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? (std::string)*it : def;
}

static std::string mode_str(layer_mode m) {
    switch (m) {
        case layer_mode::text:    return "text";
        case layer_mode::color:   return "color";
        case layer_mode::tile:    return "tile";
        case layer_mode::heatmap: return "heatmap";
    }
    return "text";
}

static layer_mode mode_from_str(std::string const& s) {
    if (s == "color")   return layer_mode::color;
    if (s == "tile")    return layer_mode::tile;
    if (s == "heatmap") return layer_mode::heatmap;
    return layer_mode::text;
}

// 0xAABBGGRR -> "rrggbb" (alpha dropped; every stored color is opaque).
static std::string color_to_hex(uint32_t col) {
    unsigned r = (col >> 0) & 0xFF;
    unsigned g = (col >> 8) & 0xFF;
    unsigned b = (col >> 16) & 0xFF;
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02x%02x%02x", r, g, b);
    return hex;
}

// "rrggbb" -> 0xFFBBGGRR; false on malformed input.
static bool hex_to_color(std::string const& s, uint32_t& out) {
    if (s.size() != 6) return false;
    char* end = nullptr;
    unsigned long hex = std::strtoul(s.c_str(), &end, 16);
    if (end != s.c_str() + 6) return false;
    uint32_t r = (hex >> 16) & 0xFF;
    uint32_t g = (hex >> 8) & 0xFF;
    uint32_t b = hex & 0xFF;
    out = (0xFFu << 24) | (b << 16) | (g << 8) | r;
    return true;
}

// ── project_config ────────────────────────────────────────────────────────────

std::string project_config::json_path(std::string const& ls_path) {
    return ls_path + ".json";
}

void project_config::sync_layers(std::vector<std::string> const& layer_names) {
    // Rebuild in the script's declared order every time (composite order
    // tracks the .ls `layers { }` block, not whatever order a stale sidecar
    // had). Existing per-layer settings are preserved by name.
    std::vector<layer_config> reordered;
    reordered.reserve(layer_names.size());
    for (auto const& name : layer_names) {
        layer_config lc{name};
        for (auto const& old : layers)
            if (old.name == name) { lc = old; break; }
        reordered.push_back(lc);
    }
    layers = std::move(reordered);
}

project_config project_config::load(std::string const& ls_path,
                                    std::vector<std::string> const& layer_names) {
    project_config cfg;
    std::ifstream f(json_path(ls_path));
    if (f.is_open()) {
        json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_object()) {
            if (j.contains("cell_zoom") && j["cell_zoom"].is_number()) {
                cfg.cell_zoom = jget(j, "cell_zoom", 1.f);
            } else {
                // Pre-zoom sidecar: derive zoom from the old absolute
                // cell_px setting so an existing project's view size holds.
                cfg.cell_zoom = jget(j, "cell_px", project_config::base_cell_px)
                              / project_config::base_cell_px;
            }
            cfg.mini_px = jget(j, "mini_px", 24.f);
            uint32_t bg;
            if (hex_to_color(jget(j, "grid_bg_color", std::string{}), bg))
                cfg.grid_bg_color = bg;
            bool migrate_tile_layers = false;
            if (j.contains("tilesets") && j["tilesets"].is_array()) {
                for (auto const& jt : j["tilesets"]) {
                    if (!jt.is_object()) continue;
                    cfg.tilesets.push_back({
                        jget(jt, "name", std::string{"Tileset"}),
                        jget(jt, "path", std::string{}),
                        jget(jt, "tile_w", 16),
                        jget(jt, "tile_h", 16),
                    });
                }
            } else if (j.contains("tileset") && j["tileset"].is_object()) {
                // Pre-multi-tileset sidecar: one global tileset implicitly
                // shared by every "tile" mode layer. Migrate it into a
                // single named entry and point those layers at it below.
                auto const& jt = j["tileset"];
                cfg.tilesets.push_back({
                    "Tileset",
                    jget(jt, "path", std::string{}),
                    jget(jt, "tile_w", 16),
                    jget(jt, "tile_h", 16),
                });
                migrate_tile_layers = true;
            }
            if (j.contains("layers") && j["layers"].is_array()) {
                for (auto const& jl : j["layers"]) {
                    if (!jl.is_object()) continue;
                    cfg.layers.push_back({
                        jget(jl, "name", std::string{}),
                        jget(jl, "visible", true),
                        jget(jl, "opacity", 1.0f),
                        mode_from_str(jget(jl, "mode", std::string{"text"})),
                        jget(jl, "tileset", std::string{}),
                    });
                }
            }
            if (migrate_tile_layers) {
                for (auto& lc : cfg.layers)
                    if (lc.mode == layer_mode::tile && lc.tileset.empty())
                        lc.tileset = "Tileset";
            }
            if (j.contains("tag_colors") && j["tag_colors"].is_object()) {
                for (auto const& [tname, jvc] : j["tag_colors"].items()) {
                    if (!jvc.is_object()) continue;
                    for (auto const& [key, val] : jvc.items()) {
                        char* end = nullptr;
                        long vid = std::strtol(key.c_str(), &end, 10);
                        uint32_t col;
                        if (end != key.c_str() && val.is_string() &&
                            hex_to_color(val.get<std::string>(), col))
                            cfg.tag_colors[tname][(int)vid] = col;
                    }
                }
            }
        }
    }
    cfg.sync_layers(layer_names);
    return cfg;
}

void project_config::save(std::string const& ls_path) const {
    json j;
    j["cell_zoom"]     = cell_zoom;
    j["mini_px"]       = mini_px;
    j["grid_bg_color"] = color_to_hex(grid_bg_color);
    j["tilesets"] = json::array();
    for (auto const& ts : tilesets) {
        j["tilesets"].push_back({
            {"name", ts.name},
            {"path", ts.path},
            {"tile_w", ts.tile_w},
            {"tile_h", ts.tile_h},
        });
    }
    j["layers"] = json::array();
    for (auto const& lc : layers) {
        j["layers"].push_back({
            {"name", lc.name},
            {"visible", lc.visible},
            {"opacity", lc.opacity},
            {"mode", mode_str(lc.mode)},
            {"tileset", lc.tileset},
        });
    }
    if (!tag_colors.empty()) {
        json jtc;
        for (auto const& [tname, vmap] : tag_colors) {
            json jvc;
            for (auto const& [vid, col] : vmap)
                jvc[std::to_string(vid)] = color_to_hex(col);
            jtc[tname] = jvc;
        }
        j["tag_colors"] = jtc;
    }
    std::ofstream f(json_path(ls_path));
    f << j.dump(2) << '\n';
}

tileset_config const* project_config::find_tileset(std::string const& name) const {
    if (name.empty()) return nullptr;
    for (auto const& ts : tilesets)
        if (ts.name == name) return &ts;
    return nullptr;
}

}  // namespace ls
