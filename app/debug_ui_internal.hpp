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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
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
    ast_file    ast;    // display metadata only: the closure, merged (§2.6)
    compiled    meta;   // display metadata only
    // The entry sequence a run applies (§6). The name survives reloads; the
    // id is re-resolved on each, falling back to the first sequence.
    std::string entry_name{"main"};
    int         entry{-1};
    // Breakpoints, by dotted statement path within the entry ("2.0.1"). They
    // survive Reset and reloads; a path that no longer exists never hits.
    std::set<std::string> breakpoints;
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

    void restart(generator const& g, int entry, uint64_t seed) {
        gen = g.run(entry, seed, step_mode::application, observe::on);
        stmt_count = g.statement_count(entry);
        started = false;
        done = false;
        cmd = command::none;
        last_stack.clear();
        paused_at.clear();
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

    // ── VS Code-style stepping ────────────────────────────────────────────
    //
    // The run is a coroutine: it can only stop *after* an event (one rule
    // application, or a statement finishing), never before one, and never go
    // back. Every command below pulls events until its condition holds, a
    // breakpoint is entered, or the run ends. Long commands are time-sliced
    // by the caller (tick()) so the UI stays live and Pause can interrupt.

    enum class command { none, step_over, step_out, cont };
    command cmd{command::none};
    size_t  cmd_depth{0};        // statement-stack depth the command started at
    bool    cmd_went_deeper{false};
    std::vector<stmt_frame> last_stack;   // stack of the previous event
    std::string paused_at;       // "breakpoint 2.0.1" when one stopped the run

    bool busy() const { return cmd != command::none; }

    // Dotted statement path of the first `n` frames: "2.0.1" (§6.10 frames).
    static std::string dotted(std::vector<stmt_frame> const& st, size_t n) {
        std::string p;
        for (size_t k = 0; k < n && k < st.size(); ++k) {
            if (k) p += '.';
            p += std::to_string(st[k].index);
        }
        return p;
    }

    // A breakpoint is entered when the event's stack reaches a marked
    // statement it was not in on the previous event - so every iteration of
    // a sequence body re-enters it, while further applications of the same
    // statement do not.
    std::string entered_breakpoint(std::set<std::string> const& bps,
                                   std::vector<stmt_frame> const& st) const {
        for (size_t n = 1; n <= st.size(); ++n) {
            bool same = last_stack.size() >= n;
            for (size_t k = 0; same && k < n; ++k)
                same = last_stack[k].index == st[k].index &&
                       last_stack[k].iteration == st[k].iteration;
            if (same) continue;
            std::string key = dotted(st, n);
            if (bps.count(key)) return key;
        }
        return "";
    }

    // Pull one event and decide whether the active command stops on it.
    bool advance_cmd(std::set<std::string> const& bps) {
        if (!advance()) { cmd = command::none; return false; }
        auto st = gen.stmt_stack();
        size_t d = st.size();
        bool boundary = gen.at_statement_boundary();
        std::string bp = entered_breakpoint(bps, st);
        last_stack = st;
        if (!bp.empty()) {
            paused_at = "breakpoint " + bp;
            cmd = command::none;
            return true;
        }
        switch (cmd) {
        case command::step_over:
            if (d > cmd_depth) cmd_went_deeper = true;
            // left the level, finished a statement at this level, or came
            // back from a sequence into the next statement
            if (d < cmd_depth || (boundary && d <= cmd_depth) ||
                (cmd_went_deeper && d == cmd_depth))
                cmd = command::none;
            break;
        case command::step_out:
            if (d < cmd_depth) cmd = command::none;
            break;
        default: break;
        }
        return true;
    }

    void begin(command c) {
        if (done) return;
        paused_at.clear();
        size_t d = gen.stmt_stack().size();
        cmd = c;
        cmd_depth = std::max<size_t>(d, 1);
        cmd_went_deeper = false;
        if (c == command::step_out && cmd_depth <= 1) cmd = command::cont;   // out of the entry = run on
    }

    // Step Into (F11): exactly one event - the finest step.
    void step_into() {
        if (done) return;
        paused_at.clear();
        advance();
        last_stack = gen.stmt_stack();
        refresh();
    }
    // Pause (F6): stop whatever command is running.
    void pause() { cmd = command::none; refresh(); }

    // Run the active command for up to `budget_ms`; refresh once at the end.
    void tick(std::set<std::string> const& bps, double budget_ms) {
        if (!busy()) return;
        auto t0 = std::chrono::steady_clock::now();
        while (busy()) {
            advance_cmd(bps);
            double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            if (ms >= budget_ms) break;
        }
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
char const* stmt_icon(program_stmt const& s, compiled_stmt const& cs);

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
void draw_program_window(script& sc, debug_run const& run);
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
