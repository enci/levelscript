// Module (section 2.6): rules and a sequence over the schema's grid. It uses
// schema.ls directly, so it sees `level` and `algo`.
use "schema.ls"

rule wall_all { level[.] => level[W] }
rule open_room {
    { all
      level[W]
      where[ (x > 0 && x < width - 1 && y > 0 && y < height - 1) ]
    }
    =>
    level[R]
}

sequence carve {
    resize(8, 5)
    all wall_all
    all open_room
}
