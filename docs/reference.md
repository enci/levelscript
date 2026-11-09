# LevelScript reference

The user-facing reference shown in the lab's Reference panel. `tools/build_site_data.py`
merges each section below into the entry of the same name, which it reads from the spec's
tables (signatures stay single-sourced there). The build fails if an entry has no section
here, or a section names no entry.

Format of a section:

- `## name` - the entry, as listed in the panel.
- The first paragraph is the one-line summary shown in the list.
- Everything after it is the detail shown when the entry is opened: paragraphs, `-` lists,
  and fenced `ls` examples. Inline `code`, **bold** and *italic* are supported.
- An optional last line `See also: a, b, c` links related entries.

A section may also add an entry the spec does not tabulate, by declaring its kind and
signature in the heading: `## symmetry (attribute: symmetry=none|horizontal|vertical|all)`.

<!-- entries -->

## resize

Set the size of every layer to W x H. Usually the first statement of a generator.

- `w`, `h` - the new width and height. Any number expression: `resize(size * 2, size)`.

Existing content is kept and anchored at the top-left. Growing adds empty cells on the right and bottom; shrinking cuts off the right and bottom. Every grid always has the same size, so there is no per-layer size.

```ls
sequence main {
    resize(60, 40)
    scatter(5) start
}
```

A width or height below 1 does nothing and warns.

See also: trim, pad, upscale

## upscale

Multiply the size by N x M, turning every cell into an N x M block of the same value.

- `n` - the horizontal factor.
- `m` - the vertical factor.

A common trick: generate a coarse layout on a small grid, then `upscale` it and refine the edges with rules at full resolution.

```ls
resize(30, 20)
grow(150) walk
upscale(2, 2)        // now 60 x 40, every cell doubled
settle smooth
```

A factor below 1 is treated as 1.

See also: resize

## trim

Crop every layer to the smallest rectangle that holds content, and move it to the top-left.

A cell counts as content if *any* layer has a value there. Useful after a random walk or growth, when you don't know in advance where the content ended up.

```ls
resize(60, 40)
once start
grow(100) walk
trim()
```

If every layer is empty, `trim()` does nothing and warns.

See also: resize, pad

## mirror

Copy one half of the grid onto the other, reflected, in every layer at once.

- `horizontal` - the **left** half is mirrored onto the right.
- `vertical` - the **top** half is mirrored onto the bottom.

The source half is never changed. For four-way symmetry, mirror twice. With an odd size the middle row or column stays as it is.

```ls
resize(41, 31)
grow(200) walk
mirror(horizontal)
mirror(vertical)     // quadrant symmetry
```

See also: symmetry, pad

## pad

Add an empty border N cells wide around every layer.

The size grows by 2N in each direction and the content moves to (N, N). Handy for framing a generated region, or to make room for a wall around it. To remove margins, use `resize` or `trim`.

```ls
trim()
pad(1)
everywhere wall_border
```

See also: trim, resize

## path

Carve the shortest (or cheapest) route from one set of cells to another, writing a value along it.

- `from` - where the route may start: a tag, or a boolean expression. Any matching cell works.
- `to` - where it may end. The route goes to the *nearest* reachable one.
- `into` - the grid to write the route into.
- `write` - the value written on every route cell, endpoints included.
- `passable` *(optional)* - which cells the route may step on. Default: any cell that is non-empty in some layer.
- `cost` *(optional, default 1)* - what it costs to step onto a cell. With a constant cost you get the shortest route; with an expression (`cost=elevation`) the cheapest one, so roads follow low ground.
- `connectivity` *(optional, default 4)* - `4` for side neighbours only, `8` to also allow diagonal steps.
- `over` *(optional)* - a graph layer: route from room to room instead of cell to cell, and stamp `write` over each room on the route.

All arguments are named. If no route exists, `path` does nothing and warns. Ties are broken by the seed, so the same seed always gives the same route.

```ls
path(from=start, to=exit, into=land, write=road,
     passable=(land != water), cost=(elevation + 1))
```

A bare tag such as `from=start` reads whichever layer declares `start`. If two layers could hold it, name the layer: `from=(land is start)`.

See also: voronoi, distance, graph

## bsp

Split the grid into rectangular rooms (binary space partitioning), writing each room's id into a number grid.

- `into` - a number grid. Every cell gets the id of the rectangle it lies in, from 0.
- `min` - the smallest allowed side of a rectangle.
- `max` *(optional)* - rectangles with a longer side are always split further (when they can be).
- `split` *(optional, default 50)* - the percentage chance that a rectangle that may split does split. Lower values leave bigger rooms.

All arguments are named. `bsp` only computes the rectangles; draw walls, carve rooms and place doors with rules. A rule that compares neighbouring ids finds the borders:

```ls
rule border(rotation=all) {
    zones[?a *]
    where[* (zones != a)]
    =>
    level[* wall]
}
sequence main {
    resize(48, 32)
    bsp(into=zones, min=6, max=14)
    everywhere border
}
```

Ids are numbered row by row from the top-left, the same order as `graph of zones`, so a graph over the result has node `i` = room `i`.

See also: voronoi, noise, graph

## voronoi

Divide the cells into zones, giving each cell to its nearest site, and write zone ids into a number grid.

- `sites` - the site cells, usually placed by a rule beforehand. A connected blob of site cells counts as one site.
- `into` - a number grid. Each claimed cell gets its zone id, from 0. Unclaimed cells are left empty.
- `within` *(optional)* - which cells can be claimed. Default: every cell. Zones only grow through these cells.
- `metric` *(optional, default `euclidean`)* - how distance is measured: `euclidean` (straight line, round zones), `manhattan` (steps through `within`, side neighbours), or `chebyshev` (steps, diagonals included).
- `cost` *(optional; `manhattan` and `chebyshev` only)* - what it costs to step onto a cell, as for `path`.

All arguments are named. Every zone is connected and holds its site.

```ls
rule place_site { level[floor] marks[.] => marks[site] }
sequence main {
    // ... carve floor ...
    scatter(12) place_site
    voronoi(sites=site, within=floor, into=zones)
}
```

See also: bsp, path, graph

## noise

Fill a number grid with smooth, organic-looking random values (Perlin noise), for terrain, moisture, density and the like.

- `into` - a number grid. Every cell is overwritten.
- `lo`, `hi` - the range of the output, both included.
- `scale` - the size of the features, in cells. Bigger is smoother, with broader hills.
- `octaves` *(optional, default 1)* - layers of detail. Each extra octave adds features half the size of the one before.
- `persistence` *(optional, default 50)* - how strong each octave is compared to the previous one, as a percentage. Higher is rougher.
- `normalize` *(optional, default `rank`)* - how raw noise maps onto `lo..hi`. `rank` spreads the values evenly, so every value covers about the same share of the cells. `range` keeps the shape of the field and stretches its lowest and highest points to `lo` and `hi`.

All arguments are named. Under `rank`, thresholds are predictable: with `lo=0, hi=99`, `elevation < 30` selects 30% of the cells for every seed and every scale.

```ls
rule flood { level[*] where[ (elevation < 30) ] => level[water] }
sequence main {
    resize(64, 64)
    noise(into=elevation, lo=0, hi=99, scale=16, octaves=3)
    everywhere flood
}
```

See also: where, bsp, random

## once

Apply a rule (or a sequence) one time, at one randomly chosen match.

Every match has the same chance. If nothing matches, nothing happens. Over a sequence, `once` runs its body one time.

```ls
once place_exit
```

See also: grow, scatter, everywhere

## scatter

Apply a rule at up to N random places at once, or at a percentage of the places it could.

- `scatter(N)` - at most N applications.
- `scatter(P%)` - that percentage of the applications `everywhere` would make.

All matches are found first, then applied in a random order. Matches whose writes would overlap one already applied are skipped. Every application sees the grid as it was *before* the statement, so a scattered rule never reacts to its own writes.

```ls
scatter(5) start
scatter(30%) carve
scatter(budget / 10) place_enemies
```

Rules only; use `once` or `settle` for sequences.

See also: everywhere, once, grow

## everywhere

Apply a rule at every place it matches, all at once.

Like `scatter` without a limit: every match is found against the same snapshot, then applied in random order, skipping any whose writes overlap one already applied. Use it for conversion passes, such as turning a layout into tiles, or for one step of a cellular automaton.

```ls
everywhere fill_geo
everywhere { algo[F|S] => level[floor] }   // inline rule
```

Rules only.

See also: settle, scatter

## grow

Apply a rule step by step. Each step picks one random match and applies it, and the next step sees the result.

- `grow(N)` - at most N steps.
- `grow` - keep going until nothing matches.

This is the mode for anything that builds on itself: random walks, growing caves, spreading rivers. Without a count, make sure the rule eventually stops matching (it fills empty cells, for example), or it runs forever. The compiler warns in the cases it can detect.

```ls
once start
grow(200) walk      // drunkard's walk: each step continues from the last
```

Rules only.

See also: once, settle, ordered

## settle

Repeat `everywhere` passes until a pass changes nothing.

- `settle` - until stable.
- `settle(N)` - at most N passes.

The cellular-automaton mode: smoothing caves, eroding walls, spreading until stable. Over a sequence, `settle` repeats the whole sequence until one round changes nothing.

```ls
settle smooth
settle(4) erode
```

See also: everywhere, grow, sequence

## if

Choose between two values: `a` when the condition holds, `b` otherwise.

Both values must have the same type (numbers, tags or booleans). Both branches are always evaluated, which is safe because no operation can fail: `if(n == 0, 0, 100 / n)` works at `n == 0`. Nest calls for more than two choices.

```ls
land[ (if(elevation < 3, water, if(elevation < 6, grass, hill))) ]
```

Because both branches run, `if(c, random(0, 9), random(0, 9))` draws twice. To draw once, put the `random` outside the `if`.

See also: min, max, clamp

## min

The smaller of two numbers.

Nest for more: `min(a, min(b, c))`.

See also: max, clamp

## max

The larger of two numbers.

Nest for more: `max(a, max(b, c))`.

See also: min, clamp

## abs

The absolute value of a number.

```ls
where[ (abs(x - width / 2) < 4) ]   // a vertical band in the middle
```

See also: mod

## clamp

Limit a number to the range `lo..hi`.

The same as `max(lo, min(v, hi))`.

```ls
params {
    rooms: number = 8
    safe_rooms = clamp(rooms, 2, 20)
}
```

See also: min, max

## mod

The remainder of `a` divided by `b`, always between 0 and `b` (for positive `b`).

Unlike `%` in many languages, the result is never negative for a positive `b`: `mod(-1, 8)` is `7`. `mod(a, 0)` is `0`. Good for repeating patterns:

```ls
where[ (mod(x + y, 2) == 0) ]   // a checkerboard
```

See also: abs

## random

A random whole number between `lo` and `hi`, both included.

Each call gives a new value, decided by the seed, so the same seed gives the same results. Usable anywhere an expression is: in params, counts, guards, `where` cells and written cells.

```ls
params { rooms = random(6, 12) }
...
scatter(random(3, 6)) place_enemies
rule speckle { level[floor] where[ (random(0, 99) < 10) ] => level[rock] }
```

When `hi < lo`, the result is `lo`.

See also: if, noise, any, weight

## distance

The number of graph steps from node `n` to the nearest node of a target.

- `n` - a node, bound by a graph pattern (`map[?n]`) or a graph block.
- `t` - another node, or a selector such as `rooms[Start]`, meaning every node with a cell that matches.

The result is `0` when `n` is itself in the target. When no node of the target can be reached it is a very large number (2^30), so "far away" comparisons also hold for unreachable nodes. Use `reachable` to test reachability.

```ls
// Open a wall between rooms that are not already directly connected.
rule open_if_unconnected(rotation=all) {
    level[floor wall floor]
    map[?a * ?b]
    when (distance(a, b) > 1)
    =>
    level[* door *]
}
```

See also: reachable, farthest, neighbors, graph

## neighbors

How many nodes of a target are directly connected to node `n`.

`t` is a node or a selector such as `rooms[Shop]`. A node is never its own neighbour.

```ls
when (neighbors(n, rooms[Hall]) >= 2)
```

See also: adjacent, distance, graph

## adjacent

Whether node `n` is directly connected to some node of a target.

The same as `neighbors(n, t) > 0`.

```ls
when (!adjacent(n, rooms[Start]))
```

See also: neighbors, reachable

## reachable

Whether some node of a target can be reached from node `n` at all.

The same as `distance(n, t) < 2^30`.

```ls
when (reachable(n, rooms[Exit]))
```

See also: distance, path

## some

Whether at least one cell of node `n`'s region matches a selector.

`s` is a one-cell pattern over any grid, such as `items[chest]`: the cell matches when that grid holds a matching value there.

```ls
when (!some(n, items[chest]))     // a room without a chest
```

See also: count

## count

Count matching cells: in one node's region, or in a whole grid.

- `count(n, s)` - the number of cells of node `n`'s region that match the selector `s`, such as `items[chest]`.
- `count(s)` - the number of cells in the whole grid that match `s`. Works anywhere a statement-level value is allowed, so it can size a count or gate a statement.

```ls
scatter(count(level[floor]) / 40) place_enemies
everywhere add_water  when (count(level[water]) < 50)
```

See also: some, nodes, edges

## nodes

The number of nodes (regions) in a graph layer.

```ls
everywhere join_rooms  when (nodes(map) > 1)
```

See also: edges, count, graph

## edges

The number of connections in a graph layer.

See also: nodes, graph

## farthest

Whether node `n` is (one of) the farthest from a target, counted in graph steps.

- `n` - the node being tested.
- `t` - where distance is measured from: a node or a selector.
- `u` *(optional)* - only nodes of this target compete. Default: every node.

Nodes that cannot reach `t` never count. Ties are not broken: every node at the greatest distance qualifies, and the mode picks among them.

```ls
// The exit goes in the room farthest from the start.
when (farthest(n, rooms[Start]))
```

See also: distance, reachable

## tag

Declare a tagset: the named values a grid can hold, plus named masks that group them.

Values are listed in braces, separated by commas or line breaks. A named mask (`name = ...`) combines values: `|` is "or", `!` is "not", `.` is empty. A tagset can hold up to 30 values.

```ls
tag geometry {
    wall, floor, door
    blocker = wall | door
    open    = !blocker        // floor, or empty
}
```

Two tagsets may reuse a name; each grid looks names up in its own tagset.

See also: layers, is, grid

## layers

Declare the grids a generator works on, and any graph views of them.

Each entry is `name: grid of tagset`, `name: grid of number`, or `name: graph of grid`. All grids share one size, set at run time with `resize`.

```ls
layers {
    level:  grid of geometry
    items:  grid of items
    height: grid of number
}
```

See also: grid, graph, tag, number

## grid

A layer of cells, declared in `layers` as `name: grid of tagset` or `name: grid of number`.

Every cell starts empty (`.`). A tag grid holds values of its tagset; a number grid holds integers (and `0` is not the same as empty).

See also: layers, number, graph

## number

The cell type of a numeric grid: `name: grid of number`.

Number grids hold whole numbers, such as elevation, distance or zone ids from `bsp`, `voronoi` and `noise`. Match them with expressions in `where` cells, since pattern cells only test exact values. Also the type of input params: `size: number = 32`.

```ls
layers { elevation: grid of number }
rule lowland { level[.] where[ (elevation < 30) ] => level[water] }
```

See also: grid, where, params

## graph

A read-only view of a grid as rooms and their connections: `name: graph of grid`.

Each connected patch of one value is a **node**. Nodes that touch are connected. With `(nodes=..., edges=...)`, only cells of the `nodes` values become nodes, and cells of the `edges` values (halls, doors) connect the nodes they touch. The graph updates itself whenever its grid changes.

```ls
layers {
    level: grid of algo
    zones: grid of number
    map:   graph of level(nodes=R, edges=H)   // rooms joined by halls
    areas: graph of zones                     // one node per zone
}
```

Read graphs with graph patterns (`map[?n]`), attributes (`n.size`, `n.degree`, `map.size` at a cell), and the graph functions. `path(over=map, ...)` routes from room to room.

See also: distance, neighbors, farthest, nodes, path

## of

Links a layer to its type: `grid of tagset`, `grid of number`, `graph of grid`.

See also: layers, grid, graph

## rule

Declare a rewrite: a pattern to find, `=>`, and what to write there.

The match side lists one pattern per layer; all must match at the same place. The write side writes into one or more layers. `*` matches anything and, on the write side, leaves the cell as it is. `.` is empty.

```ls
rule walk(rotation=all) {
    level[floor .]
    =>
    level[floor floor]
}
```

A rule does nothing by itself: apply it from a sequence with a mode (`once`, `scatter`, `everywhere`, `grow`, `settle`). Rules can also be written inline: `everywhere { level[.] => level[wall] }`.

See also: sequence, any, all, ordered, where, symmetry

## symmetry (attribute: symmetry=none|horizontal|vertical|all)

A rule attribute that also matches the pattern flipped: `symmetry=horizontal`, `vertical`, or `all`.

Each flip is a separate match, so you write the pattern once instead of once per direction. Combine with `rotation` for every orientation: `(symmetry=all, rotation=all)` gives all 8.

```ls
rule corner(symmetry=all) {
    level[wall wall
          wall .   ]
    =>
    level[* *
          * torch]
}
```

See also: rotation, rule, mirror

## rotation (attribute: rotation=none|90|180|270|all|{...})

A rule attribute that also matches the pattern rotated: `rotation=90`, `180`, `270`, `all`, or a set like `{90, 270}`.

`rotation=all` matches all four orientations, which is what most walking and carving rules want.

```ls
rule walk(rotation=all) { level[floor .] => level[floor floor] }
```

See also: symmetry, rule

## sequence

A named list of statements, run top to bottom. Tools run the sequence called `main`.

Sequences can call other sequences with `once` (one run) or `settle` (repeat until nothing changes), which makes them the way to build reusable steps.

```ls
sequence smooth {
    everywhere erode
    everywhere dilate
}
sequence main {
    resize(60, 40)
    noise(into=height, lo=0, hi=9, scale=8)
    settle(3) smooth
}
```

See also: rule, once, settle, when

## params

Declare the numbers that configure a generator: inputs with defaults, and values derived from them.

`name: number = default` is an input: it can be set from outside (the lab's sliders, `--param` on the command line, or the embedding API). `name = expression` is derived from earlier params and cannot be set from outside.

```ls
params {
    difficulty: number = 3
    budget = difficulty * 10
    rooms  = random(6, 12)
}
```

Params are fixed for the whole run. Use them in counts, guards, expressions and operation arguments.

See also: when, number, random

## where

A match-only pattern of boolean conditions, one per cell: `where[ (expr) ... ]`.

Lines up with the rule's other patterns like any layer. Use it to compare numbers, test the position, or check anything a plain cell cannot. In a condition, a layer name reads that layer at the same cell, and `x`, `y`, `width`, `height` give the position and size. `*` places no condition.

```ls
rule shore {
    level[.]
    where[ (elevation < 30) ]
    =>
    level[water]
}
rule edge { level[*] where[ (x == 0 || y == 0) ] => level[wall] }
```

See also: when, is, number, noise

## when

A guard: only run when a condition holds. Follows a statement, or the match side of a rule.

- After a **statement**, the condition is checked once, when the statement is reached, and decides whether it runs at all. It can read params, `width`, `height` and global counts.
- Inside a **rule**, after the match patterns, it is checked once per match and can also read the rule's `?variables`.

```ls
everywhere place_bosses  when (difficulty > 3)

rule wall_between(rotation=all) {
    regions[?a ?b]
    when (a != b)
    =>
    level[* wall]
}
```

For conditions on individual cells, use `where`.

See also: where, params

## use

Import another file, so its tags, layers, rules, sequences and params can be used here.

`use` lines go at the top of the file. A file only sees what it imports directly.

```ls
use "schema.lvs"
use "carve.lvs"
```

See also: sequence

## any

Pick one alternative at random. On the write side, or as the combinator of a rule body.

On the write side, `{ any ... }` writes one of its options, chosen at random each time the rule applies. Give options a `(weight=N)` to make them more or less likely. As a rule body, `any` holds several sub-rules, all of which can match.

```ls
rule reward {
    level[floor] items[.]
    =>
    { any
      (weight=3) items[.]
      items[chest]
      items[potion]
    }
}
```

See also: weight, all, ordered

## all

Do every item together. On the write side, or as the combinator of a rule body.

On the write side, `{ all ... }` writes all of its patterns at once, across layers. As a rule body, `all` lists independent sub-rules that apply side by side with equal priority.

```ls
rule fill_geo { all
    algo[W] => level[wall]
    algo[F] => level[floor]
}
```

See also: any, ordered

## ordered

A rule body whose sub-rules take priority in the order written.

Under `grow`/`once`, each step applies the first sub-rule that has a match, and returns to the first as soon as it matches again. Under `everywhere`/`scatter`, the first sub-rule claims its cells first and later ones fill what's left.

```ls
rule place { ordered
    level[floor floor floor] => level[* chest *]   // big gaps first
    level[floor] => level[torch]                  // then anything else
}
```

See also: any, all, grow

## weight

How likely an option of `{ any ... }` is, relative to the others: `(weight=N)`.

The default is 1. An option of weight 3 is picked three times as often as one of weight 1, and weight 0 is never picked.

```ls
{ any
  (weight=8) items[.]
  (weight=1) items[chest]
  (weight=1) items[heart]
}
```

See also: any, random

## is

Test a tag cell against a value or mask: `(level is wall|door)`.

True when the cell holds any of the named values, the same test a plain pattern cell performs. Use it in expressions: `where` cells, `if`, and operation arguments such as `passable=(level is floor)`.

```ls
where[ (level is floor) ]
path(from=(rooms is Start), to=(rooms is Exit), into=marks, write=route)
```

See also: where, tag
