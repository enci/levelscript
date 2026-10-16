// @seed 42
// T13: Two tagsets may declare the same value name; resolved by grid's tagset.
// Rule uses ga[X] which resolves to ta.X, not tb.X.
// @expect grid ga count(X) == 4
// @expect grid gb count(X) == 0
tag ta { X, Y }
tag tb { X, Z }
layers {
    ga: grid of ta
    gb: grid of tb
}
rule r { ga[.] => ga[X] }
sequence main { resize(2, 2) everywhere r }
