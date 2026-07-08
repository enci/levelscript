// E-nest-3: weight is still rejected inside { all }, including nested.
// @expect error
// @expect stderr-contains 'weight' is not allowed
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
program { resize(1,1) }
