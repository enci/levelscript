// v0.6.3 — `pad` frames a generated region with an empty border.
//
// `pad(N)` is the one geometric verb that is NOT top-left anchored: it adds N
// empty rows/columns on every side and shifts existing content inward to
// (N, N). Here a filled 3×3 becomes a 5×5 with a one-cell empty margin — handy
// for guaranteeing a walkable edge around a generated interior.
//
// @seed 1
// @expect run-ok
// @expect grid level count(f) == 9    // the original 3×3 interior, shifted to (1,1)
// @expect grid level count(.) == 16   // the added one-cell border on all sides

tag geo { floor }

layers { level: grid of geo }

rule fill { level[.] => level[floor] }

sequence main {
    resize(3, 3)
    everywhere fill      // solid 3×3 interior
    pad(1)        // → 5×5; interior moves to (1,1)–(3,3), border is empty
}
