// @seed 42
// T12: 'grid of number' accepts integer literals as cell values.
// @expect grid heat count(5) == 16
layers { heat: grid of number }
rule warm { heat[.] => heat[5] }
sequence main { resize(4, 4) all warm }
