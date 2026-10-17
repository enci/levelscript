// Showcase: WFC-flavoured constraint collapse under `grow`.
//
// A 1-D "wave function collapse" 3-colouring: every interior cell must differ
// from both neighbours (r / g / b). Undecided cells are `open`; the two ends are
// pinned as clues so every interior cell has both neighbours in bounds.
//
// The constraint lives entirely in the MATCH side: one "observe" sub-rule per
// colour whose LHS only matches when neither neighbour already forbids that
// colour (`!X` = a decided cell that is not X). Because `grow`
// re-collects after every write, each collapse sees its neighbours' latest
// values, so it never picks a conflicting colour — no backtracking needed: an
// open interior cell always has at least one legal colour of three (its two
// neighbours can forbid at most two).
//
// (v0.6's `ranked` policy ran this same file lowest-entropy-first; v0.7 removed
//  it — the ordering was flavour, the correctness is the match side. Sequential
//  re-checking is what enforces the constraint, and `grow` provides it.)
//
// @seed 1
// @expect run-ok
// @expect grid line count(o) == 0     // every cell collapsed to a colour

tag c { open, r, g, b }

layers {
    line: grid of c
}

rule init  { line[.] => line[open] }
rule seedL { where[ (x == 0) ]         => line[r] }
rule seedR { where[ (x == width - 1) ] => line[b] }

rule collapse { all
    line[ !r open !r ] => line[ * r * ]
    line[ !g open !g ] => line[ * g * ]
    line[ !b open !b ] => line[ * b * ]
}

sequence main {
    resize(9, 1)
    everywhere init
    everywhere seedL
    everywhere seedR
    grow collapse
}
