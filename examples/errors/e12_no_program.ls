// @expect error
// @expect stderr-contains no 'program' block
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
