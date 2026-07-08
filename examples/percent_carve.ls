// @seed 42
// Showcase (v0.6): the `percent` count with the default `snapshot` policy.
//
// `some(percent=P)` applies P% of one snapshot batch's matches — ⌊P·total/100⌋,
// an *exact* fraction of the candidate set (unlike a per-cell `random` gate,
// which has variance; cf. random_scatter.mgsl). It is snapshot-only, since the
// full match set is the percentage's denominator.
//
// Here every cell is rock (100 matches), then exactly 40% are carved to floor.
//
// @expect run-ok
// @expect grid g count(f) == 40    // exactly 40% of 100 carved
// @expect grid g count(r) == 60

tag t { rock, floor }

layers {
    g: grid of t
}

rule solid { g[.] => g[rock] }
rule carve { g[rock] => g[floor] }

program {
    resize(10, 10)
    all solid
    some(percent=40) carve
}
