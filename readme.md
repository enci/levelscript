# LevelScript

[![Build](https://github.com/enci/levelscript/actions/workflows/build.yaml/badge.svg?branch=main)](https://github.com/enci/levelscript/actions/workflows/build.yaml)
[![Release](https://img.shields.io/github/v/release/enci/levelscript)](https://github.com/enci/levelscript/releases/latest)

A scripting language for procedural level generation. Programs transform a
stack of correlated grids through pattern-rewrite rules; output is a pure
function of (seed, params).

LevelScript is the successor of the MGSL experiment (`../mgsl`), reimplemented
with its lessons applied and an embedding API designed for games first.

```
tag geo { wall, floor }

layers {
    level: grid of geo
}

rule fill {
    level[.]
    =>
    level[floor]
}

sequence main {
    resize(8, 4)
    everywhere fill
}
```

```cpp
auto gen   = ls::generator::compile(source, "dungeon.lvs", resolve);  // resolve maps `use` paths
auto level = gen.generate(gen.sequence("main"), seed);
auto geo   = level["level"];
int  wall  = gen.tag("geo.wall");
if (geo.at(x, y) == wall) ...
```

## Install

Installers for Windows and macOS are attached to every
[release](https://github.com/enci/levelscript/releases/latest). Both install
`levelscript` and `levelscript-debugger` and add them to your PATH; open a new
terminal afterwards.

To build them yourself:

- **Windows:** `python packaging/windows/build_installer.py` builds a per-user installer
  (needs [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3+) at
  `dist/LevelScript-setup-<version>-x64.exe`.
- **macOS:** `python packaging/macos/build_pkg.py` builds an Apple Silicon
  (macOS 11+) package at
  `dist/LevelScript-<version>-macos-arm64.pkg`. It installs to
  `/usr/local/levelscript` and puts `bin/` on PATH via `/etc/paths.d`. The
  package is unsigned: the first time, allow it under System Settings >
  Privacy & Security > Open Anyway. Uninstall with
  `sudo /usr/local/levelscript/uninstall.sh`.

## Compiler / runner: `levelscript`

Compiles a `.lvs` file, runs one sequence and prints every layer as text.

```
levelscript [--seed N] [--entry name] [--param name=value ...] [--inspect] <file.lvs>
```

| Option | Meaning |
|---|---|
| `--seed N` | Seed for the run. The output is a pure function of (seed, params); omitted, a random seed is used. |
| `--entry name` | Sequence to run (default `main`). If it doesn't exist, the available sequences are listed. |
| `--param name=value` | Set an integer parameter; repeatable. |
| `--inspect` | Don't run; print a JSON report (diagnostics, symbols) for editor tooling. |

```
levelscript --seed 42 examples/dungeon.lvs
levelscript --seed 7 --param difficulty=2 examples/arena.lvs
```

Compile errors and warnings go to stderr; a compile error exits with 1.
`use` paths are resolved relative to the importing file.

## Debugger: `levelscript-debugger`

An interactive window over the same program: step through the generation one
rule application at a time and watch the layers change.

```
levelscript-debugger [--seed N] [--entry name] <file.lvs>
```

Breakpoints are set by clicking the gutter next to a statement in the code
view (red dot). Keys follow VS Code:

| Key | Action |
|---|---|
| `F5` | Run / continue to the next breakpoint |
| `F6` | Pause |
| `F10` | Step over: finish the statement, called sequences included |
| `F11` | Step into: one rule application |
| `Shift+F11` | Step out: finish the current sequence |
| `Ctrl+Shift+F5`, `R` | Restart |
| `Space` | Toggle timed playback |
| `Q` | Quit |

Per-layer display settings (text or tileset, opacity, visibility, zoom,
background colour) are saved next to the script as `<file>.lvs.json`
(see `examples/dungeon.lvs.json`). The theme, window geometry and seed are
remembered per user.

## Build & test

```
cmake -S . -B build
cmake --build build --config Debug --parallel
build/tests/Debug/ls_tests.exe
python tools/run_examples.py
```
