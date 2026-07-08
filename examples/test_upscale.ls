// @seed 42
// Upscale: a 2x2 grid of W cells upscaled 3x2 becomes 6x4, all W.
// @expect grid g count(W) == 24

tag t { W }
layers { g: grid of t }

rule fill { g[.] => g[W] }

program {
    resize(2, 2)
    all fill
    upscale(3, 2)
}
