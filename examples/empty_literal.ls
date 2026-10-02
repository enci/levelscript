// Showcase (v0.6.1): the `.` empty literal in expressions.
//
// `.` names the empty value inside ( ) — so you can test empty / non-empty and
// conditionally clear a cell. It is polymorphic: typed from the grid or the
// operand it is compared against.
//
// @seed 1
// @expect run-ok

tag geometry { floor, wall }

layers {
    level: grid of geometry
    heat:  grid of number
}

rule seed  { where[ (x == 0) ] => level[wall] }
rule fill  { where[ (level == .) ] => level[floor] }   // empty test
rule scar  { level[ (if(level != ., floor, .)) ] => level[ * ] }  // non-empty test + conditional clear
rule warm  { where[ (heat == .) ] => heat[ (heat + 1) ] }  // empty number reads as 0

sequence main {
    resize(4, 4)
    all seed
    all fill
    all warm
}
