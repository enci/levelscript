// E100: mirror takes horizontal or vertical — nothing else (§7.3 #35 as of v0.7).
// @expect error
// @expect stderr-contains invalid mirror axis
tag t { a }
layers { g: grid of t }
program { resize(2,2) mirror(diagonal) }
