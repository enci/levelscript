// Showcase (v0.5.3): param defaults — this generator runs seed-only.
//
// An input param may carry a default (`difficulty: number = 3`), evaluated once
// at startup only when the runtime doesn't supply it. A later param's default
// may read an earlier one (same scope discipline as a derived param). Supply
// `--param difficulty=N` to override; otherwise the defaults drive the run.
//
// Run it:  mgsl --seed 1 examples/param_defaults.mgsl
//     or:  mgsl --seed 1 --param difficulty=5 examples/param_defaults.mgsl
//
// @seed 1
// @expect run-ok
// @expect grid loot count(6) == 4    // budget = difficulty(3) * 2 = 6

params {
    difficulty: number = 3
    budget:     number = difficulty * 2
}

layers {
    loot: grid of number
}

rule fill { loot[.] => loot[ (budget) ] }

sequence main {
    resize(2, 2)
    all fill
}
