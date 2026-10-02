// E12: 'program' blocks were removed in 0.7 (section 6) - a run applies an entry
// sequence; tools run 'main' by default.
// @expect error
// @expect stderr-contains 'program' blocks were removed in 0.7
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program { resize(1, 1)  all r }
