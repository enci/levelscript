// E62: upscale(0, 1) — scale factors must be positive.
// @expect error
// @expect stderr-contains factors must be positive
layers { }
program {
    resize(2, 2)
    upscale(0, 1)
}
