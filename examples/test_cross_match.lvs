// @seed 42
// T29: match-side patterns conjoin: all listed grids must match at the same position.
// Fill 'a' with X; every cell then satisfies both a[X] and b[.].
// (v0.3: tag value renamed from 'y' — that is now a reserved expression name.)
// @expect grid b count(m) == 9
tag ta { X }
tag tb { mark }
layers {
    a: grid of ta
    b: grid of tb
}
rule fill_a { a[.] => a[X] }
rule place {
    a[X]
    b[.]
    =>
    b[mark]
}
sequence main {
    resize(3, 3)
    everywhere fill_a
    everywhere place
}
