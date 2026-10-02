// @seed 5
// Weighted alternatives: every cell becomes floor or wall, biased 3:1.
tag geo { wall, floor }

layers {
    level: grid of geo
}

rule mix {
    level[.]
    =>
    { any
      (weight=3) level[floor]
      (weight=1) level[wall]
    }
}

sequence main {
    resize(12, 8)
    all mix
}
