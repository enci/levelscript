// @seed 18
// Params, where, and when together: a paved arena with a wall frame; lava
// hazards only when the difficulty param says so (default 5; try
// --param difficulty=2 to disable the hazard pass).
tag geo { wall, floor, lava }

layers {
    level: grid of geo
}

params {
    difficulty: number = 5
}

rule pave {
    level[.]
    =>
    level[floor]
}

rule frame {
    { all
      level[floor]
      where[ (x == 0 || y == 0 || x == width - 1 || y == height - 1) ]
    }
    =>
    level[wall]
}

rule hazards {
    level[floor]
    =>
    { any
      (weight=9) level[floor]
      (weight=1) level[lava]
    }
}

program {
    resize(12, 7)
    all pave
    all frame
    all hazards  when (difficulty > 3)
}
