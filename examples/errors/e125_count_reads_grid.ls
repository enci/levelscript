// E125 (0.8): a count reads params only, like a guard (section 7.3, check 21).
// @expect error
// @expect stderr-contains cannot be read here
tag t { a }
layers { g: grid of t  n: grid of number }
rule r { g[.] => g[a] }
sequence main { resize(2, 2) scatter(n) r }
