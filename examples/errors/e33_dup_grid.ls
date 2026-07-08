// @expect error
// @expect stderr-contains duplicate grid
tag t { a }
layers {
    g: grid of t
    g: grid of t
}
program { resize(1,1) }
