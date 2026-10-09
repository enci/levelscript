// @seed 12
// Cross-grid rules: an algo layer drives geometry and loot on other layers —
// sub-rules transcode the whole grid, an all-of-any write decorates floors.
tag algo  { S, F }
tag geo   { wall, floor }
tag items { chest, heart }

layers {
    algo:  grid of algo
    level: grid of geo
    loot:  grid of items
}

rule plant {
    algo[.]
    =>
    algo[S]
}

rule spread(rotation=all) {
    algo[S .]
    =>
    algo[* F]
}

rule fill_geo { all
    algo[S] => level[floor]
    algo[F] => level[floor]
    algo[.] => level[wall]
}

rule reward {
    level[floor]
    loot[.]
    =>
    { any
      (weight=6) loot[.]
      (weight=1) loot[chest]
      (weight=1) loot[heart]
    }
}

sequence main {
    resize(9, 6)
    scatter(4) plant
    everywhere spread
    everywhere fill_geo
    everywhere reward
}
