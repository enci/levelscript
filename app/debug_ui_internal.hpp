#pragma once
// Shared internals of the lsd debugger, split across debug_ui.cpp (app state,
// main loop, controls) and debug_ui_views.cpp (grid composite, pattern
// previews, inspector windows).
//
// Execution boundary: everything that RUNS a script goes through the public
// API in ls.hpp (ls::generator / ls::run / ls::level). The internal
// headers (parser.hpp / sema.hpp) are included for DISPLAY METADATA ONLY --
// statement descriptions, rule pattern previews, tag value names. That data
// is read-only and never executes anything.

#include "ls.hpp"           // execution: compile/run/step/snapshot/highlights
#include "ast.hpp"          // metadata: statement + rule attribute display
#include "sema.hpp"         // metadata: compiled patterns / tag + layer tables
#include "project_config.hpp"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace ls {

// ── palette helpers ───────────────────────────────────────────────────────────

// Color for a tag value, stable for a given (tag_id, value_id) pair and
// matched to the same 12-color cycle the VS Code extension's decorations.ts
// paints that value with in the editor (light/dark chosen from the current
// ImGui style). Falls back to a hash-based color for non-tag cells (number
// grids, tag_id < 0). An explicit user color from the sidecar config wins
// over either when present.
ImU32 tag_color(int tag_id, int value_id,
                std::unordered_map<int, uint32_t> const* colors = nullptr);
ImU32 with_alpha(ImU32 col, float opacity);
ImU32 heatmap_color(float t, float opacity);

// The one ImU32 <-> float[3] conversion (color editors + config round trips).
void     col3_from_u32(uint32_t c, float out[3]);
uint32_t u32_from_col3(float const c[3]);

// A layer's display palette: its tagset id (-1 = number layer) and the user
// color overrides for that tagset, if any.
struct palette {
    int                                      tag_id{-1};
    std::unordered_map<int, uint32_t> const* colors{nullptr};
};
palette layer_palette(compiled const& meta, project_config const& cfg, int grid_id);

// Glyph for a display value: middle dot when empty, tag initial, or number.
std::string cell_glyph(compiled const& meta, int tag_id, int v);

// Text centered in a box_px square anchored at p0 (grid cells, pattern cells).
void draw_centered_text(ImDrawList* dl, ImVec2 p0, float box_px, ImU32 col,
                        char const* text);

// Filled marker centered on `c`, fitting a circle of radius `r`.
void draw_shape(ImDrawList* dl, ImVec2 c, float r, tag_shape shape, ImU32 col);

// ── script: one loaded .ls file ───────────────────────────────────────────────

struct script {
    std::string path;
    generator   gen;    // execution factory -- the public API surface
    ast_file    ast;    // display metadata only
    compiled    meta;   // display metadata only
    std::string status;       // one-line load status for the status bar
    std::string full_error;   // full diagnostics of a failed load
    bool        ok{false};

    std::vector<std::string> layer_names() const {
        std::vector<std::string> names;
        for (auto const& l : meta.layers) names.push_back(l.name);
        return names;
    }
};

// (Re)load from disk: public compile for execution, plus one parse+analyze
// for the display metadata. On failure the old gen/ast/meta stay in place.
bool load_script(script& sc);

// ── debug_run: one in-flight progressive run ─────────────────────────────────
//
// A thin stepper over ls::run, started at application granularity with
// the observe channel on. This is the ONLY stepping mechanism: one pulled
// event is one rule application or one statement boundary, mid-batch grids
// come from snapshot(), highlights from highlights(), restart is a new
// run(). The snapshot/highlight copies are cached and refreshed once per
// action rather than per frame.

struct debug_run {
    run                          gen;
    level                       snap;
    std::vector<cell_highlight> hls;
    int  stmt_count{0};
    bool started{false};
    bool done{false};
    int  apps_in_stmt{0};    // applications of the statement being worked on
    int  counted_stmt{-1};
    std::vector<stmt_frame> counted_stack;   // leaf position apps_in_stmt counts for

    void restart(generator const& g, uint64_t seed) {
        gen = g.run(seed, step_mode::application, observe::on);
        stmt_count = g.statement_count();
        started = false;
        done = false;
        apps_in_stmt = 0;
        counted_stmt = -1;
        refresh();
    }

    void refresh() {
        snap = gen.snapshot();
        hls  = gen.highlights();
    }

    // Pull one event; false when the run is finished.
    bool advance() {
        if (done) return false;
        if (!gen.step()) { done = true; return false; }
        started = true;
        // a new leaf statement - at top level or inside a sequence (§6.10)
        int s = gen.statement_index();
        auto st = gen.stmt_stack();
        bool same = s == counted_stmt && st.size() == counted_stack.size();
        for (size_t k = 0; same && k < st.size(); ++k)
            same = st[k].index == counted_stack[k].index &&
                   st[k].iteration == counted_stack[k].iteration;
        if (!same) { counted_stmt = s; counted_stack = std::move(st); apps_in_stmt = 0; }
        if (!gen.at_statement_boundary()) apps_in_stmt++;
        return true;
    }

    // Step (F10): one application (or the closing statement boundary).
    void step_once() { advance(); refresh(); }
    // Next Statement (F11): pull until a statement boundary or done.
    void next_statement() {
        while (advance() && !gen.at_statement_boundary()) {}
        refresh();
    }
    // Run: drain everything.
    void run_all() {
        while (advance()) {}
        refresh();
    }

    // Statement the last event worked on (the Rule window's subject).
    int shown_stmt() const { return gen.statement_index(); }
    // Statement in progress / about to run (the Program window's marker).
    int current_stmt() const {
        if (!started) return 0;
        if (done) return stmt_count;
        int last = gen.statement_index();
        // only a top-level boundary finishes a program statement; a leaf
        // inside a sequence leaves the marker on the applying statement
        bool top_done = gen.at_statement_boundary() && gen.stmt_stack().size() <= 1;
        return top_done ? last + 1 : last;
    }
};

// ── statement display ─────────────────────────────────────────────────────────

std::string stmt_desc(program_stmt const& s);
char const* stmt_icon(program_stmt const& s);

// ── tileset texture ───────────────────────────────────────────────────────────

struct tile_texture {
    SDL_Texture* sdl_tex{nullptr};
    int          width{0};
    int          height{0};
    std::string  loaded_path;

    // Reload only when path has changed. Returns true if texture is ready.
    bool sync(SDL_Renderer* renderer, std::string const& abs_path);

    tile_texture() = default;
    tile_texture(tile_texture const&)            = delete;
    tile_texture& operator=(tile_texture const&) = delete;
    ~tile_texture();
};

// Resolve a path relative to the .ls file's directory.
std::string resolve_path(std::string const& ls_path, std::string const& rel);

// (Re)load every tileset in cfg.tilesets into `textures` (keyed by tileset
// name, sync()'d in place so an unchanged path is a no-op), and drop entries
// for tilesets no longer configured, freeing their GPU texture.
void sync_tilesets(SDL_Renderer* renderer, std::string const& ls_path,
                   project_config const& cfg,
                   std::unordered_map<std::string, tile_texture>& textures);

// ── views (debug_ui_views.cpp) ────────────────────────────────────────────────

void draw_rule_window(script const& sc, debug_run const& run, float mini_px,
                      project_config const& cfg);
void draw_program_window(script const& sc, debug_run const& run);
void draw_tags_window(compiled const& meta, project_config& cfg);
void draw_grid_composite(script const& sc, debug_run const& run,
                         project_config const& cfg,
                         std::unordered_map<std::string, tile_texture> const& tile_textures,
                         float cell_px);
// Returns the height it occupied, so the caller can size the grid above it.
float draw_layer_strip(project_config& cfg, compiled const& meta);
void draw_settings_window(project_config& cfg,
                          std::unordered_map<std::string, tile_texture>& tile_textures,
                          SDL_Renderer* renderer, std::string const& ls_path);

}  // namespace ls
