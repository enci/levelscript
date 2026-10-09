// E121 (0.8): a scatter percentage written above 100 (section 7.3, check 30).
// @expect error
// @expect stderr-contains above 100
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence main { resize(2,2) scatter(150%) r }
