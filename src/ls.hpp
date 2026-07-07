#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// LevelScript embedding API — what a game sees.
//
//   auto gen   = ls::generator::compile(source);        // once, at load
//   int  wall  = gen.tag("geo.wall");                   // resolve names once
//   auto level = gen.generate(seed);                    // pure in (seed)
//   auto geo   = level["level"];
//   if (geo.at(x, y) == wall) ...
//
// generator is a reusable factory (keep one per .ls file; share across
// threads). level is a self-contained value: it owns its cells and outlives
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

// ── grid — one layer of a generated level ────────────────────────────────────

class grid {
public:
    grid() = default;

    /// Cell at (x, y): the tag value id, or the stored number for number
    /// layers; -1 when empty (or out of range / invalid).
    int  at(int x, int y) const;
    bool empty(int x, int y) const;
    bool is_number() const;

    /// Name of a tag value id ("" for number layers or out of range).
    std::string name(int value_id) const;

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
    grid layer(int index) const;              // declaration order
    std::string layer_name(int index) const;

private:
    friend class generator;
    friend class generation;
    explicit level(std::shared_ptr<internal::level_data const> data);

    std::shared_ptr<internal::level_data const> data_;
};

// ── generation — one in-flight progressive run ──────────────────────────────

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

class generation {
public:
    generation();
    generation(generation&&) noexcept;
    generation& operator=(generation&&) noexcept;
    ~generation();

    /// Advance by the granularity fixed at begin(); false when done.
    bool step();
    /// Copy of the current state — mid-batch, committed statements plus this
    /// batch's applications so far.
    level snapshot() const;
    /// Drain whatever remains and return the finished level ("skip" path).
    level finish();

    // ── observe channel (populated only when begun with observe::on) ──
    /// Index of the statement the last step worked on (-1 before first step).
    int stmt_index() const;
    /// Matched/written cells of the last application.
    std::vector<cell_highlight> highlights() const;

private:
    friend class generator;
    explicit generation(std::unique_ptr<internal::run_state> s);

    std::unique_ptr<internal::run_state> s_;
};

// ── generator — one compiled program; a reusable, stateless factory ─────────

class generator {
public:
    generator() = default;

    /// Compile LevelScript source text. The game owns file/asset IO; `name`
    /// labels diagnostics ("dungeon.ls:12:3: error: ...").
    static generator compile(const std::string& source,
                             const std::string& name = "generator");

    explicit operator bool() const { return prog_ != nullptr; }
    /// Formatted diagnostics when compile failed; "" when it succeeded.
    std::string error() const { return error_; }
    /// Formatted warnings of a successful compile (e.g. a fixpoint statement
    /// over a rule that can never terminate); "" when there are none.
    std::string warnings() const { return warnings_; }

    /// Tag value id, qualified by tagset: tag("geo.wall"). Ids are per
    /// tagset, valid for every layer of that tagset. -1 if unknown.
    int tag(const std::string& qualified) const;

    /// Run the whole program: (seed, params) -> level, deterministically.
    /// Params override the declared defaults; unknown names are ignored.
    level generate(uint64_t seed,
                   std::vector<std::pair<std::string, int>> const& params = {}) const;

    /// Start a progressive run; pull it with generation::step().
    generation begin(uint64_t seed, step_mode mode = step_mode::statement,
                     observe obs = observe::off,
                     std::vector<std::pair<std::string, int>> const& params = {}) const;

    /// Number of program statements (progress denominators).
    int statement_count() const;

private:
    std::shared_ptr<compiled const> prog_;
    std::string                     error_;
    std::string                     warnings_;
};

}  // namespace ls
