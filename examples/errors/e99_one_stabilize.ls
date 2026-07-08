// E-pol-2: one with stabilize is contradictory.
// @expect error
// @expect stderr-contains contradictory
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program { resize(2,2) one(policy=stabilize) r }
