// @seed 42
// T34: '{ all }' on LHS (cross-grid match) and '{ any }' on RHS (alternatives).
// Every cell qualifies: level[floor] AND actors[.].
// After 'all place', every actor cell holds a monster — none stay empty.
// @expect grid level count(f) == 9
// @expect grid actors count(.) == 0
tag terrain { floor }
tag entity  { goblin, troll }
layers {
    level:  grid of terrain
    actors: grid of entity
}
rule fill_floor { level[.] => level[floor] }
rule place {
    { all
      level[floor]
      actors[.]
    }
    =>
    { any
      actors[goblin]
      actors[troll]
    }
}
program {
    resize(3, 3)
    all fill_floor
    all place
}
