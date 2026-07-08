// @seed 42
// Showcase (v0.5): `when` statement guards + derived params.
//
// Two runtime inputs shape the generator:
//   style      — 0 selects a "cave" fill, 1 selects a "room" fill.
//   difficulty — a derived `budget` (difficulty * 3) decides whether bosses spawn.
//
// `when (expr)` gates WHETHER a statement runs (temporal), reading params only —
// complementary guards on `style` pick exactly one fill pass. This complements
// `where`, which gates WHICH cells inside a rule (spatial). Derived params name
// a function of the inputs once (`budget`) and reuse it in a guard.
//
// Run it:  mgsl --param style=0 --param difficulty=5 examples/when_style.mgsl
//
// @param style 0
// @param difficulty 5
// @expect run-ok
// @expect grid terrain count(c) == 9    // style 0 → cave fill
// @expect grid actors count(b) == 9     // budget = 15 > 6 → bosses spawn

params {
    style:      number = 0
    difficulty: number = 0
    budget = difficulty * 3
}

tag ground { cave, room }
tag mob    { boss }

layers {
    terrain: grid of ground
    actors:  grid of mob
}

rule cave_fill { terrain[.] => terrain[cave] }
rule room_fill { terrain[.] => terrain[room] }
rule spawn     { actors[.]  => actors[boss] }

program {
    resize(3, 3)
    all cave_fill when (style == 0)
    all room_fill when (style == 1)
    all spawn     when (budget > 6)
}
