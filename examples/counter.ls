// @seed 42
// Showcase (v0.3): a number grid used as a counter via a self-read expression.
//
// `bump` matches any cell that already holds a number and writes that value
// plus one. Because matches read the pre-statement snapshot (section 7.1), every
// cell increments once per `all bump` pass. Three passes → every cell holds 3.
//
// @expect run-ok
// @expect grid n count(3) == 4

layers {
    n: grid of number
}

rule zero { n[.] => n[0] }
rule bump { n[ (n) ] => n[ (n + 1) ] }

sequence main {
    resize(2, 2)
    all zero
    all bump
    all bump
    all bump
}
