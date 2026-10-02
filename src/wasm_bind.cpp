#include <emscripten/bind.h>
#include "inspect.hpp"
#include "ls.hpp"
#include <cstdio>
#include <string>
#include <unordered_map>

using namespace emscripten;

namespace {

// Hand-rolled JSON emission, same approach as inspect.cpp — this file talks
// only to the public ls:: embedding API, so it stays dependency-free too.
void js(std::string& o, std::string_view s) {
    o += '"';
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                o += buf;
            } else {
                o += c;
            }
        }
    }
    o += '"';
}

// One in-flight debug run, keyed by an opaque id the JS side holds.
struct session {
    ls::generator gen;
    ls::run       r;
    unsigned int  seed{0};
    bool          done{false};
    int           entry{-1};   // the sequence this run applies (§6)
    // Applications pulled for the statement currently being worked on — same
    // bookkeeping as lsd's debug_run::advance() (app/debug_ui_internal.hpp),
    // recomputed as we go since a step can land mid-statement.
    int           apps_in_stmt{0};
    int           counted_stmt{-1};
};

// Pull one application/statement-boundary event, updating apps_in_stmt.
// False once the run is finished — mirrors lsd's debug_run::advance().
bool advance(session& s) {
    if (s.done) return false;
    if (!s.r.step()) { s.done = true; return false; }
    int idx = s.r.statement_index();
    if (idx != s.counted_stmt) { s.counted_stmt = idx; s.apps_in_stmt = 0; }
    if (!s.r.at_statement_boundary()) s.apps_in_stmt++;
    return true;
}

// A JS resolver (path, from) => {name, source} | null, as an ls::resolver
// (§2.6). Called synchronously during compile; `undefined` means no `use`
// resolves.
ls::resolver js_resolver(val resolve) {
    if (resolve.isUndefined() || resolve.isNull()) return {};
    return [resolve](std::string const& path, std::string const& from)
               -> std::optional<ls::module_source> {
        val r = resolve(path, from);
        if (r.isUndefined() || r.isNull()) return std::nullopt;
        return ls::module_source{r["name"].as<std::string>(), r["source"].as<std::string>()};
    };
}

std::string inspect_with(std::string const& source, std::string const& name, val resolve) {
    return ls::inspect_json(source, name, js_resolver(resolve));
}

std::unordered_map<int, session> g_sessions;
int g_next_id = 1;
std::string g_last_error;

void emit_level(std::string& o, ls::level const& lvl) {
    o += "{\"width\":" + std::to_string(lvl.width()) +
         ",\"height\":" + std::to_string(lvl.height()) + ",\"layers\":[";
    for (int li = 0; li < lvl.layer_count(); ++li) {
        if (li) o += ",";
        ls::grid g = lvl.layer(li);
        o += "{\"name\":";
        js(o, lvl.layer_name(li));
        o += ",\"isNumber\":";
        o += g.is_number() ? "true" : "false";
        o += ",\"cells\":[";
        for (int y = 0; y < lvl.height(); ++y) {
            for (int x = 0; x < lvl.width(); ++x) {
                if (x || y) o += ",";
                if (g.is_number()) {
                    if (g.is_empty(x, y)) o += "null";
                    else o += std::to_string(g.at(x, y));
                    continue;
                }
                if (g.is_empty(x, y)) { o += "[]"; continue; }
                o += "[";
                bool first = true;
                // Public API has no "list this tagset's values" query, so we
                // probe every value bit (1..30 — bit 0 is the empty flag, bit
                // 31 reserved) and ask the grid to name the ones that
                // overlap — has()/valueName() are exactly the pair documented
                // for reading a (possibly union) cell generically.
                for (int b = 1; b <= 30; ++b) {
                    int mask = 1 << b;
                    if (!g.has(x, y, mask)) continue;
                    std::string name = g.valueName(mask);
                    if (name.empty()) continue;
                    if (!first) o += ",";
                    first = false;
                    js(o, name);
                }
                o += "]";
            }
        }
        o += "]}";
    }
    o += "]}";
}

void emit_highlights(std::string& o, ls::run const& r) {
    o += "[";
    bool first = true;
    for (auto const& h : r.highlights()) {
        if (!first) o += ",";
        first = false;
        o += "{\"layer\":" + std::to_string(h.layer) +
             ",\"x\":" + std::to_string(h.x) +
             ",\"y\":" + std::to_string(h.y) + ",\"what\":\"" +
             (h.what == ls::cell_highlight::kind::write ? "write" : "match") +
             "\"}";
    }
    o += "]";
}

std::string state_json(session const& s, ls::level const& lvl) {
    std::string o = "{\"done\":";
    o += s.done ? "true" : "false";
    o += ",\"seed\":" + std::to_string(s.seed);
    o += ",\"statementIndex\":" + std::to_string(s.r.statement_index());
    o += ",\"statementCount\":" + std::to_string(s.gen.statement_count(s.entry));
    o += ",\"atStatementBoundary\":";
    o += s.r.at_statement_boundary() ? "true" : "false";
    o += ",\"appsInStatement\":" + std::to_string(s.apps_in_stmt);
    o += ",\"highlights\":";
    emit_highlights(o, s.r);
    o += ",\"level\":";
    emit_level(o, lvl);
    o += "}";
    return o;
}

}  // namespace

// Compile + start a progressive run of sequence `entry` (application
// granularity, observe on). Returns a session id, or -1 on a compile error
// or an unknown entry (see run_last_error()).
int run_begin(std::string const& source, std::string const& name, unsigned int seed,
              std::string const& entry, val resolve) {
    ls::generator gen = ls::generator::compile(source, name, js_resolver(resolve));
    if (!gen) {
        g_last_error = gen.error();
        return -1;
    }
    int eid = gen.sequence(entry);
    if (eid < 0) {
        g_last_error = name + ": error: no sequence '" + entry + "' to run";
        if (gen.sequence_count() == 0) {
            g_last_error += " (the file declares no sequences)";
        } else {
            g_last_error += "; declared sequences:";
            for (int i = 0; i < gen.sequence_count(); ++i)
                g_last_error += (i ? ", " : " ") + gen.sequence_name(i);
        }
        g_last_error += "\n";
        return -1;
    }
    ls::run r = gen.run(eid, seed, ls::step_mode::application, ls::observe::on);
    int id = g_next_id++;
    g_sessions.emplace(id, session{std::move(gen), std::move(r), seed, false, eid});
    return id;
}

std::string run_last_error() {
    return g_last_error;
}

// Current state without advancing — the initial frame before any step().
std::string run_state(int id) {
    auto it = g_sessions.find(id);
    if (it == g_sessions.end()) return "{}";
    return state_json(it->second, it->second.r.snapshot());
}

// Advance one application/statement and return the resulting state.
std::string run_step(int id) {
    auto it = g_sessions.find(id);
    if (it == g_sessions.end()) return "{}";
    session& s = it->second;
    advance(s);
    return state_json(s, s.r.snapshot());
}

// Advance to the next statement boundary (lsd's "Next Statement" / F11):
// pull applications until one completes a statement, or the run ends.
std::string run_next_statement(int id) {
    auto it = g_sessions.find(id);
    if (it == g_sessions.end()) return "{}";
    session& s = it->second;
    while (advance(s) && !s.r.at_statement_boundary()) {}
    return state_json(s, s.r.snapshot());
}

// Drain the remaining run in one shot and return the finished state.
std::string run_finish(int id) {
    auto it = g_sessions.find(id);
    if (it == g_sessions.end()) return "{}";
    session& s = it->second;
    while (advance(s)) {}
    return state_json(s, s.r.snapshot());
}

void run_end(int id) {
    g_sessions.erase(id);
}

EMSCRIPTEN_BINDINGS(ls_module) {
    function("inspect_json", &inspect_with);
    function("run_begin", &run_begin);
    function("run_last_error", &run_last_error);
    function("run_state", &run_state);
    function("run_step", &run_step);
    function("run_next_statement", &run_next_statement);
    function("run_finish", &run_finish);
    function("run_end", &run_end);
}
