// Showcase (v0.5.2): named tag unions.
//
// `blocker = wall | door` names a mask over the tagset's own members. Matching
// is the ordinary any-overlap rule (section 4.1), so `level[blocker]` hits a cell that
// is wall OR door. A union costs no bit and does not count toward the 30-value
// cap — it is a pure alias for the `wall|door` mask.
//
// Here `random` scatters wall and door across the grid, then a single `breach`
// rule opens every blocker (wall *or* door) in one pass — the point of the union.
//
// @seed 1
// @expect run-ok
// @expect grid level count(o) == 16    // every wall and door was a blocker → open

tag geometry { wall, door, open, blocker = wall | door }

layers {
    level: grid of geometry
}

rule scatter { where[ (random(0, 1) == 0) ] => level[wall] }
rule doors   { level[.] => level[door] }
rule breach  { level[blocker] => level[open] }

sequence main {
    resize(4, 4)
    all scatter   // ~half become wall
    all doors     // the rest become door
    all breach    // blocker matches both → open
}
