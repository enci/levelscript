// E29: Using a tagset name ('t') where a tag value is expected.
// @expect error
// @expect stderr-contains unknown tag value
tag t { a, b }
layers { g: grid of t }
rule bad {
    g[a]
    =>
    g[t]
}
program { resize(1,1) }
