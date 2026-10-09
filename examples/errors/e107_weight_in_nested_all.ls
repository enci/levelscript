// E-nest-3: weight is still rejected inside { all }, including nested.
// @expect error
// @expect stderr-contains only allowed on '{ any }' items
tag geometry { floor }
tag item { chest }
layers { level: grid of geometry  items: grid of item }
rule bad {
    level[.] items[.]
    =>
    { all
      (weight=2) level[floor]
      items[chest]
    }
}
sequence main { resize(1,1) }
