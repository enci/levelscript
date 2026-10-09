// E124 (0.8): 'grow' selects among one rule's candidates; not valid on a sequence (check 37).
// @expect error
// @expect stderr-contains 'grow' is not valid on sequence
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence s { everywhere r }
sequence main { resize(2, 2)  grow(3) s }
