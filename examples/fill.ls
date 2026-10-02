// @seed 42
// The smallest complete generator: fill the level with floor.
tag geo { wall, floor }

layers {
    level: grid of geo
}

rule fill {
    level[.]
    =>
    level[floor]
}

sequence main {
    resize(8, 4)
    all fill
}
