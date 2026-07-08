#pragma once
#include <cstdint>
#include <optional>
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
};

struct tileset_config {
    std::string path;   // relative to the .ls file
    int         tile_w{16};
    int         tile_h{16};
};

// Per-project debugger settings, persisted in a sidecar JSON next to the
// script (<file>.ls.json). Keyed by names only, so it survives recompiles
// and needs no internal compiler types.
struct project_config {
    float                         cell_px{16.f};
    float                         mini_px{24.f};
    uint32_t                      grid_bg_color{0xFF282828};   // 0xAABBGGRR
    std::optional<tileset_config> tileset;
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

    static std::string json_path(std::string const& ls_path);
};

}  // namespace ls
