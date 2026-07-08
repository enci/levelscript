// @expect error
// @expect stderr-contains program
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
