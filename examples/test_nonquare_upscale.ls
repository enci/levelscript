// @seed 42
// T44: non-square upscale multiplies width and height independently.
// 2x1 grid upscale(3,2) → 6 cols x 2 rows = 12 cells.
// @expect grid g count(W) == 12
tag t { W }
layers { g: grid of t }
rule fill { g[.] => g[W] }
program {
    resize(2, 1)
    all fill
    upscale(3, 2)
}
