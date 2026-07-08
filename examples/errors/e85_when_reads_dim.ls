// E-when-2: width/height are not readable at statement scope.
// @expect error
// @expect stderr-contains cannot be read here
tag t { a }
layers { g: grid of t }
rule r { g[.] => g[a] }
program { resize(2,2) all r when (width > 1) }
