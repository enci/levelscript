// E82 (0.8): built-in names are not reserved (spec section 5.10). Followed by
// '(' a name is the built-in; otherwise it is a tag value, grid, or param.
tag t { a, random }
layers { min: grid of t  n: grid of number }
params { max: number = 3 }
rule fill { min[.] => min[random] }
rule count { min[random] n[.] => n[ (min(max, 7) + max(1, 2)) ] }
sequence main {
    resize(3, 2)
    everywhere fill
    everywhere count
}
