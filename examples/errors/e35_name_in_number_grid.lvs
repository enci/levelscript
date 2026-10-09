// E35: a tag-value name used as a cell in a 'number' grid.
// @expect error
// @expect stderr-contains expected an integer or wildcard in a 'number' grid cell
layers { g: grid of number }
rule bad {
    g[5]
    =>
    g[a]
}
sequence main { resize(1,1) }
