// E117: rules and sequences share one namespace (§7.3 #39).
// @expect error
// @expect stderr-contains share one namespace
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence fill { resize(1, 1) }
program { resize(1, 1) }
