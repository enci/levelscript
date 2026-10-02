// E15: resize() with only one argument. As of v0.7 this parses (generic
// op_call grammar) and fails argument validation in sema (section 7.3, check 34).
// @expect error
// @expect stderr-contains requires argument
layers { }
sequence main {
    resize(10)
}
