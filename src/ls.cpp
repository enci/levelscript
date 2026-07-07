#include "ls.hpp"
#include "machine.hpp"
#include "parser.hpp"
#include "sema.hpp"

namespace ls {

using internal::level_data;
using internal::machine;
using internal::sequence;
using internal::step_event;

// ── grid ─────────────────────────────────────────────────────────────────────

grid::grid(std::shared_ptr<level_data const> data, int layer)
    : data_(std::move(data)), layer_(layer) {}

// Decode raw storage to a display value: -1 empty, else the tag value id
// (lowest set value bit) or the stored number.
int grid::at(int x, int y) const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return -1;
    if (x < 0 || x >= data_->width || y < 0 || y >= data_->height) return -1;
    auto const& l = data_->layers[layer_];
    int64_t raw = l.cells[y * data_->width + x];
    if (l.tag_id < 0)   // number grid
        return raw == num_empty ? -1 : (int)raw;
    for (int b = 1; b <= 30; ++b)
        if (raw & (int64_t{1} << b)) return b - 1;
    return -1;   // only the empty bit (or nothing) set
}

bool grid::empty(int x, int y) const { return at(x, y) == -1; }

bool grid::is_number() const {
    return data_ && layer_ >= 0 && layer_ < (int)data_->layers.size()
        && data_->layers[layer_].tag_id < 0;
}

std::string grid::name(int value_id) const {
    if (!data_ || layer_ < 0 || layer_ >= (int)data_->layers.size()) return "";
    int tag = data_->layers[layer_].tag_id;
    if (tag < 0 || tag >= (int)data_->info->tag_values.size()) return "";
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

// ── generation ───────────────────────────────────────────────────────────────

namespace internal {
// One in-flight run: the machine and the coroutine pulling it. Heap-allocated
// and never moved, so the coroutine's machine pointer stays valid.
struct run_state {
    machine              m;
    sequence<step_event> seq;
    step_mode            mode;
    bool                 done{false};

    run_state(std::shared_ptr<compiled const> prog, uint64_t seed, step_mode md)
        : m(std::move(prog), seed), seq(m.run()), mode(md) {}
};
}  // namespace internal

generation::generation()  = default;
generation::~generation() = default;
generation::generation(generation&&) noexcept            = default;
generation& generation::operator=(generation&&) noexcept = default;
generation::generation(std::unique_ptr<internal::run_state> s) : s_(std::move(s)) {}

bool generation::step() {
    if (!s_ || s_->done) return false;
    while (s_->seq.next()) {
        auto const& e = s_->seq.value();
        if (s_->mode == step_mode::application ||
            e.what == step_event::kind::statement)
            return true;
    }
    s_->done = true;
    return false;
}

level generation::snapshot() const {
    return s_ ? level{s_->m.snapshot()} : level{};
}

level generation::finish() {
    while (step()) {}
    return snapshot();
}

// ── generator ────────────────────────────────────────────────────────────────

generator generator::compile(const std::string& source, const std::string& name) {
    generator g;
    diagnostics diags;
    std::string label = name.empty() ? "generator" : name;

    auto ast = parse(source, label, diags);
    if (ast && !diags.has_errors()) {
        if (!ast->has_program)
            diags.error(label, 1, 1, "no 'program' block");
        else {
            auto prog = std::make_shared<compiled>();
            if (analyze(*ast, *prog, diags, label))
                g.prog_ = std::move(prog);
        }
    }
    if (!g.prog_) g.error_ = diags.format_all();
    return g;
}

int generator::tag(const std::string& qualified) const {
    if (!prog_) return -1;
    auto dot = qualified.find('.');
    if (dot == std::string::npos) return -1;
    int tid = prog_->tag_id(qualified.substr(0, dot));
    return prog_->value_id(tid, qualified.substr(dot + 1));
}

level generator::generate(uint64_t seed) const {
    if (!prog_) return {};
    machine m(prog_, seed);
    auto seq = m.run();
    while (seq.next()) {}
    return level{m.snapshot()};
}

generation generator::begin(uint64_t seed, step_mode mode) const {
    if (!prog_) return generation{};
    return generation{std::make_unique<internal::run_state>(prog_, seed, mode)};
}

}  // namespace ls
