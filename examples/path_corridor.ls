// Showcase (v0.7): `path` — the first structural operation (§6.6).
//
// A wall splits the map in two; the only opening is at (5, 5). `path` computes
// a shortest floor-only route from the `door` marker to the `exit` marker and
// stamps `corridor` into the marks layer along it, endpoints included.
//
// Every shortest route must pass the gap, so its length is fixed:
// manhattan((1,1)→(5,5)) + manhattan((5,5)→(9,1)) + 1 = 8 + 8 + 1 = 17 cells —
// but WHICH equally-short corridor gets carved varies with the seed (ties break
// by seed, §7.2). Downstream rules would consume the route with ordinary
// same-position reads (e.g. `marks[corridor] => level[floor]`).
//
// @seed 3
// @expect run-ok
// @expect grid marks count(c) == 17     // corridor, endpoints included
// @expect grid marks count(d) == 0      // door overwritten by the route
// @expect grid marks count(e) == 0      // exit overwritten by the route

tag g { floor, wall }
tag m { door, exit, corridor }

layers {
    level: grid of g
    marks: grid of m
}

rule barrier { where[ (x == 5 && y != 5) ] => level[wall] }
rule ground  { level[.] => level[floor] }
rule dd { where[ (x == 1 && y == 1) ] => marks[door] }
rule ee { where[ (x == 9 && y == 1) ] => marks[exit] }

program {
    resize(11, 7)
    all barrier
    all ground
    all dd
    all ee
    path(from=door, to=exit, into=marks, write=corridor, passable=floor)
}
