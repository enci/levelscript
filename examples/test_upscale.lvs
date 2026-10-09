// @seed 42
// Upscale: a 2x2 grid of W cells upscaled 3x2 becomes 6x4, all W.
// @expect grid g count(W) == 24

tag t { W }
layers { g: grid of t }

rule fill { g[.] => g[W] }

sequence main {
    resize(2, 2)
    everywhere fill
    upscale(3, 2)
}
