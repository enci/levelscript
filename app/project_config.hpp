#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ls {

struct directive_set;

enum class layer_mode { text, color, tile, heatmap, shape, corner, border };

// Marker shape a tag value can be given (used by the shape layer mode).
// Persisted by name, so the enum can grow without breaking old sidecars.
enum class tag_shape { none, circle, ring, square, diamond, triangle, cross, star, hexagon };
constexpr int k_tag_shape_count = 9;
char const* shape_name(tag_shape s);
tag_shape   shape_from_name(std::string const& s);

// Corner-triangle leg length as a fraction of the cell.
enum class corner_size { normal, small, tiny };

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
    corner_size corner{corner_size::normal};   // Corner mode only
    // The grid's @view (spec B.8) that mode and visible were chosen under ("":
    // none); nullopt until known. When the script's @view changes, mode and
    // visible go back to what it gives (apply_views).
    std::optional<std::string> view;
    // The mode and visibility the script's @view gives; only what differs is saved.
    layer_mode  base_mode{layer_mode::text};
    bool        base_visible{true};
};

// A grid's starting view, from its @view or the default (spec B.8).
struct layer_view {
    std::string name;
    std::string view;      // the @view as written; "": none
    layer_mode  mode{layer_mode::text};
    bool        visible{true};
};

struct tileset_config {
    std::string name{"Tileset"};
    std::string path;   // relative to the .lvs file
    int         tile_w{16};
    int         tile_h{16};
};

// Per-project debugger settings, persisted in a sidecar JSON next to the
// script (<file>.lvs.json). Keyed by names only, so it survives recompiles
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
    // Graph layers overlaid on the viewport, by name; hidden by default.
    std::vector<std::string>      shown_graphs;
    bool graph_shown(std::string const& name) const {
        for (auto const& g : shown_graphs) if (g == name) return true;
        return false;
    }
    // tag_name -> value_id -> 0xAABBGGRR
    std::unordered_map<std::string, std::unordered_map<int, uint32_t>> tag_colors;
    // tag_name -> value_id -> shape; absent means tag_shape::none
    std::unordered_map<std::string, std::unordered_map<int, tag_shape>> tag_shapes;

    // Input param overrides that differ from the script's @param directives:
    // a value, or nullopt for "use the declared default" (a @param unset).
    std::map<std::string, std::optional<int>> params;

    // Load from <ls_path>.json; builds defaults from layer_names if absent.
    static project_config load(std::string const& ls_path,
                               std::vector<std::string> const& layer_names);
    void save(std::string const& ls_path) const;

    // Rebuild the layer list in the script's declared order, preserving
    // per-layer settings by name; dropped layers vanish, new ones default.
    void sync_layers(std::vector<std::string> const& layer_names);

    // Record each layer's starting view; a layer whose mode and visibility
    // were chosen under another @view (or never) takes it.
    void apply_views(std::vector<layer_view> const& views);

    // Each grid's starting view from the script's directives: its @view, else
    // fill for the first grid and shape for the rest. `grids`: name and
    // whether it is a grid of number, in script order.
    static std::vector<layer_view> views_of(directive_set const& dirs,
        std::vector<std::pair<std::string, bool>> const& grids);

    tileset_config const* find_tileset(std::string const& name) const;

    static std::string json_path(std::string const& ls_path);
};

}  // namespace ls
