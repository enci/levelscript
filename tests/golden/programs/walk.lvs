// @seed 21
// Step-mode growth (`grow`): each application re-collects and sees prior writes,
// so the blob feeds on itself — the drunkard's-walk family. Under the
// batch modes the same rule could only ring the first seed.
tag algo { S }

layers {
    algo: grid of algo
}

rule plant {
    algo[.]
    =>
    algo[S]
}

rule step(rotation=all) {
    algo[S .]
    =>
    algo[* S]
}

sequence main {
    resize(13, 9)
    once plant
    grow(45) step
}
