// E12: 'program' is not a declaration; a run applies an entry sequence
// (tools run 'main' by default).
// @expect error
// @expect stderr-contains expected a declaration
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program { resize(1, 1)  everywhere r }
