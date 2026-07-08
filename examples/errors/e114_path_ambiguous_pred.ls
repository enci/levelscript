// E-path-1 (v0.7): a bare-tag predicate is ambiguous when several layers could
// hold the value (R1, §7.3 #35) — name the grid with the expression form,
// e.g. from=(g1 == start).
// @expect error
// @expect stderr-contains ambiguous
tag t { start, goal, route }
layers {
    g1: grid of t
    g2: grid of t
}
program { resize(2, 2) path(from=start, to=goal, into=g1, write=route) }
