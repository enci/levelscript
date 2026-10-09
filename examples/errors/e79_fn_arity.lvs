// E79: built-in 'min' requires two arguments.
// @expect error
// @expect stderr-contains takes
layers { tiles: grid of number }
rule bad { tiles[.] => tiles[ (min(3)) ] }
sequence main { resize(1,1) }
