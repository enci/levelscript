// @seed 44
// Geometry composition: generate a quarter, mirror twice for 4-fold symmetry,
// then frame it. Layers stay co-registered through every operation.
tag geo { wall, floor }

layers {
    level: grid of geo
}

rule carve {
    level[.]
    =>
    { any
      (weight=2) level[floor]
      (weight=1) level[wall]
    }
}

rule frame {
    level[*]
    where[ (x == 0 || y == 0 || x == width - 1 || y == height - 1) ]
    =>
    level[wall]
}

sequence main {
    resize(6, 4)
    all carve
    mirror(horizontal)
    mirror(vertical)
    pad(1)
    all frame
}
