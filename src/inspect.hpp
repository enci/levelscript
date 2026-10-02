#pragma once
#include "ls.hpp"   // resolver
#include <string>

// Machine-readable compile report for editor tooling — one function, two
// frontends: `levelscript --inspect` on the command line, and the WASM module the
// VS Code extension embeds. Best-effort: a broken (mid-edit) file still
// yields every symbol table it supports, plus all diagnostics.
//
// JSON shape:
//   {
//     "ok": bool,
//     "diagnostics": [{"line","col","severity":"error"|"warning","message"}],
//     "tokens":      [{"line","col","len","tag","value"}],   // colorable
//                    // pattern cells (1-based positions). tag >= 0: id into
//                    // symbols.tags, value = palette slot (values first,
//                    // then unions); tag -1 '*', -2 '.', -3 number literal
//                    // (value = the number)
//     "symbols": {
//       "tags":   [{"name","values":[{"name","loc"}],"unions":[{"name","loc"}]}],
//       "layers": [{"name","type"}],       // type: tagset name or "number"
//       "params": [{"name","derived"}],
//       "rules":  [...], "sequences": [{"name","loc"}],
//       "ops":    [...], "builtins": [...] // completion vocabulary
//     }
//   }

namespace ls {

// `resolve` maps the file's `use` declarations (§2.6); tokens and refs cover
// the root file only, symbol locations carry their declaring module.
std::string inspect_json(std::string const& source, std::string const& name,
                         resolver const& resolve = {});

}  // namespace ls
