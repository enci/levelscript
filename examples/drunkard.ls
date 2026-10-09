// @seed 42
// Drunkard's walk corridor generator.
// A single walker starts in the middle and meanders, leaving floor behind.
// @expect grid level count(f) >= 5

tag algo    { S, F }
tag terrain { wall, floor }
layers {
    a:     grid of algo
    level: grid of terrain
}

rule start {
    a[.]
    =>
    a[S]
}

// Walker moves one step in any cardinal direction into empty space.
// 1x2 pattern works right up to the grid border (unlike 3x3).
rule walk(rotation=all) {
    a[S .]
    =>
    a[F S]
}

rule finalize { all
    a[F] => level[floor]
    a[S] => level[floor]
}

sequence main {
    resize(40, 20)
    once start
    grow(300) walk
    everywhere finalize
}
