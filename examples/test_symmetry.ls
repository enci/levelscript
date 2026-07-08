// Symmetry expansion: [S F] with symmetry=all also matches [F S].
// Fill a 1x4 grid: S F S F. After sym rule, all pairs become F S.
// The swap rule's matches overlap (snapshot conflict-resolves by the seeded
// shuffle), so the clean 2/2 swap is seed-dependent; pin a seed that yields it.
// @seed 6
// @expect grid g count(F) == 2
// @expect grid g count(S) == 2

tag t { S, F }
layers { g: grid of t }

rule seed_sf { g[.] => g[S] }

// Alternate: positions 0,2 get S; 1,3 get F via this trick
rule alt {
    g[ S S ]
    =>
    g[ S F ]
}

// With symmetry=all: [S F] => [F S] also matches [F S] (flipped) => [S F]
// Net effect: every [S F] pair gets swapped.
rule swap(symmetry=all) {
    g[ S F ]
    =>
    g[ F S ]
}

program {
    resize(4, 1)
    all seed_sf
    all alt        // result: S F S F
    all swap       // result: F S F S  (each pair swapped)
}
