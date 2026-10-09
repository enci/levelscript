// E117: rules and sequences share one namespace (section 7.3, check 39).
// @expect error
// @expect stderr-contains share one namespace
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence fill { resize(1, 1) }
sequence main { resize(1, 1) }
