// E115 (0.8): a sequence takes 'once' or 'settle' only (section 7.3, check 37).
// @expect error
// @expect stderr-contains 'everywhere' is not valid on sequence 's'
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
sequence s { everywhere r }
sequence main { resize(2, 2)  everywhere s }
