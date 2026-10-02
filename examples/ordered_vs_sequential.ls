// Showcase (v0.7): the `ordered` combinator vs. statement-level sequencing.
//
// `ordered` gives the sub-rules a PRIORITY (declaration order) applied as an
// ordering key on the candidate vector (section 5.2). Under the default `snapshot`
// policy, higher-priority candidates pull first under the write-protection
// mask — so where two sub-rules write the SAME cell, the higher one claims it
// and the lower one is skipped. It is NOT "apply s1 everywhere, then s2 as a
// second pass" — that global sequencing is a statement-level concern (separate
// statements), and a later statement CAN overwrite an earlier one.
//
// The two grids below are seeded identically (a diagonal of `seed`, the rest
// empty) and classified two ways:
//
//   perCell  — one `ordered` rule:  seed → gold (s1), everything else → plain
//              (s2). At a `seed` cell both sub-rules want the cell; s1 pulls
//              first and the mask blocks s2 there. Result: gold on the
//              diagonal, plain elsewhere.
//
//   global   — two separate statements: `toGold` then `toPlain`. `toPlain`'s
//              pattern (`*`) matches EVERY cell, including the gold `toGold`
//              just wrote, so it overwrites it. Result: all plain — the gold
//              is lost.
//
// Takeaway: use `ordered` when, at a single cell, one rule should take priority
// over another. Use separate statements for global "do s1's rule, then s2's" —
// but then a later pass can overwrite an earlier one (design s2 to match only
// the leftovers if you don't want that).
//
// @seed 1
// @expect run-ok
// @expect grid perCell count(g) == 3     // `ordered` protects the diagonal gold
// @expect grid perCell count(p) == 6
// @expect grid global count(g) == 0      // sequential `*` pass clobbered it
// @expect grid global count(p) == 9

tag t { seed, gold, plain }

layers {
    perCell: grid of t
    global:  grid of t
}

rule sowA { where[ (x == y) ] => perCell[seed] }
rule sowB { where[ (x == y) ] => global[seed] }

// Same-cell priority: at a `seed` cell s1 claims the write; s2 is masked out.
rule classify { ordered
    perCell[seed] => perCell[gold]
    perCell[*]    => perCell[plain]
}

// Global sequencing: the second statement overwrites the first (its `*` matches gold).
rule toGold  { global[seed] => global[gold] }
rule toPlain { global[*]    => global[plain] }

sequence main {
    resize(3, 3)
    all sowA
    all sowB
    all classify
    all toGold
    all toPlain
}
