#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// LevelScript embedding API — what a game sees.
//
//   auto gen   = ls::generator::compile(source, "dungeon.ls", resolve);  // once, at load
//   int  wall  = gen.tag("geo.wall");                   // resolve names once
//   int  entry = gen.sequence("main");                  // any sequence can be the entry
//   auto level = gen.generate(entry, seed);             // pure in (entry, seed, params)
//   auto geo   = level["level"];
//   if (geo.at(x, y) == wall) ...
//
// generator is a reusable factory (keep one per root module and its closure;
// share across threads). level is a self-contained value: it owns its cells and outlives
// the generator. grid shares ownership of its layer, so a grid handed to a
// GC'd scripting host can outlive the level object it came from.
// Nothing here throws; a failed compile yields a falsy generator with
// error(), and every query on anything invalid reads as empty.

namespace ls {

struct compiled;
namespace internal {
struct level_data;
struct run_state;
}

// ── modules (spec §2.6) ──────────────────────────────────────────────────────

/// A resolved module: its canonical name (identifies it within one compile
/// and labels its diagnostics) and its source text.
struct module_source {
    std::string name;
    std::string source;
};

/// Maps a `use` path, written in the module whose canonical name is `from`,
/// to a module - or std::nullopt when it cannot (§7.3 check 9). The game
/// owns all IO; it must return the same canonical name for every path that
/// denotes the same module.
using resolver = std::function<std::optional<module_source>(const std::string& path,
                                                            const std::string& from)>;

// ── grid — one layer of a generated level ────────────────────────────────────

class grid {
public:
    grid() = default;

    /// Name of the layer this grid was pulled from ("" if invalid).
    std::string name() const;
    bool is_number() const;

    /// Cell at (x, y): for a tag layer, the stored value mask (bits 1..30 —
    /// a single bit for a normal cell, multiple bits for a union write); for
    /// a number layer, the stored number. -1 is out-of-band only — empty,
    /// out of range, or an invalid grid — and for number layers is not a
    /// reliable emptiness test on its own (a cell can legitimately store the
    /// number -1). Use is_empty() for a real emptiness test.
    int  at(int x, int y) const;
    bool is_empty(int x, int y) const;
    /// True if the cell's stored mask overlaps `mask` at all — the one query
    /// that stays correct for a union cell (multiple value bits set).
    bool has(int x, int y, int mask) const;

    /// Name of a single-bit value mask ("" if `mask` isn't exactly one value
    /// bit, or is out of range / a number layer).
    std::string valueName(int mask) const;

private:
    friend class level;
    grid(std::shared_ptr<internal::level_data const> data, int layer);

    std::shared_ptr<internal::level_data const> data_;
    int layer_{-1};
};

// ── level — one generated outcome: the stack of co-registered grids ─────────

class level {
public:
    level() = default;

    int width() const;
    int height() const;

    int  layer_count() const;
    grid operator[](const std::string& layer_name) const;
    grid layer(int index) const;              // layer order (§4): canonical module order
    std::string layer_name(int index) const;

private:
    friend class generator;
    friend class run;
    explicit level(std::shared_ptr<internal::level_data const> data);

    std::shared_ptr<internal::level_data const> data_;
};

// ── run — one in-flight progressive generation ──────────────────────────────

/// statement: one leaf statement (a rule application or an operation, in the
/// entry's body or inside a nested sequence) per step(); application: one rule application
/// per step (operations and atomic batches still advance whole). Applying a
/// sequence is never a step of its own.
enum class step_mode { statement, application };
enum class observe   { off, on };

/// One highlighted cell of the observe channel: which layer/cell the last
/// application matched (kind::match) or wrote (kind::write).
struct cell_highlight {
    enum class kind { match, write };
    int  layer;
    int  x, y;
    kind what;
};

/// One level of run::stmt_stack(): `index` is the statement's position in
/// its enclosing list (the entry's body, or a nested sequence body);
/// `iteration` is the 0-based iteration of the enclosing sequence
/// application (always 0 for frame 0 - the entry runs once).
struct stmt_frame {
    int index;
    int iteration;
};

class run {
public:
    run();
    run(run&&) noexcept;
    run& operator=(run&&) noexcept;
    ~run();

    /// Advance by the granularity fixed at generator::run(); false when done.
    bool step();
    /// Copy of the current state — mid-batch, committed statements plus this
    /// batch's applications so far.
    level snapshot() const;
    /// Drain whatever remains and return the finished level ("skip" path).
    level finish();

    // ── observe channel (populated only when begun with observe::on) ──
    /// Index, in the entry's body, of the statement the last step worked on
    /// (-1 before the first step); inside a nested sequence, the entry-body
    /// statement that applied it.
    int statement_index() const;
    /// Position of the last step through nested sequences, outermost first:
    /// frame 0 is a statement of the entry's body, each further frame a
    /// statement inside the sequence the frame before applied. Empty before
    /// the first step.
    std::vector<stmt_frame> stmt_stack() const;
    /// True when the last step completed a statement (vs. one application
    /// within it) — progress bars and steppers key off this.
    bool at_statement_boundary() const;
    /// Matched/written cells of the last application.
    std::vector<cell_highlight> highlights() const;

private:
    friend class generator;
    explicit run(std::unique_ptr<internal::run_state> s);

    std::unique_ptr<internal::run_state> s_;
};

// ── generator — one compiled program; a reusable, stateless factory ─────────

class generator {
public:
    generator() = default;

    /// Compile `source` as the root module (§2.6), with canonical name
    /// `name`. The game owns file/asset IO: each `use` is mapped to a module
    /// by `resolve`; with no resolver every `use` is unresolved. Diagnostics
    /// are labelled with canonical module names ("dungeon.ls:12:3: error: ...").
    static generator compile(const std::string& source,
                             const std::string& name = "generator",
                             resolver resolve = {});

    explicit operator bool() const { return prog_ != nullptr; }
    /// Formatted diagnostics when compile failed; "" when it succeeded.
    std::string error() const { return error_; }
    /// Formatted warnings of a successful compile (e.g. a fixpoint statement
    /// over a rule that can never terminate); "" when there are none.
    std::string warnings() const { return warnings_; }

    /// Value mask, qualified by tagset: tag("geo.wall") -> that value's bit;
    /// also resolves a named union to its composite mask. Masks are per
    /// tagset, valid for every layer of that tagset. 0 if unknown (an
    /// unknown tag ORed into a composite mask degrades correctly, unlike -1
    /// would).
    int tag(const std::string& qualified) const;

    /// Sequence id, for use as an entry (§6); -1 if unknown. Ids are
    /// 0 .. sequence_count() - 1 in canonical declaration order.
    int sequence(const std::string& name) const;
    int sequence_count() const;
    /// Name of a sequence id ("" if out of range).
    std::string sequence_name(int id) const;
    /// Canonical names of the closure's modules, canonical order, root last.
    /// After a failed compile, the modules resolved before the failure - so
    /// a host can watch a broken file and retry (hot reload).
    std::vector<std::string> modules() const { return modules_; }

    /// Run sequence `entry` as the entry (§6): (entry, seed, params) -> level,
    /// deterministically. Params override the declared defaults; unknown
    /// names are ignored. An invalid entry id yields an empty level.
    level generate(int entry, uint64_t seed,
                   std::vector<std::pair<std::string, int>> const& params = {}) const;

    /// Start a progressive run; pull it with run::step(). Named `class run`
    /// (not bare `run`) in the return position: this method's own name is
    /// `run`, and an unqualified `run run(...)` here would silently change
    /// which `run` the return type names (GCC: "changes meaning of 'run'").
    /// With an invalid entry id, the first step() returns false.
    class run run(int entry, uint64_t seed, step_mode mode = step_mode::statement,
                  observe obs = observe::off,
                  std::vector<std::pair<std::string, int>> const& params = {}) const;

    /// Number of statements in the entry's body (progress denominators); a
    /// nested sequence application counts as one. 0 for an invalid id.
    int statement_count(int entry) const;

private:
    std::shared_ptr<compiled const> prog_;
    std::string                     error_;
    std::string                     warnings_;
    std::vector<std::string>        modules_;
};

}  // namespace ls
