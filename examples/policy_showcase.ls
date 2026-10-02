// Showcase: execution policies + the `ordered` sub-rule combinator (v0.7).
//
// v0.6 split the old strategy trio into a count (one/all/some) and an execution
// policy (snapshot default, incremental, stabilize). v0.7 replaced `first` with
// `ordered` (sub-rule priority as an ordering key) and removed `ranked`. This
// file exercises the grammar; the exact output is seed-fixed.
//
// @seed 1
// @expect run-ok

tag t { a, b, c }

layers { g: grid of t }

rule seed   { g[.] => g[a] }
rule spread(rotation=all) { g[a b] => g[a a] }
rule recolor {
    ordered
    g[a] => g[b]
    g[a] => g[c]
}

sequence main {
    resize(6, 6)
    all seed
    all(policy=stabilize) spread       // sweep to fixpoint
    some(percent=50) recolor           // half of one snapshot batch, s1 claims first
    one(policy=incremental) recolor    // a single sequential application
}
