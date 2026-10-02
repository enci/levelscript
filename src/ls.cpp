#include "ls.hpp"
#include "machine.hpp"
#include "modules.hpp"
#include "sema.hpp"

namespace ls {

using internal::level_data;
using internal::machine;
using internal::sequence;
using internal::step_event;

// Tag cells store a 32-bit mask (spec section 3/section 4.1): bit 0 = empty, values occupy
// bits 1..30 in declaration order, bit 31 reserved. `value_mask` isolates the
// value bits from a raw cell.
constexpr int64_t value_mask = 0x7FFFFFFE;

// ── grid ─────────────────────────────────────────────────────────────────────

grid::grid(std::shared_ptr<level_data const> data, int layer)
    : data_(std::move(data)), layer_(layer) {}

std::string grid::name() const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->info->layers.size()) return "";
    return data_->info->layers[layer_].name;
}

bool grid::is_number() const {
    return data_ && layer_ >= 0 && layer_ < (int)data_->layers.size()
        && data_->layers[layer_].tag_id < 0;
}

// -1 is out-of-band only (empty / out of range / invalid) — for a tag cell
// the value mask itself (bits 1..30, a single bit for a normal cell, several
// for a union write); for a number cell the stored number as-is. A number
// cell can legitimately store -1, which is why is_empty() exists as the real
// emptiness test rather than `at() == -1`.
int grid::at(int x, int y) const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return -1;
    if (x < 0 || x >= data_->width || y < 0 || y >= data_->height) return -1;
    auto const& l = data_->layers[layer_];
    int64_t raw = l.cells[y * data_->width + x];
    if (l.tag_id < 0)   // number grid
        return raw == num_empty ? -1 : (int)raw;
    if (raw & tag_empty) return -1;         // real empty (bit 0 set)
    return (int)(raw & value_mask);         // value mask, 0 only if malformed
}

bool grid::is_empty(int x, int y) const {
    // Out of range / invalid reads as empty — a safe default for loop code
    // that does `if (grid.is_empty(x, y)) continue`.
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return true;
    if (x < 0 || x >= data_->width || y < 0 || y >= data_->height) return true;
    auto const& l = data_->layers[layer_];
    int64_t raw = l.cells[y * data_->width + x];
    return l.tag_id < 0 ? (raw == num_empty) : ((raw & tag_empty) != 0);
}

bool grid::has(int x, int y, int mask) const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return false;
    if (x < 0 || x >= data_->width || y < 0 || y >= data_->height) return false;
    auto const& l = data_->layers[layer_];
    if (l.tag_id < 0) return false;   // number grid: no mask to test
    int64_t raw = l.cells[y * data_->width + x];
    return (raw & (int64_t)mask) != 0;
}

std::string grid::valueName(int mask) const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return "";
    int tag = data_->layers[layer_].tag_id;
    if (tag < 0 || tag >= (int)data_->info->tag_values.size()) return "";

    int64_t m = (int64_t)mask & value_mask;
    if (m == 0 || (m & (m - 1)) != 0) return "";   // not exactly one value bit

    int bit = 0;
    while (!((m >> bit) & 1)) ++bit;
    int value_id = bit - 1;   // tag_bit(vid) == 1 << (vid + 1)

    auto const& vals = data_->info->tag_values[tag];
    if (value_id < 0 || value_id >= (int)vals.size()) return "";
    return vals[value_id];
}

// ── level ────────────────────────────────────────────────────────────────────

level::level(std::shared_ptr<level_data const> data) : data_(std::move(data)) {}

int level::width() const  { return data_ ? data_->width : 0; }
int level::height() const { return data_ ? data_->height : 0; }

int level::layer_count() const {
    return data_ ? (int)data_->layers.size() : 0;
}

grid level::operator[](const std::string& name) const {
    if (data_) {
        int id = data_->info->layer_id(name);
        if (id >= 0) return grid{data_, id};
    }
    return {};
}

grid level::layer(int index) const {
    if (data_ && index >= 0 && index < (int)data_->layers.size())
        return grid{data_, index};
    return {};
}

std::string level::layer_name(int index) const {
    if (data_ && index >= 0 && index < (int)data_->info->layers.size())
        return data_->info->layers[index].name;
    return "";
}

// ── run ──────────────────────────────────────────────────────────────────────

namespace internal {
// One in-flight run: the machine and the coroutine pulling it. Heap-allocated
// and never moved, so the coroutine's machine pointer stays valid.
struct run_state {
    machine              m;
    sequence<step_event> seq;
    step_mode            mode;
    bool                 done{false};
    int                  last_stmt{-1};
    bool                 at_boundary{false};
    bool                 at_begin{false};
    bool                 stop_at_begin{false};

    run_state(std::shared_ptr<compiled const> prog, int entry, uint64_t seed,
              step_mode md, observe obs,
              std::vector<std::pair<std::string, int>> const& params)
        : m(std::move(prog), seed, entry), seq(m.run()), mode(md) {
        m.set_observe(obs == observe::on);
        for (auto const& [name, value] : params) m.set_param(name, value);
    }
};
}  // namespace internal

run::run()  = default;
run::~run() = default;
run::run(run&&) noexcept            = default;
run& run::operator=(run&&) noexcept = default;
run::run(std::unique_ptr<internal::run_state> s) : s_(std::move(s)) {}

bool run::step() {
    if (!s_ || s_->done) return false;
    while (s_->seq.next()) {
        auto const& e = s_->seq.value();
        if (e.what == step_event::kind::begin && !s_->stop_at_begin) continue;
        s_->last_stmt = e.stmt;
        s_->at_boundary = e.what == step_event::kind::statement;
        s_->at_begin = e.what == step_event::kind::begin;
        if (s_->at_begin) return true;
        if (s_->mode == step_mode::application ||
            e.what == step_event::kind::statement)
            return true;
    }
    s_->done = true;
    return false;
}

int run::statement_index() const {
    return s_ ? s_->last_stmt : -1;
}

bool run::at_statement_boundary() const {
    return s_ && s_->at_boundary;
}

void run::stop_at_begin(bool on) {
    if (s_) s_->stop_at_begin = on;
}

bool run::at_statement_begin() const {
    return s_ && s_->at_begin;
}

std::vector<stmt_frame> run::stmt_stack() const {
    std::vector<stmt_frame> out;
    if (!s_ || s_->last_stmt < 0) return out;
    for (auto const& f : s_->m.frames()) out.push_back({f.index, f.iteration});
    return out;
}

std::vector<cell_highlight> run::highlights() const {
    std::vector<cell_highlight> out;
    if (!s_) return out;
    for (auto const& h : s_->m.highlights())
        out.push_back({h.grid, h.col, h.row,
                       h.is_write ? cell_highlight::kind::write
                                  : cell_highlight::kind::match});
    return out;
}

level run::snapshot() const {
    return s_ ? level{s_->m.snapshot()} : level{};
}

level run::finish() {
    while (step()) {}
    return snapshot();
}

// ── generator ────────────────────────────────────────────────────────────────

generator generator::compile(const std::string& source, const std::string& name,
                             resolver resolve) {
    generator g;
    diagnostics diags;
    std::string label = name.empty() ? "generator" : name;

    module_closure mods = load_closure(source, label, resolve, diags);
    for (int id : mods.order) g.modules_.push_back(mods.names[(size_t)id]);
    if (!diags.has_errors()) {
        auto prog = std::make_shared<compiled>();
        if (analyze(mods, *prog, diags))
            g.prog_ = std::move(prog);
    }
    if (!g.prog_) g.error_ = diags.format_all();
    else if (!diags.all.empty()) g.warnings_ = diags.format_all();
    return g;
}

int generator::tag(const std::string& qualified) const {
    if (!prog_) return 0;
    auto dot = qualified.find('.');
    if (dot == std::string::npos) return 0;
    int tid = prog_->tag_id(qualified.substr(0, dot));
    if (tid < 0) return 0;
    return (int)prog_->mask_of(tid, qualified.substr(dot + 1));
}

int generator::sequence(const std::string& name) const {
    if (!prog_) return -1;
    for (int i = 0; i < (int)prog_->sequences.size(); ++i)
        if (prog_->sequences[(size_t)i].name == name) return i;
    return -1;
}

int generator::sequence_count() const {
    return prog_ ? (int)prog_->sequences.size() : 0;
}

std::string generator::sequence_name(int id) const {
    if (!prog_ || id < 0 || id >= (int)prog_->sequences.size()) return "";
    return prog_->sequences[(size_t)id].name;
}

level generator::generate(int entry, uint64_t seed,
                          std::vector<std::pair<std::string, int>> const& params) const {
    if (!prog_ || entry < 0 || entry >= sequence_count()) return {};
    machine m(prog_, seed, entry);
    for (auto const& [name, value] : params) m.set_param(name, value);
    auto seq = m.run();
    while (seq.next()) {}
    return level{m.snapshot()};
}

class run generator::run(int entry, uint64_t seed, step_mode mode, observe obs,
                         std::vector<std::pair<std::string, int>> const& params) const {
    if (!prog_) return ls::run{};
    return ls::run{std::make_unique<internal::run_state>(prog_, entry, seed, mode, obs,
                                                         params)};
}

int generator::statement_count(int entry) const {
    if (!prog_ || entry < 0 || entry >= sequence_count()) return 0;
    return (int)prog_->sequences[(size_t)entry].stmts.size();
}

}  // namespace ls
