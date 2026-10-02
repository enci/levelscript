// @seed 51
// The path operation: carve the cheapest corridor between the door and the
// exit. The rocky field is expensive to cross, so the road winds through
// the cheap cells; equal-cost routes vary by seed, reproducibly.
tag site { door, exit, road }

layers {
    site:   grid of site
    rocks:  grid of number
}

rule scatter_rocks {
    rocks[.]
    =>
    rocks[ (random(0, 6)) ]
}

rule place_door {
    { all site[.] where[ (x == 0 && y == height / 2) ] }
    =>
    site[door]
}

rule place_exit {
    { all site[.] where[ (x == width - 1 && y == height / 2) ] }
    =>
    site[exit]
}

sequence main {
    resize(16, 9)
    all scatter_rocks
    all place_door
    all place_exit
    path(from=door, to=exit, into=site, write=road,
         passable=((0 == 0)), cost=(1 + rocks))
}
