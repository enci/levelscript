#include "project_config.hpp"
#include "directives.hpp"
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
        case layer_mode::shape:   return "shape";
        case layer_mode::corner:  return "corner";
        case layer_mode::border:  return "border";
    }
    return "text";
}

static std::string corner_str(corner_size c) {
    switch (c) {
        case corner_size::small: return "small";
        case corner_size::tiny:  return "tiny";
        default:                 return "normal";
    }
}

static corner_size corner_from_str(std::string const& s) {
    if (s == "small") return corner_size::small;
    if (s == "tiny")  return corner_size::tiny;
    return corner_size::normal;
}

static layer_mode mode_from_str(std::string const& s) {
    if (s == "color")   return layer_mode::color;
    if (s == "tile")    return layer_mode::tile;
    if (s == "heatmap") return layer_mode::heatmap;
    if (s == "shape")   return layer_mode::shape;
    if (s == "corner")  return layer_mode::corner;
    if (s == "border")  return layer_mode::border;
    return layer_mode::text;
}

static constexpr char const* k_shape_names[k_tag_shape_count] = {
    "none", "circle", "ring", "square", "diamond", "triangle", "cross", "star", "hexagon",
};

char const* shape_name(tag_shape s) { return k_shape_names[(int)s]; }

tag_shape shape_from_name(std::string const& s) {
    for (int i = 0; i < k_tag_shape_count; ++i)
        if (s == k_shape_names[i]) return (tag_shape)i;
    return tag_shape::none;   // unknown names (newer sidecar) degrade to none
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
    // tracks the .lvs `layers { }` block, not whatever order a stale sidecar
    // had). Existing per-layer settings are preserved by name.
    std::vector<layer_config> reordered;
    reordered.reserve(layer_names.size());
    for (auto const& name : layer_names) {
        layer_config lc;
        lc.name = name;
        for (auto const& old : layers)
            if (old.name == name) { lc = old; break; }
        reordered.push_back(lc);
    }
    layers = std::move(reordered);
}

// `tiles` is the Tile mode with the tileset the user picks (@tileset is not
// read yet); `ramp` and `tiles` suit only a grid of number, and are drawn as
// fill elsewhere (spec B.8); `hidden` starts the grid hidden.
std::vector<layer_view> project_config::views_of(directive_set const& dirs,
        std::vector<std::pair<std::string, bool>> const& grids) {
    std::vector<layer_view> out;
    for (size_t i = 0; i < grids.size(); ++i) {
        auto const& [name, number] = grids[i];
        layer_view v{ name, "", i == 0 ? layer_mode::color : layer_mode::shape, true };
        if (auto d = dirs.find("view", directive_target::layer, "", name)) {
            v.view = d->value;
            if      (v.view == "fill")   v.mode = layer_mode::color;
            else if (v.view == "shape")  v.mode = layer_mode::shape;
            else if (v.view == "corner") v.mode = layer_mode::corner;
            else if (v.view == "text")   v.mode = layer_mode::text;
            else if (v.view == "border") v.mode = layer_mode::border;
            else if (v.view == "ramp")   v.mode = number ? layer_mode::heatmap : layer_mode::color;
            else if (v.view == "tiles")  v.mode = number ? layer_mode::tile : layer_mode::color;
            else if (v.view == "hidden") v.visible = false;
        }
        out.push_back(std::move(v));
    }
    return out;
}

void project_config::apply_views(std::vector<layer_view> const& views) {
    for (auto& lc : layers)
        for (auto const& v : views) {
            if (v.name != lc.name) continue;
            lc.base_mode    = v.mode;
            lc.base_visible = v.visible;
            if (lc.view != v.view) {
                lc.mode    = v.mode;
                lc.visible = v.visible;
                lc.view    = v.view;
            }
            break;
        }
}

void project_config::resolve_tag_colors(
        std::unordered_map<std::string, std::vector<std::string>> const& slot_names) {
    tag_color_slots.clear();
    for (auto& [tname, vmap] : tag_colors) {
        auto sn = slot_names.find(tname);
        if (sn == slot_names.end()) continue;   // tag not in this script: kept, unused
        auto const& names = sn->second;
        auto slot_of = [&](std::string const& n) {
            for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int)i;
            return -1;
        };
        // Legacy index keys -> names. A name already set wins over its old index.
        std::vector<std::pair<std::string, uint32_t>> renamed;
        for (auto it = vmap.begin(); it != vmap.end();) {
            auto const& key = it->first;
            bool digits = key.find_first_not_of("0123456789") == std::string::npos;
            size_t idx = digits && key.size() < 9 ? std::stoul(key) : names.size();
            if (slot_of(key) < 0 && idx < names.size()) {
                renamed.emplace_back(names[idx], it->second);
                it = vmap.erase(it);
            } else {
                ++it;
            }
        }
        for (auto const& [n, col] : renamed) vmap.emplace(n, col);
        for (auto const& [n, col] : vmap)
            if (int s = slot_of(n); s >= 0) tag_color_slots[tname][s] = col;
    }
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
            if (j.contains("graphs") && j["graphs"].is_array())
                for (auto const& jg : j["graphs"])
                    if (jg.is_string()) cfg.shown_graphs.push_back(jg.get<std::string>());
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
            // "layers": {"<name>": {...}}; older sidecars wrote an array of
            // objects each carrying its "name". Order comes from the script.
            auto read_layer = [&](std::string name, json const& jl) {
                if (!jl.is_object() || name.empty()) return;
                layer_config lc;
                lc.name    = std::move(name);
                lc.visible = jget(jl, "visible", true);
                lc.opacity = jget(jl, "opacity", 1.0f);
                lc.mode    = mode_from_str(jget(jl, "mode", std::string{"text"}));
                lc.tileset = jget(jl, "tileset", std::string{});
                lc.corner  = corner_from_str(jget(jl, "corner", std::string{"normal"}));
                if (auto it = jl.find("view"); it != jl.end() && it->is_string())
                    lc.view = it->get<std::string>();
                cfg.layers.push_back(std::move(lc));
            };
            if (auto it = j.find("layers"); it != j.end()) {
                if (it->is_object())
                    for (auto const& [name, jl] : it->items()) read_layer(name, jl);
                else if (it->is_array())
                    for (auto const& jl : *it)
                        if (jl.is_object()) read_layer(jget(jl, "name", std::string{}), jl);
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
                        uint32_t col;
                        if (!key.empty() && val.is_string() &&
                            hex_to_color(val.get<std::string>(), col))
                            cfg.tag_colors[tname][key] = col;
                    }
                }
            }
            if (j.contains("params") && j["params"].is_object()) {
                for (auto const& [pname, jv] : j["params"].items()) {
                    if (jv.is_null())                cfg.params[pname] = std::nullopt;
                    else if (jv.is_number_integer()) cfg.params[pname] = jv.get<int>();
                }
            }
            if (j.contains("tag_shapes") && j["tag_shapes"].is_object()) {
                for (auto const& [tname, jvs] : j["tag_shapes"].items()) {
                    if (!jvs.is_object()) continue;
                    for (auto const& [key, val] : jvs.items()) {
                        char* end = nullptr;
                        long vid = std::strtol(key.c_str(), &end, 10);
                        if (end == key.c_str() || !val.is_string()) continue;
                        tag_shape sh = shape_from_name(val.get<std::string>());
                        if (sh != tag_shape::none) cfg.tag_shapes[tname][(int)vid] = sh;
                    }
                }
            }
        }
    }
    cfg.sync_layers(layer_names);
    return cfg;
}

// Only what differs from the defaults is written: a layer is matched by name
// and rebuilt in script order on load, so an all-default layer is left out,
// and a config that is all defaults has no sidecar (an old one is removed).
// A layer's mode and visibility count as default when they are what its @view
// gives; a chosen one is saved with that @view, and applies only while the
// script keeps it (apply_views). An entry without a view (an older sidecar)
// so never overrides the script.
void project_config::save(std::string const& ls_path) const {
    project_config const def;
    layer_config const   def_layer;
    json j = json::object();
    if (cell_zoom != def.cell_zoom)         j["cell_zoom"]     = cell_zoom;
    if (mini_px != def.mini_px)             j["mini_px"]       = mini_px;
    if (grid_bg_color != def.grid_bg_color) j["grid_bg_color"] = color_to_hex(grid_bg_color);
    for (auto const& ts : tilesets) {
        j["tilesets"].push_back({
            {"name", ts.name},
            {"path", ts.path},
            {"tile_w", ts.tile_w},
            {"tile_h", ts.tile_h},
        });
    }
    for (auto const& lc : layers) {
        json jl;
        // a chosen view, with the @view it was chosen under
        if (lc.mode != lc.base_mode || lc.visible != lc.base_visible) {
            jl["mode"]    = mode_str(lc.mode);
            jl["visible"] = lc.visible;
            jl["view"]    = lc.view.value_or("");
        }
        if (lc.opacity != def_layer.opacity) jl["opacity"] = lc.opacity;
        if (lc.tileset != def_layer.tileset) jl["tileset"] = lc.tileset;
        if (lc.corner  != def_layer.corner)  jl["corner"]  = corner_str(lc.corner);
        if (!jl.empty()) j["layers"][lc.name] = jl;
    }
    if (!shown_graphs.empty()) j["graphs"] = shown_graphs;
    for (auto const& [pname, v] : params)
        j["params"][pname] = v ? json(*v) : json(nullptr);
    for (auto const& [tname, vmap] : tag_colors)
        for (auto const& [vname, col] : vmap)
            j["tag_colors"][tname][vname] = color_to_hex(col);
    for (auto const& [tname, vmap] : tag_shapes)
        for (auto const& [vid, sh] : vmap)
            if (sh != tag_shape::none) j["tag_shapes"][tname][std::to_string(vid)] = shape_name(sh);

    std::string const file = json_path(ls_path);
    if (j.empty()) {
        std::remove(file.c_str());   // all defaults: no sidecar
        return;
    }
    std::ofstream f(file);
    f << j.dump(2) << '\n';
}

tileset_config const* project_config::find_tileset(std::string const& name) const {
    if (name.empty()) return nullptr;
    for (auto const& ts : tilesets)
        if (ts.name == name) return &ts;
    return nullptr;
}

}  // namespace ls
