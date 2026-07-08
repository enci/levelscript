// Showcase (v0.5.1): the `random` built-in — stochastic placement.
//
// `random(lo, hi)` returns an inclusive integer in [lo, hi] and advances the
// shared seeded PRNG (§5.10/§10.7) — the one impure expression. Here a `where`
// guard rolls a per-cell die during the match scan: each cell has ~30% chance
// of becoming rubble, and the rest are filled with floor. The exact scatter is
// fixed for a given --seed (determinism is per seed, per implementation version).
//
// Run it:  mgsl --seed 42 examples/random_scatter.mgsl
//
// @seed 42
// @expect run-ok
// @expect grid ground count(.) == 0    // every cell resolved to floor or rubble

tag terrain { floor, rubble }

layers {
    ground: grid of terrain
}

// A match-side `random` draw gates placement per candidate (~30%).
rule scatter { where[ (random(1, 100) <= 30) ] => ground[rubble] }

// Everything still empty becomes floor.
rule fill { ground[.] => ground[floor] }

program {
    resize(8, 8)
    all scatter
    all fill
}
