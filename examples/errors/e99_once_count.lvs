// E99 (0.8): 'once' takes no count.
// @expect error
// @expect stderr-contains takes no count
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) once(3) r }
