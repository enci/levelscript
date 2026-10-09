// The worked example of spec section 8, verbatim: a dungeon sketched in `algo`
// by a drunkard's walk, upscaled, then converted into geometry, rewards,
// enemies, and tile indices. Keep it in sync with the spec.
// @expect run-ok

tag items     { chest, heart, potion, sword, shield }
tag enemies   { goblin, troll, dragon }
tag algo      { F, W, S }
tag geometry  { wall, floor }

layers {
    level:   grid of geometry
    tiles:   grid of number
    enemies: grid of enemies
    items:   grid of items
    algo:    grid of algo
}

rule rwalk(rotation=all) {
    algo[
        * * *
        * S W
        * * * ]
    =>
    { any
      (weight=2) algo[
          * * *
          * F S
          * * * ]
      (weight=1) algo[
          * S *
          * F *
          * * * ]
    }
}

rule reduce(rotation=all) {
    algo[
        S S
        S S ]
    =>
    algo[
        S F
        F F ]
}

rule reward {
    algo[S]
    =>
    { any
      items[chest]
      items[heart]
      items[potion]
      items[sword]
      items[shield]
    }
}

rule place_enemies {
    algo[F]
    enemies[.]
    =>
    { any
      enemies[goblin]
      enemies[troll]
      enemies[dragon]
    }
}

sequence main {
    resize(60, 40)
    everywhere { algo[.] => algo[W] }
    scatter(5) { algo[W] => algo[S] }
    grow(100)  rwalk
    upscale(2, 2)
    everywhere reduce
    everywhere reward
    everywhere { all
        algo[W] => level[wall]
        algo[F] => level[floor]
        algo[S] => level[floor]
    }
    scatter(10) place_enemies
    everywhere { all
        level[floor] => tiles[1]
        level[wall]  => tiles[2]
    }
}
