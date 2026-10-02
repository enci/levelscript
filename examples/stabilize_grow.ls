// @seed 42
// Showcase (v0.6): the `stabilize` policy — a synchronous cellular automaton.
//
// `all(policy=stabilize)` runs full `snapshot` sweeps back-to-back, re-snapshotting
// between them, until a sweep changes nothing (a fixpoint, §6.4/§6.6). Each sweep
// updates every cell against the *previous* generation (synchronous), unlike
// `incremental` which sees each write immediately.
//
// Here a border of walls grows inward one ring per generation. Because each
// sweep sees only the last generation, both sides advance in lockstep until the
// walls meet in the middle — then a final no-change sweep detects the fixpoint
// and stops. (This is the CA mechanic; a real cave smoother would keep some
// floors, which needs neighbour-count rules — see the graph/WFC parked item.)
//
// @expect run-ok
// @expect grid g count(f) == 0     // grown to a fixpoint: no floor remains
// @expect grid g count(w) == 36    // 6 × 6 all wall

tag t { wall, floor }

layers {
    g: grid of t
}

rule border { where[ (x == 0 || y == 0 || x == width - 1 || y == height - 1) ] => g[wall] }
rule mid    { g[.] => g[floor] }

// A floor cell adjacent to a wall becomes wall (`*` preserves the wall cell, so
// the four rotated variants never conflict on it).
rule grow(rotation=all) { g[wall floor] => g[* wall] }

sequence main {
    resize(6, 6)
    all border
    all mid
    all(policy=stabilize) grow
}
