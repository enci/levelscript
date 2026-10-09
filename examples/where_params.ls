// @seed 42
// Showcase (v0.3): the `where` pseudo-layer + a runtime `params` input.
//
// `sea_level` is a number supplied at runtime (like the seed). The `flood`
// rule uses a `where` position guard to turn the top `sea_level` rows into
// water; everything else becomes grass. Raising sea_level floods more of the
// map from the top.
//
// Run it:  mgsl --param sea_level=2 examples/where_params.mgsl
//
// @param sea_level 2
// @expect run-ok
// @expect grid map count(w) == 8     // top 2 of 4 rows × 4 cols
// @expect grid map count(g) == 8     // the remaining land

params { sea_level: number = 0 }

tag terrain { water, grass }

layers {
    map: grid of terrain
}

// Water fills every still-empty cell whose row is above the sea level.
rule flood {
    map[.]
    where[ (y < sea_level) ]
    =>
    map[water]
}

// Everything left becomes grass.
rule land { map[.] => map[grass] }

sequence main {
    resize(4, 4)
    everywhere flood
    everywhere land
}
