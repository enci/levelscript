// Showcase: modes + the `ordered` sub-rule combinator (v0.8).
//
// v0.8 replaced the count x policy pair with five modes (once, scatter,
// everywhere, grow, settle); v0.7 had replaced `first` with `ordered`
// (sub-rule priority as an ordering key). This file exercises the grammar;
// the exact output is seed-fixed.
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
    everywhere seed
    settle spread       // sweep to fixpoint
    scatter(50%) recolor           // half of one snapshot batch, s1 claims first
    once recolor    // a single uniform pick
}
