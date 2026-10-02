// E-empty-1: a `.` with no inferable type.
// @expect error
// @expect stderr-contains no inferable type
tag t { a }
layers { g: grid of t }
rule bad { where[ (. == .) ] => g[a] }
sequence main { resize(1,1) }
