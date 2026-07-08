// v0.6.4 — nested write-side combinators.
//
// The write side is now a recursive tree: an { all } or { any } item may
// itself be a block. Two composite shapes this unlocks at one matched cell:
//
//   all-of-any  — write one grid deterministically AND choose another
//                 independently. Here: floor is always laid; an item is drawn
//                 per cell (mostly nothing, occasionally a chest/heart).
//
//   any-of-all  — pick ONE correlated multi-layer combination, so the layers
//                 move together (shown commented below: floor⇔chest / wall⇔empty).
//
// Each { any } node contributes exactly one PRNG draw per application, in
// declaration order (§10.7) — deterministic per seed.
//
// @seed 1
// @expect run-ok
// @expect grid level count(f) == 64    // floor laid at every cell (the { all } arm)

tag geometry { floor }
tag item { chest, heart }

layers {
    level: grid of geometry
    items: grid of item
}

// all-of-any: level[floor] is unconditional; the item is an independent choice.
rule furnish {
    { all level[.] items[.] }
    =>
    { all
      level[floor]
      { any
        (weight=8) items[.]
        (weight=1) items[chest]
        (weight=1) items[heart]
      }
    }
}

program {
    resize(8, 8)
    all furnish
}

// any-of-all alternative (correlated layers) would read:
//   => { any
//        { all level[floor] items[chest] }   // floor always carries a chest
//        { all level[wall]  items[.] }        // wall is always empty
//      }
