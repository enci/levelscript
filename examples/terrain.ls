// @seed 26
// Expressions across grids: a random depth field classified into terrain by
// where-guarded thresholds (cross-grid reads at the same position).
tag geo { water, grass, rock }

layers {
    depth: grid of number
    land:   grid of geo
}

rule roll {
    depth[.]
    =>
    depth[ (random(0, 9)) ]
}

rule classify { all
    { all land[.] where[ (depth <= 2) ] }               => land[water]
    { all land[.] where[ (depth > 2 && depth <= 6) ] } => land[grass]
    { all land[.] where[ (depth > 6) ] }                => land[rock]
}

program {
    resize(14, 8)
    all roll
    all classify
}
