// E-pol-3: unknown policy value.
// @expect error
// @expect stderr-contains unknown policy
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) all(policy=greedy) r }
