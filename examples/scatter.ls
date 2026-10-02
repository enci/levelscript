// @seed 7
// Seeded placement: exactly five points, positions vary by seed.
tag algo { S }

layers {
    algo: grid of algo
}

rule plant {
    algo[.]
    =>
    algo[S]
}

sequence main {
    resize(10, 6)
    some(max=5) plant
}
