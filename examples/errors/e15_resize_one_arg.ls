// E15: resize() with only one argument. As of v0.7 this parses (generic
// op_call grammar) and fails argument validation in sema (§7.3 #34).
// @expect error
// @expect stderr-contains requires argument
layers { }
program {
    resize(10)
}
