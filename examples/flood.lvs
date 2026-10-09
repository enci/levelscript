// @seed 33
// Settle: iterated sweeps to a fixpoint. Water floods from one spring
// through the open cells; the rock mix bounds the pools.
tag geo { rock, water }

layers {
    level: grid of geo
}

rule scatter_rocks {
    level[.]
    =>
    { any
      (weight=1) level[rock]
      (weight=2) level[.]
    }
}

rule spring {
    level[.]
    =>
    level[water]
}

rule flow(rotation=all) {
    level[water .]
    =>
    level[* water]
}

sequence main {
    resize(15, 9)
    everywhere scatter_rocks
    once spring
    settle flow
}
