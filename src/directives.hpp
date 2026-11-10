#pragma once
#include "diagnostic.hpp"
#include "ls.hpp"   // resolver
#include "modules.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Tool directives (spec Appendix B): `// @key=value` comments carrying
// metadata for the CLI, the debugger, the web editor and the extension. To
// the language they are ordinary comments - nothing here affects a compile
// or a run, and the embedding API never reads them.
//
//   @seed=N        the file        default invocation seed (root file only)
//   @name=ID       a tag value     display name
//   @color=#RRGGBB a tag value     default color (`number` holds 0xRRGGBB)
//   @speed=N       a statement     steps per frame while the debugger plays;
//                                  @speed=-N: one step every N play ticks
//   @hold=N        a statement     play frames its result stays on screen
//   @view=ID       a grid          how the tools first draw it
//   @tileset=ID    a grid          the named tileset its `tiles` view draws from
//   @expect TEXT   the file        a test expectation (repeatable)
//   @param P=N     the file        a param value for test runs (name = P, value = N)

namespace ls {

enum class directive_target { file, tag_value, statement, layer };

struct directive {
    std::string      key;      // without the '@'
    std::string      value;    // as written
    uint64_t         number{0};   // the value, for the integer keys (its magnitude), and @color (0xRRGGBB)
    bool             negative{false};   // written with a '-' (@speed only); never for 0
    directive_target target{directive_target::file};
    source_loc       loc;          // the `@key=value` text
    source_loc       target_loc;   // the target's range; the file: none
    // tag_value: owner = tagset, name = value;
    // statement: owner = sequence, index = position in its body (what
    // run::stmt_stack() reports); layer: name = the grid
    std::string      owner, name;
    int              index{-1};
};

struct directive_set {
    std::vector<directive> all;        // in source order, module by module
    diagnostics            warnings;   // directive warnings, apart from compile diagnostics
    std::vector<std::string> modules;  // canonical names, by module id (source_loc::mod)

    // The value of `key` on a target; null when there is none.
    directive const* find(std::string_view key, directive_target target,
                          std::string_view owner, std::string_view name, int index = -1) const;
    // The root file's @seed.
    std::optional<uint64_t> seed() const;
    // The root file's @expect texts, in order, and its @param values.
    std::vector<std::string> expects() const;
    std::vector<std::pair<std::string, int>> params() const;
};

// Reads the directives of every module of a loaded closure.
directive_set read_directives(module_closure const& mods);

// Loads the closure (compile diagnostics discarded) and reads its directives.
directive_set read_directives(std::string const& source, std::string const& name,
                              resolver const& resolve = {});

}  // namespace ls
