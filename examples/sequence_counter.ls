// Named sequence (section 6.10): a saturating counter applied to a fixpoint.
// `all tick` iterates the body until an iteration changes nothing: the
// counter climbs to its cap of 3, then one more (stable) iteration ends it.
// No randomness, so this golden is identical on every platform.
// @expect run-ok
layers { tiles: grid of number }
rule inc {
    tiles[*]
    where[ (tiles < 3) ]
    =>
    tiles[ (tiles + 1) ]
}
sequence tick { all inc }
sequence main {
    resize(3, 2)
    all tick
}
