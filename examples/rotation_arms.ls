// @seed 42
// Showcase (v0.3.1): explicit rotation angles and the set form `rotation={..}`.
//
// A single `core` is placed at the grid centre, then `grow` extends an arm into
// the empty cell "ahead of" it. The rotation attribute decides which directions
// the arm grows:
//
//   rotation=all        → 4 arms (up, down, left, right)   — the full turn set
//   rotation=180        → 2 arms (right + left)            — a single half-turn
//   rotation={90, 270}  → 3 arms (right, down, up)         — identity + 90 + 270,
//                         but NOT left, because 180 is excluded from the set
//
// This file uses the set form. `{ }` is the set delimiter (as in tag value
// sets), never `[ ]` (which is pattern-body only). Identity is always included,
// so {90, 270} yields three orientations.
//
// The RHS keeps the core with `*` (preserve) so only the arm cells are written —
// that way the four rotated variants never conflict on the shared core cell.
//
// @expect run-ok
// @expect grid g count(c) == 1     // one core at the centre
// @expect grid g count(a) == 3     // right, down, up — no left (180 excluded)

tag t { core, arm }

layers {
    g: grid of t
}

// Place a single core at the centre via `where` position guards.
rule seed {
    { all
      g[.]
      where[ (x == width / 2 && y == height / 2) ]
    }
    =>
    g[core]
}

// Grow an arm into the empty cell ahead of a core, in three orientations.
rule grow(rotation={90, 270}) {
    g[core .]
    =>
    g[*    arm]
}

sequence main {
    resize(5, 5)
    all seed
    all grow
}
