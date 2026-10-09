// Showcase: `grow` for spacing (a greedy maximal independent set).
//
// `plant` places a tree on an `open` cell only when neither neighbour is already
// a tree (`!tree`). Under `grow` this runs one write at a time and
// re-checks after each, so no two trees ever end up adjacent — a blue-noise-ish
// spacing.
//
// The mode is the whole story here. The SAME rule under `everywhere`
// collects every candidate against one frozen snapshot — where
// no trees exist yet, so every interior cell qualifies — and packs trees solidly
// (`g t t t t …`). Sequential re-checking (`grow`) is what turns
// "plant where allowed" into "plant with spacing". Leftover `open` cells (next
// to a tree, or the two ends a 1×3 rule can't reach) become `ground`.
//
// (This file ran under v0.6's `ranked` policy before v0.7 removed it; the
//  spacing invariant lives in the match side and is indifferent to pick order.)
//
// @seed 1
// @expect run-ok
// @expect grid g count(o) == 0     // every cell resolved (tree or ground)

tag t { open, tree, ground }

layers {
    g: grid of t
}

rule init  { g[.] => g[open] }
rule plant { g[ !tree open !tree ] => g[ * tree * ] }
rule fill  { g[open] => g[ground] }

sequence main {
    resize(11, 1)
    everywhere init
    grow plant
    everywhere fill
}
