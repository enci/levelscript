// E-rand-1: `random` is binary.
// @expect error
// @expect stderr-contains takes
layers { tiles: grid of number }
rule bad { tiles[.] => tiles[ (random(3)) ] }
program { resize(1,1) }
