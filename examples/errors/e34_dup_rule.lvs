// @expect error
// @expect stderr-contains duplicate rule
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
rule r { g[.] => g[a] }
sequence main { resize(1,1) }
