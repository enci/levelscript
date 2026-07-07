// @seed 9
// Rotation variants: seeds sprout into all four neighbours in one batch —
// the [S .] rule matches left-to-right as written, and up/down/right-to-left
// through its rotation=all variants.
tag algo { S }

layers {
    algo: grid of algo
}

rule plant {
    algo[.]
    =>
    algo[S]
}

rule sprout(rotation=all) {
    algo[S .]
    =>
    algo[* S]
}

program {
    resize(11, 7)
    some(max=3) plant
    all sprout
}
