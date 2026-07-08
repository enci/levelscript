// E28: integer literal used as a cell value in a tagged (non-number) grid.
// @expect error
// @expect stderr-contains non-number grid
tag t { a, b }
layers { g: grid of t }
rule bad {
    g[a]
    =>
    g[5]
}
program { resize(1,1) }
