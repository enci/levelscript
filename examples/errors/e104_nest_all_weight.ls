// E104 (v0.6.4): weight stays illegal inside { all }, including when nested
// (§7.3 #5). Weights only bias the items of an { any }.
// @expect error
// @expect stderr-contains weight
tag geometry { floor }
tag item { chest }
layers { level: grid of geometry  items: grid of item }
rule bad {
    { all level[.] items[.] }
    =>
    { all
      (weight=2) level[floor]
      items[chest]
    }
}
sequence main { resize(1,1) }
