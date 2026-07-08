// @seed 42
// T29: { all } on LHS requires all listed grids to match at the same position.
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
    { all
      a[X]
      b[.]
    }
    =>
    b[mark]
}
program {
    resize(3, 3)
    all fill_a
    all place
}
