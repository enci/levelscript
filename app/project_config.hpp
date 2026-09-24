#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ls {

enum class layer_mode { text, color, tile, heatmap };

struct layer_config {
    std::string name;
    bool        visible{true};
    float       opacity{1.0f};
    layer_mode  mode{layer_mode::text};
    // Name of the tileset_config this layer's Tile mode draws from ("" =
    // none / falls back to the gray placeholder). Keyed by name like
    // tag_colors below, so renaming a tileset in Settings breaks the
    // reference - re-pick it there if that happens.
    std::string tileset;
};

struct tileset_config {
    std::string name{"Tileset"};
    std::string path;   // relative to the .ls file
    int         tile_w{16};
    int         tile_h{16};
};

// Per-project debugger settings, persisted in a sidecar JSON next to the
// script (<file>.ls.json). Keyed by names only, so it survives recompiles
// and needs no internal compiler types.
struct project_config {
    // 1.0 = one screen pixel per source pixel at base_cell_px - a natural
    // "1x" for point-sampled pixel art (section on tile_texture::sync()).
    static constexpr float        base_cell_px{16.f};
    float                         cell_zoom{1.f};
    float                         cell_px() const { return cell_zoom * base_cell_px; }
    float                         mini_px{24.f};
    uint32_t                      grid_bg_color{0xFF282828};   // 0xAABBGGRR
    std::vector<tileset_config>   tilesets;
    std::vector<layer_config>     layers;   // composite order: index 0 = bottom
    // tag_name -> value_id -> 0xAABBGGRR
    std::unordered_map<std::string, std::unordered_map<int, uint32_t>> tag_colors;

    // Load from <ls_path>.json; builds defaults from layer_names if absent.
    static project_config load(std::string const& ls_path,
                               std::vector<std::string> const& layer_names);
    void save(std::string const& ls_path) const;

    // Rebuild the layer list in the script's declared order, preserving
    // per-layer settings by name; dropped layers vanish, new ones default.
    void sync_layers(std::vector<std::string> const& layer_names);

    tileset_config const* find_tileset(std::string const& name) const;

    static std::string json_path(std::string const& ls_path);
};

}  // namespace ls
