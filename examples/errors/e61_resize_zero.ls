// E61: resize(0, 0) — dimensions must be positive.
// @expect error
// @expect stderr-contains dimensions must be positive
layers { }
program {
    resize(0, 0)
}
