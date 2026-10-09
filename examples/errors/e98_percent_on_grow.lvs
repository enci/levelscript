// E98 (0.8): only 'scatter' takes a percentage (section 6.7).
// @expect error
// @expect stderr-contains only 'scatter' takes a percentage
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) grow(50%) r }
