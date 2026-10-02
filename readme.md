# LevelScript

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
    all fill
}
```

```cpp
auto gen   = ls::generator::compile(source, "dungeon.ls", resolve);  // resolve maps `use` paths
auto level = gen.generate(gen.sequence("main"), seed);
auto geo   = level["level"];
int  wall  = gen.tag("geo.wall");
if (geo.at(x, y) == wall) ...
```

## Build & test

```
cmake -S . -B build
cmake --build build --config Debug --parallel
build/tests/Debug/ls_tests.exe
python run_examples.py
```
