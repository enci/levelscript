// @seed 42
// T37: Full dihedral group D4 (symmetry=all, rotation=all) — 8 variants.
// Rule marks the top-left cell of any 3x3 all-S block with F.
// With D4, this fires for patterns in all 8 orientations.
// Exact count depends on overlap; just verify it runs.
// @expect run-ok
// @expect grid g count(F) >= 1
tag t { S, F }
layers { g: grid of t }
rule fill { g[.] => g[S] }
rule seed_one {
    g[
        S S S
        S S S
        S S S ]
    =>
    g[
        F S S
        S S S
        S S S ]
}
rule spread(symmetry=all, rotation=all) {
    g[
        F *
        * * ]
    =>
    g[
        F *
        * F ]
}
program {
    resize(5, 5)
    all fill
    one seed_one
    all spread
}
