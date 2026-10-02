// Showcase (v0.7): an unreachable `path` goal is a WARNING and a no-op (section 6.6),
// never an error — like `trim` on an entirely empty stack.
//
// The wall column has no gap and `passable=floor`, so no route exists; the
// grids are left unchanged (the door/exit markers survive).
//
// @seed 1
// @expect run-ok
// @expect stderr-contains no route
// @expect grid marks count(c) == 0     // nothing stamped
// @expect grid marks count(d) == 1     // door still there
// @expect grid marks count(e) == 1     // exit still there

tag g { floor, wall }
tag m { door, exit, corridor }

layers {
    level: grid of g
    marks: grid of m
}

rule barrier { where[ (x == 3) ] => level[wall] }
rule ground  { level[.] => level[floor] }
rule dd { where[ (x == 0 && y == 1) ] => marks[door] }
rule ee { where[ (x == 6 && y == 1) ] => marks[exit] }

sequence main {
    resize(7, 3)
    all barrier
    all ground
    all dd
    all ee
    path(from=door, to=exit, into=marks, write=corridor, passable=floor)
}
