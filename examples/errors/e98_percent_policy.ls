// E-pol-1: percent requires the default snapshot policy.
// @expect error
// @expect stderr-contains 'percent' requires
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) some(percent=50, policy=incremental) r }
