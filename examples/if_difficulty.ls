// @seed 42
// Showcase (v0.4): built-in functions — conditional writes with `if`.
//
// A `difficulty` param drives what fills the map. `if(cond, a, b)` is eager and
// total (section 5.8/section 5.10): both branches are always evaluated but only one is kept,
// and integer ops never fault (e.g. `/ 0 == 0`), so a dead branch is always safe.
//
//   terrain: high difficulty (> 3) floods the map with lava, else grass.
//   loot:    a numeric "reward" grid — clamped so it never exceeds 5, and the
//            `100 / spacing` term is safe even when the (dead) branch divides by 0.
//
// Run it:  levelscript --param difficulty=5 examples/if_difficulty.ls
//
// @param difficulty 5
// @expect run-ok
// @expect grid terrain count(l) == 9     // difficulty 5 > 3 → all lava
// @expect grid loot count(5) == 9        // clamp(..., 0, 5) caps every cell at 5

params { difficulty: number = 0 }

tag ground { grass, lava }

layers {
    terrain: grid of ground
    loot:    grid of number
}

// Conditional tag write: lava when difficulty is high, else grass.
rule paint {
    terrain[.]
    =>
    terrain[ (if(difficulty > 3, lava, grass)) ]
}

// Numeric write using min/max/clamp and a safe (dead) division-by-zero branch.
// clamp(difficulty * 4, 0, 5) = clamp(20, 0, 5) = 5 when difficulty = 5.
rule reward {
    loot[.]
    =>
    loot[ (clamp(if(difficulty == 0, 100 / difficulty, difficulty * 4), 0, 5)) ]
}

sequence main {
    resize(3, 3)
    everywhere paint
    everywhere reward
}
