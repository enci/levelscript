# LevelScript

**Status**: Draft 0.5
**File extension**: `.ls`
**CLI**: `lsc [--seed N] [--param name=value ...] <file.ls>`
**Embedding**: namespace `ls::` (see Appendix A)
**Scope**: Core language semantics for grid-based procedural generation.

LevelScript is a domain-specific language for procedural generation of grid-based content (roguelike dungeons, puzzle layouts, tile maps). A program describes how to transform a stack of correlated grids through a sequence of pattern-rewrite rules, orchestrated by a small set of built-in operations.

**Heritage.** LevelScript is the successor of MGSL (Multi Grid Scripting Language), a predecessor experiment whose reference implementation and spec (Draft v0.7) proved out the language design. LevelScript reimplements that language from scratch with the lessons applied - a single coroutine-backed execution core, a self-contained compiled artifact, an embedding-first API - plus a small number of deliberate semantic changes documented inline where they occur. LevelScript starts its version history clean; this document has no changelog. Draft 0.5 corresponds to roadmap steps 1-5 (the full language below); graph layers are the planned future.

---

## 1. Design principles

1. **Declarative**: the program describes *what to match and write*, not *how to iterate*.
2. **Visual**: pattern bodies look like the grids they match; an editor can render them with colored backgrounds.
3. **Minimal surface area**: a small set of orthogonal concepts (tags, grids, rules, sequences, programs, operations) covers the design space.
4. **Predictable execution**: snapshot-based rule application; one well-defined collect-then-pick model for all strategies.
5. **Cognitive load over terseness**: a rule should read clearly in isolation, even if that costs a few characters. Boilerplate is required only when ambiguity would otherwise exist.

---

## 2. Lexical structure

Formal grammar productions throughout this document use EBNF: `::=` defines a production; `|` separates alternatives; `?` is zero or one; `*` is zero or more; `+` is one or more; `( )` groups; quoted strings are literal terminals; `UPPER_CASE` names are lexical tokens; lowercase names are non-terminals. Whitespace and comments are skipped between tokens everywhere except inside pattern cell grids, where newlines act as row separators (section 5.3).

**Grammar** - top-level structure:
```
source_file ::= top_decl*
top_decl    ::= tag_decl | layers_decl | params_decl | rule_decl | sequence_decl | program_decl
```

### 2.1 Source files

LevelScript source files have the extension `.ls` and are UTF-8 encoded. Identifiers are ASCII (see section 2.3); non-ASCII bytes are only valid inside comments. Unicode identifiers are deferred to a future version.

### 2.2 Comments

```
// single-line comment
```

Block comments are not supported.

### 2.3 Identifiers

- **Start character**: an ASCII letter (`A`-`Z`, `a`-`z`) or `_`.
- **Continue character**: an ASCII letter, an ASCII digit (`0`-`9`), or `_`.

So `wall`, `Mauer`, `_internal` are valid; non-ASCII names lex as errors. This rule applies to tag names, tag value names, grid names, param names, rule names, and sequence names alike.

**Grammar:**
```
IDENT          ::= IDENT_START IDENT_CONTINUE*
IDENT_START    ::= [A-Za-z_]
IDENT_CONTINUE ::= [A-Za-z0-9_]
INTEGER        ::= [0-9]+
COMMENT        ::= '//' <any chars> NEWLINE
```

### 2.4 Reserved keywords

```
tag  layers  grid  of  number
rule  sequence  program  params  where  when
any  all  one  some  ordered
weight
policy  snapshot  incremental  stabilize  percent
```

Keywords are ASCII and match exactly.

```
RESERVED_CHARS ::= '[' | ']' | '{' | '}' | '(' | ')' | ',' | '='
                 | '*' | '.' | WS | NEWLINE
                 | <ASCII letters, digits, and '_'>
```
`?` is not reserved; it is held for future syntax and lexes as an error.

**Contextual names.** A few grammar terminals are matched by identifier text rather than reserved: `max` in a `some(...)` count (section 6), and the attribute names and values of section 5.6 (`symmetry`, `rotation`, `none`, `horizontal`, `vertical`). They lex as `IDENT`. `max` is still unavailable as a tag value, grid, or param name, because it is a built-in function name (section 5.10, section 7.3 check 21); the others may be used as names, since each slot resolves them against its own table.

**Operation names are not keywords.** The built-in operations `resize`, `upscale`, `trim`, `mirror`, `pad`, `path` (section 6.0) are resolved from the operation table, not reserved - like the built-in *functions* of section 5.10, a bare name that is not an operation call is just an identifier. They may be used as tag values, grid names, or params; they resolve as operations only in `op_call` position (section 6).

**Reserved expression identifiers.** Inside expressions (section 5.8) the bare identifiers `x`, `y`, `width`, `height` are reserved: `x`/`y` denote the current candidate position, `width`/`height` the current grid dimensions. A tag value, grid name, or param named `x`, `y`, `width`, or `height` is a compile error.

### 2.5 Pattern tokens

A pattern cell is an **expression** (section 5.8). Cells are whitespace-separated; line breaks are row breaks (section 5.3). Two surface forms exist:

- **Bare mask literal** - written without internal whitespace:

  | Form | Meaning |
  |------|---------|
  | `*` | match any value, incl. empty (mask = all bits); on the RHS, **preserve** (not in the write footprint, section 5.7) |
  | `.` | empty (mask `0x1`) - match empty, or on the RHS clear the cell |
  | *tag* | a tag value (its single bit) |
  | `!`*tag* | tag complement - **match-side only** |
  | *tag* `\|` *tag* ... | tag union (bitwise OR of masks) |
  | *integer* | a numeric value (for `grid of number`) |

- **Parenthesized expression** - `( ... )`, which may contain whitespace and uses the full expression grammar (section 5.8). Required for any computed form (comparison, arithmetic, logical, function call, multi-token reads). Example: `( tiles > 5 )`.

A bare `*` is always the wildcard; the multiplication operator `*` occurs only inside `( ... )`. A spaced operator (e.g. `wall | floor` with spaces) is **not** a bare cell - wrap it: `(wall | floor)`, or write it whitespace-free: `wall|floor`. `?` remains reserved.

A `mask_atom`'s `IDENT` (section 5.3) may be a `tag_value` or a `tag_union` (section 3); both resolve to masks and compose under `|` and `!` identically.

---

## 3. Tag declarations

A `tag` declares a tagset: a closed set of named values that a grid can hold.

```ls
tag items    { chest, heart, potion, sword, shield }
tag enemies  { goblin, troll, dragon }
tag geometry { wall, floor }
```

Tag values are referenced by their name in pattern bodies. Two tagsets may define values with the same name; resolution is by the grid's declared tagset.

**Grammar:**
```
tag_decl       ::= 'tag' IDENT '{' tag_item_list? '}'
tag_item_list  ::= tag_item (list_sep tag_item)* list_sep?
tag_item       ::= tag_value | tag_union
tag_value      ::= IDENT
tag_union      ::= IDENT '=' union_expr
union_expr     ::= union_atom ('|' union_atom)*
union_atom     ::= IDENT
list_sep       ::= ',' | NEWLINE
```
`list_sep` allows commas or newlines between list items (or both); a trailing separator is permitted.

A `tag_union` names a mask over the tagset's own members. Each `union_atom` is a `tag_value` of the same tagset, or a `tag_union` declared **earlier** in the same block (so unions may build on unions - `hazard = blocker | lava` - while remaining acyclic). A union references only members of its own tagset; there is no cross-tagset union.

```ls
tag geometry { wall, floor, door, blocker = wall | door }
```

**Encoding and cap.** A tagset holds at most **30** `tag_value`s (compile error if exceeded); these occupy bits 1..30 **in declaration order** - the first value declared is bit 1, the second bit 2, and so on. Bit 0 is reserved for **empty** (section 4.1), bit 31 is reserved. The bit assignment is normative: embedders may rely on it to compute a value's mask directly from its declaration position, without a name lookup. Each cell of a tag grid stores a 32-bit mask. A normal (single-valued) cell has exactly one value bit set; a cell may hold a multi-bit mask when written by a `|` expression or a named union (section 5.5).

A `tag_union` is a **named alias** for a mask over the tagset's value bits. It consumes **no** bit of its own and does **not** count toward the 30-value cap. A union name resolves, everywhere it may appear, to the bitwise OR of its members' masks - it is fully equivalent to writing that `|` expression inline. A union name shares the tagset's value namespace (redeclaring a value name is a duplicate error).

This encoding applies to **tag grids only**. `grid of number` cells store integers and are matched by value, not by mask.

---

## 4. Grids and the `layers` block

All grids used by a program are declared in a single `layers` block:

```ls
layers {
    level:   grid of geometry
    tiles:   grid of number
    enemies: grid of enemies
    items:   grid of items
}
```

Each entry is `name: grid of <type>`, where `<type>` is either:
- A tagset name (`grid of geometry`)
- The keyword `number` (for numeric/scalar grids)

Grids have no declared size; size is set at program runtime via `resize`. All grids share the current size at all times.

**Grammar:**
```
layers_decl      ::= 'layers' '{' layer_entry_list? '}'
layer_entry_list ::= layer_entry (list_sep layer_entry)* list_sep?
layer_entry      ::= IDENT ':' 'grid' 'of' grid_type
grid_type        ::= IDENT | 'number'
```
`IDENT` after `of` must reference a declared tag (semantic check).

### 4.1 Empty cells

Every cell of every grid starts empty. For tag grids, **empty is bit 0** of the cell's mask (value `0x1`) - a real bit, not the absence of bits. A non-empty tag cell has bit 0 clear and at least one value bit set. Empty is matched and written via `.`, which is the mask `0x1`.

```ls
algo[.]       // match an empty cell
algo[.]       // (on the RHS) clear the cell
```

Numeric grids represent empty separately from `0`; `0` is a distinct stored value (section 10.3).

Matching is uniform across all mask forms: a pattern cell carries a mask, and it matches the stored cell when `(stored & pattern_mask) != 0`. Thus `.` (mask `0x1`) matches only empty; a tag `t` (its single bit) matches a cell containing `t`; `*` (all bits, section 2.5) matches anything including empty.

Empty is also nameable in expressions as the literal `.` (section 5.8): `(g == .)` tests empty, `(g != .)` tests non-empty, and a `.` result on the RHS clears the cell.

### 4.2 Parameters

A `params` block declares runtime numeric inputs:

```ls
params {
    difficulty: number = 3
    budget = difficulty * 10
}
```

**Grammar:**
```
params_decl      ::= 'params' '{' param_entry_list? '}'
param_entry_list ::= param_entry (list_sep param_entry)* list_sep?
param_entry      ::= input_param | derived_param
input_param      ::= IDENT ':' 'number' '=' expr
derived_param    ::= IDENT '=' expr
```

Two kinds of param, distinguished solely by the `: number` annotation:

- An **input param** (`name: number = default`) may be supplied at runtime - via the embedding API's `generate(seed, params)` / `begin(...)` or the CLI's `--param name=value` - alongside the seed. The `= expr` **default is required**: it is evaluated once at startup only when the runtime does not supply the param; a supplied value overrides it and the default expression is not evaluated. Because every input is defaulted, a program always has a complete configuration from the seed alone - **there is no "missing required param" runtime failure**.
- A **derived param** (`name = expr`) is computed once, at startup, when the input params bind. It is never settable from outside.

Both kinds carry an expression under the same scope discipline: literals, input params, and **earlier-declared** params only - no grid reads, no `x`/`y`, and no `width`/`height` (dimensions change during a run, so a startup-frozen dimension would be stale; read `width`/`height` directly in a cell expression instead). References resolve in declaration order; a forward reference or any cycle is a compile error. The expression may call `random` (section 5.10), drawing once at that single evaluation. Because both kinds are fixed once inputs bind, params are run-constant and determinism (section 7.2) holds.

At most one `params` block per file. A param name (input or derived) must not collide with a grid name, a reserved identifier (section 2.4), or a built-in name (section 5.10). Param values and expressions are numbers; a derived param whose expression is not a number is a compile error.

---

## 5. Rules

A rule is a named match-write transformation. Its body declares one or more *match patterns* (left of `=>`) and one or more *write patterns* (right of `=>`).

### 5.1 Basic form

```ls
rule start {
    algo[.]
    =>
    algo[S]
}
```

The simplest rule body has a single match and a single write. When a position in the grid matches the LHS, the matched cells are replaced according to the RHS.

**Grammar** - rule structure:
```
rule_decl         ::= 'rule' IDENT rule_attrs? '{' rule_body '}'
rule_attrs        ::= '(' attr_list ')'
attr_list         ::= attr (',' attr)*
attr              ::= IDENT '=' attr_value
attr_value        ::= IDENT | INTEGER | 'all' | '{' INTEGER (',' INTEGER)* '}'
rule_body         ::= pattern_pair
                    | combinator pattern_pair_list
pattern_pair_list ::= pattern_pair (list_sep pattern_pair)* list_sep?
pattern_pair      ::= match_side '=>' write_side
```

### 5.2 Combinators (`any` / `all` / `ordered`)

**Combinator blocks are required when grouping more than one item at the same level; a single-item block is also legal.** A rule containing a single match-write pair, with a single pattern on each side, needs no combinator anywhere - but `{ all p }`, `{ any p }`, and a one-sub-rule combinator body all parse and run, with the obvious meaning. An **empty** combinator block (zero items) is a compile error.

> Difference from MGSL: MGSL rejected single-item combinator blocks ("a context with exactly one item must omit the combinator"). LevelScript relaxes this - single-item blocks run with zero special-casing, which matters for generated or heavily-edited sources where an alternative list shrinks to one entry. Semantics: `{ all p }` is identical to bare `p`; a single-item `{ any p }` always picks its one item but, having no special case, **still consumes its one PRNG draw** (section 6.7) like any other `{ any }` node.

A combinator block is needed for:

- A LHS that conjoins multiple patterns across grids (use `{ all ... }`).
- A RHS that picks among alternatives (use `{ any ... }`).
- A rule body containing multiple independent match-write sub-rules (use `any`, `all`, or `ordered` at the rule body level).

Concretely (a one-match, one-write rule like `start` in section 5.1 needs none):

```ls
// Multiple write alternatives - { any } on the RHS.
rule reward {
    algo[S]
    =>
    { any
      items[chest]
      items[heart]
      items[potion]
    }
}

// Multiple independent sub-rules - combinator at the rule body level.
rule fill_geo { all
    algo[W] => level[wall]
    algo[F] => level[floor]
    algo[S] => level[floor]
}
```

There is no default combinator: in any context that contains more than one item, `any`, `all` (or, at body level, `ordered`) must be stated explicitly.

**Grammar** - match and write sides:
```
match_side      ::= pattern
                  | '{' 'all' pattern_list '}'
write_side      ::= write_term
write_term      ::= pattern
                  | '{' 'all' all_item_list '}'
                  | '{' 'any' any_item_list '}'
all_item_list   ::= write_term (list_sep write_term)* list_sep?
any_item_list   ::= any_item (list_sep any_item)* list_sep?
any_item        ::= weight? write_term
pattern_list    ::= pattern (list_sep pattern)* list_sep?
combinator      ::= 'any' | 'all' | 'ordered'
weight          ::= '(' 'weight' '=' INTEGER ')'
```
All item lists are one-or-more; both sides may be a bare pattern or a braced combinator block. The combinators are not interchangeable across sides:
- A **match side** conjoins patterns across grids, so only `{ all ... }` is meaningful; `{ any ... }` on the match side is not supported (disjunctive matching would change the candidate model, read footprints, and conflict semantics; it is out of scope).
- A **write side** is a recursive `write_term`: a `{ any ... }` (pick one alternative) or `{ all ... }` (write every item simultaneously), whose items may themselves be blocks to any depth.

`weight` attaches only to the items of an `{ any ... }` write block. It is not part of the grammar for `{ all ... }` blocks or bare patterns, so `(weight=N)` inside `{ all ... }` is rejected at parse time. A weight on a nested block (an `{ all }` option inside an `{ any }`) weights the choice of that whole sub-tree.

**Nested write blocks.** Because a write item may itself be a block, two composite shapes are available at one matched position:

```ls
// all-of-any - floor always; an item chosen independently (mostly nothing)
=> { all
     level[floor]
     { any
       (weight=8) items[.]
       (weight=1) items[chest]
       (weight=1) items[heart]
     }
   }

// any-of-all - one correlated combination of both layers
=> { any
     { all level[floor] items[chest] }
     { all level[wall]  items[.] }
   }
```

An `{ all }` writes every item; an `{ any }` picks exactly one item (weighted), then resolves it. Evaluation walks the write tree once per application: each `{ any }` node on the resolved path contributes **one draw**, in **declaration order** (outer before inner, earlier sibling first), deterministic per seed (section 10.6). Nesting composes the writer only; it changes neither matching nor the write-protection mask (section 6.8), which act on the final resolved set of cell writes.

**The `ordered` body-level combinator.** `ordered` is a **body-level** combinator only (not a match- or write-side form). In `{ ordered s1 s2 ... }`, the sub-rules carry a **priority** in declaration order (s1 highest). `ordered` is purely an *ordering key on the candidate vector*: candidates are grouped by sub-rule priority (all of s1's, then s2's, ...) and shuffled only *within* each group. What that ordering *does* is inherited from the execution policy (section 6.7) that runs the statement - `ordered` is orthogonal to the policy, affecting only order:
- Under **`snapshot`**: pull in priority order under the write mask - s1 claims its cells first, s2 fills what's left, s3 last. Spatial priority / conflict resolution in one batch ("place big rooms; where you can't, small; else floor").
- Under **`incremental`**: re-collect each step and take the highest-priority available match - s1 keeps firing while it can; when a lower rule fires and reopens an s1 match, the next step prefers s1 again. This is **preemptive priority** (the ordered-rule / MarkovJunior loop), the behaviour statement-sequencing cannot express.
- Under **`stabilize`**: priority-ordered sweeps to fixpoint.

`any` stays a weighted random pick (order carries no meaning); `all` collects every matching sub-rule's candidates together (uniform priority). Because `ordered` is body-level only, using it as a match-side or write-side combinator is a compile error (section 7.3, check 29).

### 5.3 Pattern bodies

A pattern body is a rectangular grid of cells enclosed in `[ ]`. Whitespace between cells is required; line breaks within `[ ... ]` denote row breaks.

```ls
algo[
    * * *
    * S W
    * * * ]
```

The above is a 3x3 pattern. The center cell must be `S`, the right-center must be `W`, and the eight cells marked `*` match anything (including empty).

Single-cell patterns are written inline: `algo[S]`.

Multi-row pattern grammar:
- Empty rows are illegal.
- Trailing whitespace on a row is ignored.
- The closing `]` may be on its own line or at the end of the last row.
- Inconsistent column counts across rows are a compile error.

**Grammar:**
```
pattern   ::= IDENT '[' cell_grid ']'
cell_grid ::= cell_row (NEWLINE cell_row)*
cell_row  ::= WS* cell (WS+ cell)* WS*
cell      ::= '*' | '.' | INTEGER | tag_mask | '(' expr ')'
tag_mask  ::= mask_atom ('|' mask_atom)*
mask_atom ::= '!'? IDENT
```
A `tag_mask` is whitespace-free (the cell ends at the next whitespace). `expr` inside `( ... )` follows section 5.8 and may contain whitespace. `tag_mask` and the bare-`*`/`.` forms are exactly the mask literals; every other expression must be parenthesized.

The opening `[` may be followed immediately by content on the same line or by a newline starting the first row. The closing `]` may follow the last row or sit on its own line.

**Expression cells in real grids.** A parenthesized expression in a real grid cell must evaluate to that grid's type (a tag-mask for a tag grid; a number for a number grid); a type mismatch is a compile error.
- **On the LHS**, the cell matches by mask test `(stored & value_mask) != 0` (tag grids) or by equality (number grids). Matching is strict - an expression cell does not implicitly mean `*`.
- **On the RHS**, the evaluated value is written. A multi-bit tag result (e.g. `wall|floor`) is written as-is; single-bit results are not enforced. `*` on the RHS still means preserve, distinct from writing all bits.

Same-position self-reads see the snapshot value (section 7.1), so a counter reads naturally:
```ls
tiles[ (tiles < 9) ] => tiles[ (tiles + 1) ]
```

Conditional writes select among tags with `if` (section 5.10): `level[ (if(difficulty > 3, wall, floor)) ]`. A parenthesized RHS expression may evaluate to empty (via `.`), clearing the cell - e.g. `g[ (if(cond, floor, .)) ]` writes `floor` or clears.

### 5.4 Match-write shape constraint

For each match-write pair, the match pattern and every write pattern must have identical dimensions. This applies **recursively** through nested write blocks: every leaf `pattern` anywhere in the write tree - and hence every `{ any }` alternative and every `{ all }` item at any depth - must have the LHS pattern's dimensions. The compiler rejects mismatched shapes at any level.

### 5.5 Weights and union writes

Within an `{ any }` block, options may carry a weight:

```ls
{ any
  (weight=2) algo[F S]
  (weight=1) algo[S F]
}
```

Weight is per-option, prefixed before the option. Omitted weight defaults to `1`. Weights bias random selection proportionally.

**Union writes.** A RHS tag cell may be a union mask, e.g. `level[wall|floor]`, written via `|` (section 5.8). The cell then stores a multi-bit mask. Single-bit writes are **not** required. The meaning of a multi-valued cell under later matching is the any-overlap rule `(stored & mask) != 0` (section 4.1).

A **named union** (section 3) behaves identically to the equivalent inline `|` mask: on the LHS it matches by the any-overlap rule, on the RHS it writes the combined bits, and `!blocker` (match-side only) is the complement of the union mask.

### 5.6 Rule attributes

Rules may declare attributes in parentheses after the rule name:

```ls
rule rwalk(rotation=all) { ... }
rule reduce(symmetry=all, rotation=all) { ... }
```

#### 5.6.1 `symmetry`
Generates additional pattern variants by flipping. Identity is always included.

- `symmetry=none` - only the pattern as written.
- `symmetry=horizontal` - adds the horizontal flip.
- `symmetry=vertical` - adds the vertical flip.
- `symmetry=all` - adds the horizontal, vertical, and both-axis flips (identity + H + V + both-axis = 4 variants).

All four are dimension-preserving (W x H unchanged). The both-axis flip (H and V applied together) is geometrically a 180-degree rotation, so it coincides with `rotation=180`. This overlap is handled by the compiler, not the author: when `symmetry` and `rotation` are combined, coincident variants are de-duplicated (section 5.6.2, section 10.2). The author writes `all` on each axis and gets the expected set with no duplicate work at runtime.

#### 5.6.2 `rotation`
Generates additional pattern variants by rotation. Identity is always included.

- `rotation=none` - only the pattern as written.
- `rotation=90`  - adds the 90-degree rotation (identity + 90 = 2 variants).
- `rotation=180` - adds the 180-degree rotation.
- `rotation=270` - adds the 270-degree rotation.
- `rotation=all` - adds the 90-, 180-, and 270-degree rotations.
- `rotation={a, b, ...}` - adds each listed angle (a **set** of angles). The set is an additive union with identity always included and duplicates removed, e.g. `rotation={90, 270}` = {identity, 90, 270}. `all` is shorthand for `{90, 180, 270}`. A set with one element (`{90}`) is equivalent to the bare value (`90`).

The set delimiter is `{ }` - the existing set brackets (section 3 tag value sets) - not `[ ]`, which is reserved for pattern bodies. An angle set is confined to this attribute slot: it is not a value and introduces no array type (section 5.8 is unchanged).

The angle values `90`, `180`, `270` are integer literals validated in this attribute slot; they are not keywords, and they do not interact with numeric grids or expressions. An angle other than 90/180/270 is a compile error.

`symmetry` and `rotation` are independent; the variant set is the **de-duplicated union** of the two attributes' variants composed together (identity always included). Two variants are duplicates iff both their transformed match side and their transformed write tree are structurally equal (section 10.2). A variant whose match side coincides with another's but whose writes differ is kept: for example, the H-flip of `g[a a] => g[b c]` survives as `g[a a] => g[c b]`, and the two compete as separate candidates (section 6.7). Because `symmetry=all` includes the both-axis flip and `rotation=all` includes 180, those coincide and collapse to a single variant. The full dihedral group D4 is `symmetry=all, rotation=all` - 8 distinct variants after de-duplication.

**Value constraints** (semantic, applied to the generic `attr` form of section 5.1):
```
attr       ::= 'symmetry' '=' sym_value
             | 'rotation' '=' rot_value
sym_value  ::= 'none' | 'horizontal' | 'vertical' | 'all'
rot_value  ::= 'none' | 'all' | angle | angle_set
angle_set  ::= '{' angle (',' angle)* '}'
angle      ::= '90' | '180' | '270'
```
These are checks, not productions: the parser accepts any `attr` of section 5.1, and the compiler validates it against the table above (section 7.3, checks 7 and 18). The quoted names (`symmetry`, `rotation`, `none`, `horizontal`, `vertical`) match `IDENT` text; they are contextual names, not keywords (section 2.4). `all` is the keyword. `90`/`180`/`270` are the `INTEGER` token, accepted only here and only with those three values. A single-element set `{90}` is legal and equals the bare `90`. `symmetry` stays single-valued.

### 5.7 Match and write footprints

Each pattern carries two compile-time footprints, derived from its cells:

- **Read footprint**: the set of cell offsets the matcher actually reads. Every cell other than `*` contributes to the read footprint: a tag value, a complement, an inline or named union, an integer literal, or `.` (empty). Expression cells are covered below. Cells marked `*` (any) are *not* in the read footprint, since the matcher never inspects them.
- **Write footprint**: the set of cell offsets the rewriter actually writes. Every cell other than `*` contributes to the write footprint: a tag value, an inline or named union, an integer literal, or `.` (which clears the cell). Expression cells are covered below. Cells marked `*` are *not* in the write footprint, since they preserve whatever was matched.

Footprints are defined relative to the pattern's origin (top-left corner). When the rule fires at grid position (x, y), each footprint is translated by (x, y) to identify which grid cells are read or written.

The bounding rectangle of a pattern is its W x H region, but the footprints may be sparse subsets (a 3x3 pattern with `*` corners has plus-shaped footprints). The write footprint drives the write-protection mask (section 6.8). The read footprint does not participate in conflict handling; it defines which cells a match inspects, and which cells the observe channel reports as matched (Appendix A).

**Expression cells.** An expression cell's read footprint is the set of grid cells its expression reads at the current position. Same-position cross-grid reads (a bare grid name, section 5.8) contribute the referenced grid's cell at this offset. Reads of `x`, `y`, `width`, `height`, and params read no grid cell and contribute nothing. `where` cells contribute to the read footprint only (never the write footprint). On the RHS, an expression cell's write footprint is its own grid cell.

Because there is no coordinate indexing (section 5.8), every read offset is statically known, so footprints are compile-time static.

### 5.8 Expressions

Expressions are **deterministic for a given seed**. Every built-in is pure except `random` (section 5.10), which advances the shared PRNG (section 10.6); `random` is the one expression form whose repeated evaluation may yield different values within a run. All other functions and operators are pure and side-effect-free. Expressions appear in pattern cells (sections 2.5/5.3), in `where` cells (section 5.9), and in param-dependent positions.

**Grammar.**
```
expr        ::= or_expr
or_expr     ::= and_expr ('||' and_expr)*
and_expr    ::= bor_expr ('&&' bor_expr)*
bor_expr    ::= eq_expr  ('|'  eq_expr)*        (* tag-union; operands must be tag-typed *)
eq_expr     ::= rel_expr (('==' | '!=') rel_expr)*
rel_expr    ::= add_expr (('<' | '<=' | '>' | '>=') add_expr)*
add_expr    ::= mul_expr (('+' | '-') mul_expr)*
mul_expr    ::= unary_expr (('*' | '/') unary_expr)*
unary_expr  ::= ('!' | '-') unary_expr | primary
primary     ::= INTEGER
              | '.'                         (* empty literal - section 4.1; polymorphic *)
              | IDENT                       (* grid read @ current position | param | x/y/width/height *)
              | IDENT '(' arg_list? ')'     (* built-in function call - section 5.10 *)
              | '(' expr ')'
arg_list    ::= expr (',' expr)*
```
Precedence follows C conventions (lowest to highest): `||`, `&&`, `|`, equality, relational, additive, multiplicative, unary, primary.

**Identifier resolution** (in order): a name followed by `(` is a built-in function call (section 5.10); else `x`/`y`/`width`/`height` are the reserved position/dimension values (section 2.4); else a declared param - input or derived (section 4.2); else, **inside a tag-grid cell**, a `tag_value` or `tag_union` of that grid's tagset (a union resolves to its members' combined mask, section 3); else a grid name, which reads **that grid's value at the current cell position** (same-position cross-grid read - no indices). A name resolving to none of these is a compile error.

**Types.** Three value kinds: **tag-mask**, **number**, **bool**.
- Tag operators: `|` (union), `!` (complement, match-side only), `==`/`!=` between same-tagset operands -> bool.
- Number operators: `+ - * /` -> number; `== != < <= > >=` -> bool. `number` is a signed 32-bit integer. `/` is integer division truncating toward zero; **`n / 0` is defined as `0`**. Arithmetic overflow wraps (two's complement). Every expression is therefore **total**: it always evaluates to a value, never a fault.
- Bool operators: `&& || !` -> bool.
- A grid read yields tag-mask (tag grid) or number (number grid). `x`/`y`/`width`/`height` and params yield number.
- Mixing kinds (e.g. `|` on numbers, `+` on tags, comparing tag to number) is a compile error.

**The empty literal `.`** denotes the empty value (section 4.1). It is **polymorphic**: in a tag context it is the empty mask `0x1`; in a number context it is the empty sentinel. Its type is inferred from context - the other operand of a comparison, the sibling branch of an `if`, or the grid of the cell it is written into. A `.` with no inferable type (e.g. `(. == .)`) is an expression type error (section 7.3, check 14).

- **Tag context:** `(g == .)` is true iff the cell is empty; `(g != .)` iff non-empty. `.` composes in `if` (`if(c, floor, .)` -> tag), and on the RHS writes empty (a conditional clear).
- **Number context:** reading an **empty** number cell yields **0** in arithmetic and comparison (expressions stay total). Therefore `(tiles == 0)` is true for an empty cell; `(tiles == .)` is the **only** test that distinguishes empty from a stored `0`. `if(c, 5, .)` writes `5` or clears; on the RHS `.` writes the empty sentinel.

`*` is **not** an expression value (inside `( )` it is multiplication); only `.` crosses from mask literal into the expression grammar.

**Same-position reads only.** An expression reads other grids exclusively at the current cell's position. There is no coordinate indexing; neighbour structure is expressed by the surrounding pattern grid, not by expressions. This keeps read footprints static (section 5.7).

**`random` and footprints.** A `random` call reads no grid cell, so it contributes nothing to the read footprint; footprints stay compile-time static. `random`'s only effect is advancing the PRNG.

### 5.9 The `where` pseudo-layer

`where` is a reserved pseudo-grid usable as a pattern head on the **LHS only**. It is not declared in `layers`. Each `where` cell holds a **boolean** expression (section 5.8) and is therefore always parenthesized:

```ls
where[ (y < height/2)  (tiles > 5) ]
```

A `where` cell matches at a position iff its expression evaluates to true there. `where` cells read but never write - they contribute to the read footprint (section 5.7) and never to the write footprint. A `where` pattern combines with real-grid patterns at the same position under `{ all }`:

```ls
rule deep_water {
    { all
      level[floor]
      where[ (tiles > 5) ]
    }
    =>
    level[water]
}
```

A `where` pattern on the RHS is a compile error. Numeric range/comparison matching is expressed through `where`; there is no numeric-cell comparison syntax.

### 5.10 Built-in functions

LevelScript has a closed set of **built-in** functions; there are no user-defined functions. A function call is recognised by the section 5.8 rule (an identifier followed by `(`), then resolved against the table below. A call to a name not in the table is a compile error (section 7.3). Built-ins are resolved by name against a schema table, exactly like operations (section 6.0) one layer up - adding one is a table row plus an evaluator, with no grammar change.

| Call | Signature | Result |
|------|-----------|--------|
| `if(c, a, b)` | `if(bool, T, T) -> T`, `T` in {number, tag, bool} | `a` if `c` else `b`. **Eager** - both `a` and `b` are evaluated. `a` and `b` must have the same type; that type is the result type. |
| `min(a, b)` | `(number, number) -> number` | smaller of `a`, `b` |
| `max(a, b)` | `(number, number) -> number` | larger of `a`, `b` |
| `abs(a)` | `(number) -> number` | absolute value (wraps at INT_MIN) |
| `clamp(v, lo, hi)` | `(number, number, number) -> number` | `max(lo, min(v, hi))` |
| `random(lo, hi)` | `(number, number) -> number` | a value in `[lo, hi]` inclusive (`lo` when `hi < lo`); **advances the PRNG** (section 10.6). One draw per call. The one non-pure built-in. |

Arities are fixed. `min`/`max` are binary; nest for more operands (`min(a, min(b, c))`). `if` is the only function whose type is parametric in `T`; it is a single generic signature, not overloading. Because evaluation is eager and all operators are total (section 5.8), `if(c, a, b)` never faults on its unused branch - e.g. `if(n == 0, 0, 100 / n)` is safe at `n == 0`.

**Reserved built-in names.** `if`, `min`, `max`, `abs`, `clamp`, `random` may not be used as tag value, grid, or param names (compile error). They are **not** keywords (they are not in section 2.4): a bare occurrence not followed by `(` does not resolve to anything and is a compile error.

**`random` is impure; the others are pure.** Because `if` is eager, `if(c, random(0,9), random(0,9))` evaluates **both** branches and therefore draws **twice**, in left-to-right order, discarding the unused value. This is defined behaviour, not a fault; if exactly one draw is wanted, place the `random` outside the `if`. (Eager `if` was a reopened decision for LevelScript and was deliberately kept: totality makes the dead branch safe, and the fixed two-draw cost keeps the draw sequence trivially pinned.) `random` may appear in any expression position (match cells, `where`, write cells, `when` guards, param expressions): it reads no grid, position, or dimension, so the config-scope restrictions of sections 4.2/6 do not exclude it. It draws once each time its expression is evaluated.

---

## 6. Programs

A `program` block declares the orchestration:

```ls
program {
    resize(60, 40)
    some(max=5)   start
    some(max=100, policy=incremental) rwalk
    upscale(2, 2)
    all reduce
    all reward
    all fill_geo
    some(max=10) place_enemies
    all decorate
}
```

Statements execute top to bottom. There is exactly one program per file (for now).

**Grammar:**
```
program_decl    ::= 'program' '{' statement_list '}'
statement_list  ::= statement*
statement       ::= (op_call | apply_stmt) guard?
guard           ::= 'when' '(' expr ')'
op_call         ::= IDENT '(' op_args? ')'
op_args         ::= positional_args
                  | named_args
                  | positional_args ',' named_args
positional_args ::= arg_value (',' arg_value)*
named_args      ::= named_arg (',' named_arg)*
named_arg       ::= IDENT '=' arg_value
arg_value       ::= INTEGER | IDENT | '(' expr ')'
apply_stmt      ::= strategy IDENT
strategy        ::= 'one'  strat_opts?
                  | 'all'  strat_opts?
                  | 'some' '(' count_arg (',' policy_arg)? ')'
strat_opts      ::= '(' policy_arg ')'
count_arg       ::= 'max' '=' INTEGER | 'percent' '=' INTEGER
policy_arg      ::= 'policy' '=' policy_name
policy_name     ::= 'snapshot' | 'incremental' | 'stabilize'
```
A program statement is either an **operation call** (`op_call` - a built-in operation applied to the grid stack, section 6.0) or an **application** (`apply_stmt` - a strategy plus a rule or sequence name). In an `op_call`, arguments are **positional first, then named**; the operation name and its arguments resolve **semantically** against the operation table (section 6.0), not by the grammar - there are no per-verb productions and no verb keywords. Newlines are permitted inside an argument list. `IDENT` after a strategy must reference a declared rule or sequence (section 6.10; semantic check; forward references are fine). Every strategy over a rule may carry `policy=`; omitted, it defaults to `snapshot`. Over a sequence, `policy=` and `percent` are errors (section 6.10). `one`/`all` take only a policy; `some` takes a count (`max` **or** `percent`, never both) and an optional policy. (`max` here is a contextual name matched by `IDENT` text, not a keyword; section 2.4.) A guard (`when`) may follow any statement.

Reads:
```ls
all fill                               // snapshot (default), full batch
all(policy=stabilize) smooth           // CA: re-sweep to fixpoint
some(max=200, policy=incremental) walk // drunkard's walk
some(percent=50) carve                 // half of one snapshot batch
path(from=door, to=exit, into=site, write=road)  // structural op: carve a route
```

A `guard` may follow any program statement. Its expression (section 5.8) is **boolean** and reads **params only** (input or derived); a grid read, `x`/`y`, or `width`/`height` in a `when` guard is a compile error (there is no candidate position or committed size at statement scope). The guard is evaluated **once**, when the statement is reached. If it is false, the statement is skipped in full - no operation, no rule application, no collection pass. If true, the statement runs exactly as it would unguarded; the guard gates *whether* the statement runs, never *which cells* within it (that is `where`, section 5.9). A guard on an operation call gates that operation the same way.

```ls
program {
    resize(60, 40)
    some(max=5) start
    all place_bosses  when (difficulty > 3)
    all cave_pass     when (style == 0)
    all room_pass     when (style == 1)
}
```
Selection among rules is expressed by complementary guards, as with `style` above. There is no `if`/`else` program block; statements stay a flat, top-to-bottom list. A sequence (section 6.10) does not change this: it is a named statement list applied by name, like a rule, not a nested block.

### 6.0 Built-in operations

LevelScript has a **closed set of built-in operations** that transform the grid stack; there are no user-defined operations. An `op_call` (section 6 grammar) is resolved by name against the table below, then its arguments are validated against that operation's **parameter schema**. This mirrors expression built-ins (section 5.10): operations are **not keywords**, and a bare name not in the table does not resolve (compile error, section 7.3).

Each parameter has a **kind**, a **binding** (positional or named-only), and required/optional status with a default. Argument kinds:

| kind | written as | resolves to |
|------|-----------|-------------|
| `int` | INTEGER | an integer literal |
| `grid` | IDENT | a declared grid name (section 4) |
| `pred` | a tag value, or `( bool-expr )` | a per-cell predicate (a tag means "cell holds this tag"; an expression means its truth per cell) |
| `value` | a tag value, INTEGER, or `( expr )` | a cell value to write (typed by the target grid) |
| `expr` | INTEGER or `( expr )` | a numeric expression evaluated per cell |
| `enum{...}` | IDENT or INTEGER | one of a fixed set (validated per operation) |

**Operation table.**

| operation | parameters | effect |
|-----------|-----------|--------|
| `resize` | `w:int`, `h:int` (positional) | set size to W x H, content-preserving, top-left anchored (section 6.1) |
| `upscale` | `n:int`, `m:int` (positional) | multiply size by N x M, duplicating cells (section 6.2) |
| `trim` | *(none)* | crop all layers to the content bounding box (section 6.3) |
| `mirror` | `axis:enum{horizontal,vertical}` (positional) | fold one half onto the other, all layers (section 6.4) |
| `pad` | `n:int` (positional) | uniform empty border of N on all sides (section 6.5) |
| `path` | `from:pred`, `to:pred`, `into:grid`, `write:value`, `over:grid?`, `passable:pred?`, `connectivity:enum{4,8}?=4`, `cost:expr?=1` (**all named**) | write a shortest route between predicates into a grid (section 6.6) |

**Binding: positional vs. named-only.** Each parameter is declared **positional** or **named-only** by convention: positional when its meaning is obvious from the operation and its order (a single argument, or an unmistakable order like width-then-height); named-only when the operation has several same-kind or optional arguments with no self-evident order. So `resize(10, 20)`, `pad(2)`, `mirror(horizontal)` read clean without names, while `path(from=..., to=..., ...)` is named throughout. A positional parameter may also be supplied by name; a named-only parameter supplied positionally is an error.

**Argument resolution.** Positional arguments come first and fill positional parameters left to right; named arguments follow and fill by name. A parameter may be given at most once; omitted optional parameters take their default. The validator checks, per call: the operation exists; positional args do not exceed the positional parameters; no named-only parameter given positionally; every named argument names a real parameter; no parameter supplied twice; every required parameter present; each argument's kind matches; each `enum` value is in range (section 7.3, checks 32-36). This one check-set covers every operation, present and future - a new operation is a table row plus an executor, with no grammar or keyword change (see section 9 for anticipated entries).

### 6.1 `resize(W, H)`

Sets the active grid size to W columns by H rows, across all layers. The grid **origin is the top-left cell (0, 0)**; all geometric operations (sections 6.1-6.6) anchor there and content grows to the right and down. Only `pad` (section 6.5) grows from a different side.

`resize` is **content-preserving and top-left anchored**:
- **First call** (grids empty): every cell is empty at the new size.
- **Growing** (new W/H larger): existing cells keep their positions; new cells on the right and bottom are empty.
- **Shrinking** (new W/H smaller): the top-left W x H region is kept; cells outside it are discarded.
- **Unchanged** dimension: no effect on that axis.

`resize` never clears surviving content. (To discover-and-crop by content, use `trim`, section 6.3; to add margins, use `pad`, section 6.5.) Dimensions must be positive.

### 6.2 `upscale(N, M)`

Multiplies the current grid size by N in width and M in height. Every existing cell is duplicated into an N x M block across all layers, preserving its value. Empty cells remain empty after upscale. Factors must be positive.

### 6.3 `trim()`

Crops the active grids to the smallest rectangle that contains all meaningful content.

```ls
program {
    resize(60, 40)
    one start
    some(max=100, policy=incremental) rwalk
    trim()
}
```

Semantics:

- Compute the union bounding box of all cells that are non-empty *in any layer*. A cell counts as non-empty if any of the program's grids holds a value (not `.`) at that position.
- Crop every layer to that bounding box. The new active size becomes the bounding-box dimensions.
- Cells inside the bounding box keep their values; cells outside are discarded.
- The bounding box is shifted to origin (0, 0); coordinates of remaining cells are translated accordingly.

Edge cases:

- If every cell of every layer is empty, `trim()` is a no-op and a warning is emitted (section 7.4).
- `trim()` takes no arguments.

`trim` is a distinct operation rather than a mode of `resize`: it *discovers* dimensions from content rather than *setting* them.

### 6.4 `mirror(axis)`

Folds the grid across its centre axis, copying the origin-side half onto the far side (reflected), across **all layers** at once. Dimensions unchanged.

- `mirror(horizontal)` - reflect across the vertical centre axis: the **left** half is copied onto the right, mirrored. For odd width W, the middle column (index floor(W/2)) is on the axis and unchanged. For even width, the left floor(W/2) columns reflect onto the right floor(W/2) (no fixed middle).
- `mirror(vertical)` - reflect across the horizontal centre axis: the **top** half is copied onto the bottom, mirrored. For odd height, the middle row is unchanged.

The origin-side half (left / top) is the source and is never modified; the far half is overwritten with the reflection. Operating per (grid, cell) across all layers keeps co-registered layers aligned. `mirror` takes one axis; there is no `mirror(both)` (compose horizontal then vertical for quadrant symmetry) and no per-axis value (asymmetric/partial mirroring is out of scope - keep the grid small instead). `mirror` is the program-level counterpart of the `symmetry` rule attribute (section 5.6.1) and reuses its axis vocabulary.

### 6.5 `pad(N)`

Adds `N` empty rows/columns on **all four sides**, across all layers, shifting existing content by (N, N). New size = (W + 2N, H + 2N); added cells are empty. `N` is non-negative.

`pad` is the only geometric operation not top-left anchored - a uniform border necessarily moves content off the origin, which is its purpose (framing a generated region, spacing a mirrored arena). To *remove* margins, use `resize` (top-left crop) or `trim` (content crop); `pad` only adds. Uniform-only is deliberate: per-side margins would reserve `left`/`right`/`top`/`bottom`, prime identifier names in generators. If non-square margins are ever needed, `pad(horizontal=N, vertical=M)` reusing the existing axis vocabulary is the keyword-free extension point.

### 6.6 `path(...)`

Computes a shortest route from any `from` cell to the nearest reachable `to` cell over the traversable cells, and writes `write` into `into` along that route (endpoints included and overwritten). Other cells of `into` are left as they are. The first **structural** operation: the non-local work is one operation; downstream *rules* consume the result with ordinary same-position reads.

Parameters (section 6.0):
- `from : pred` (required) - start cell(s): a tag or `( bool-expr )`; multi-source allowed.
- `to : pred` (required) - goal cell(s); the route ends at the nearest reachable one.
- `into : grid` (required) - the grid the route is stamped into.
- `write : value` (required) - the value stamped on each route cell (typed by `into`).
- `over : grid?` (optional) - the **connectivity substrate**. Omitted, the route runs over the implicit grid adjacency of the coordinate space. This is the **seam** for the planned graph layers (section 9): a graph reference here will route over its edges - the same operation, one swapped input. Until graphs land, only grid adjacency is defined.
- `passable : pred?` (optional, default: any non-empty cell) - traversable cells. `from`/`to` cells are always traversable.
- `connectivity : enum{4,8}?` (optional, default `4`) - 4- or 8-neighbour grid adjacency; ignored when `over` is a graph.
- `cost : expr?` (optional, default `1`) - per-step weight at the entered cell. Default 1 is breadth-first (uniform shortest path); a non-constant cost is weighted (Dijkstra). Non-positive cost is treated as 1.

Semantics: the route is the minimum-total-cost sequence of adjacent traversable cells from a `from` cell to the nearest `to` cell, **overwriting** `into` on the route cells. Ties break deterministically by seed (section 7.2). If no `to` is reachable (including an empty `from` or `to` set), `path` is a **no-op and emits a warning** (like `trim` on empty content, section 7.4), not an error. It writes only the route and exposes no distance field. Because a grid is an implicit graph, `path` also underlies connectivity guarantees, corridor carving, and room-linking; it generalizes to explicit graphs through `over`.

**Predicate resolution.** A **bare tag** (or named union) as a `pred` reads **the unique tag layer whose tagset declares that name**, with pattern-cell overlap semantics (`(stored & mask) != 0`, section 4.1); if no layer's tagset declares it, or **more than one** layer could hold it, that is a compile error (section 7.3, check 35) - use the expression form and name the grid (e.g. `passable=(level == floor)`). An expression `pred` is any boolean expression over same-position reads, `x`/`y`/`width`/`height`, and params. The **default `passable`** - "any non-empty cell" - means non-empty in **at least one layer**, the same union-of-layers content notion `trim` uses (section 6.3). Note that expressions have no mask-overlap operator, so the expr form tests exact `==` while the bare-tag form is pattern-style any-overlap.

**The draw contract.** `path` pins its evaluation and PRNG behaviour exactly, so that the output is a pure function of (seed, params) regardless of how the search is implemented:

1. The `from`, `to`, and `passable` predicates each evaluate in **one row-major pass** over the current extents, **in that order** (`from` first, then `to`, then `passable`). A `random` inside a predicate draws in that pass order.
2. `cost` evaluates **once per traversable cell**, row-major, clamped to >= 1; the search runs on those fixed weights. A `random(...)` cost draws once per traversable cell, reproducibly.
3. Tie-breaking consumes **exactly one stream draw per `path` statement**. Per-cell tie keys are derived from that single draw by hashing it with the cell index (splitmix64) - there are **no per-discovery draws**, so the number of draws a `path` statement consumes never depends on grid content or search order.
4. The route is a **pure function of the distance field** the fixed weights induce. The nearest goal is the reachable `to` cell minimizing (distance, tie key) lexicographically. The route is derived by walking backward from that goal, at each step choosing the minimum-tie-key predecessor among those on an optimal (shortest) path. The route is then stamped **start -> goal**: if `write` is an expression, it evaluates once per route cell in that order.

Consequence, stated normatively: **the search algorithm is unobservable.** Any correct shortest-path implementation - BFS at uniform cost, Dijkstra, A* with an admissible heuristic - produces the identical distance field and hence the identical route, draw count, and output. (This is a deliberate departure from MGSL, whose per-discovery tie draws made the search's discovery order part of the observable contract.)

### 6.7 Rule application semantics

An apply statement over a rule pairs a **count** (`one`/`all`/`some`) with an **execution policy** (`policy=`, default `snapshot`). (An apply statement over a sequence takes a count only; see section 6.10.) The count bounds how many applications occur; the policy governs what each application sees and how conflicts are handled.

All policies share **candidate collection**: for each anchor position in row-major `(y, x)` order, the rule's match side is tested - every sub-rule and every symmetry/rotation variant (section 5.6) - at that anchor. Each matching `(anchor, sub-rule, variant)` is a **candidate**. The anchor is the pattern's top-left origin (section 5.7); a candidate exists only where the (possibly reshaped) variant fits entirely within the grid - **matching is bounded, never wrapped**.

**`snapshot`** (default) - one frozen snapshot, one pass:
1. Snapshot the grid stack (section 7.1); collect all candidates against it.
2. Shuffle the candidates (seeded). Empty the write-protection mask (section 6.8).
3. In shuffled order, for each candidate: if its **write** footprint intersects a marked (grid, cell), skip; else apply the write and mark its write footprint. Count one application.
4. Stop at the count cap (`one` = 1; `some(max=N)` = N; `all` = the whole set). For `some(percent=P)`, the cap is floor(P * A / 100) where **A is the number of applications a full pass would make** - the non-conflicting applied set, *not* the raw candidate count (raw candidates would let redundant variants and skipped conflicts inflate the denominator, so `percent=50` could cover far more than half the grid). Concretely: the pass first determines the non-conflicting sequence the batch would apply, then applies its first floor(P * A / 100) entries.

Because all candidates saw one snapshot, earlier writes are invisible to later applications; the mask alone prevents two applications writing the same cell.

**`incremental`** - each application re-snapshots and therefore sees prior writes:
1. Snapshot; collect; pick one candidate uniformly (a single draw; under `ordered`, uniformly within the highest-priority group, section 10.4). Apply and commit; the `{ any }` alternatives on its write tree are drawn at application, weighted per section 5.5.
2. Repeat. Each step re-snapshots, so the next pick sees the last write.

One application per step means no intra-step conflict, so **`incremental` uses no mask**. Stop at the count cap; `all` runs until a step finds no candidate (**fixpoint**, section 6.9).

**`stabilize`** - iterated `snapshot` sweeps to a fixpoint:
1. Run a full `snapshot` sweep (steps 1-4 above, applying the whole set).
2. If that sweep changed any cell, repeat from a fresh snapshot; else stop.

Each sweep is internally a `snapshot` batch (mask per sweep); between sweeps the grid re-snapshots, so a later sweep sees the previous sweep's result. This is the cellular-automaton case (smooth until stable). For `stabilize` the count unit is a **sweep**: `all(policy=stabilize)` = sweeps to fixpoint; `some(max=N, policy=stabilize)` = at most N sweeps. `percent` is not valid with `stabilize`.

**Count x policy validity.**

| | `snapshot` | `incremental` | `stabilize` |
|---|---|---|---|
| `one` | yes | yes (same distribution as snapshot; different draws) | no |
| `some(max=N)` | yes (up to N of batch) | yes (N steps) | yes (N sweeps) |
| `some(percent=P)` | yes | no | no |
| `all` | yes (full batch) | yes (fixpoint) | yes (fixpoint) |

Invalid combinations are compile errors (section 7.3). `percent` needs the whole applied set as its denominator (it runs the full pass to size it), so it is `snapshot`-only. `one` with `stabilize` is contradictory (a single application cannot reach a sweep fixpoint). `one` under `snapshot` and under `incremental` select from the same distribution, because the first entry of a shuffle is a uniform pick. They consume different draws, however (a full shuffle vs. a single uniform pick, section 10.4), so a given seed yields different outputs.

**Variants and alternatives.** Each matching variant is a **separate candidate**, so shuffling treats variants with equal priority - no top-left/first-declared bias; when several variants match one anchor, the one applied is whichever the shuffle selects. At application the runtime walks the resolved write tree and draws once per `{ any }` node on the resolved path (section 5.2), each weighted per section 5.5 and reproducible per seed (section 10.6).

All random choices are deterministic per (seed, params) and per implementation version (section 7.2).

### 6.8 The write-protection mask

The mask exists only in the **batch-family** policies (`snapshot`, and each sweep of `stabilize`). `incremental` applies one write per step and needs no mask.

- The mask is keyed by **(grid, cell)** - a per-layer set, not a single grid-sized mask. Marking `level` at (x, y) does not protect `items` at (x, y).
- It tracks **write footprints only**. A candidate is skipped iff its **write** footprint intersects a cell already marked this pass. **Read footprints do not participate** - under snapshot semantics (section 7.1) a candidate reading a just-written cell already sees the pre-write snapshot value, so there is nothing to protect against, and skipping on reads would wrongly prevent a synchronous CA sweep (every cell that reads a neighbour's written value would be suppressed).
- Skipped candidates do not consume the count budget.
- Footprint intersection is per (grid, cell), never per bounding rectangle: two candidates whose rectangles overlap both apply if the overlap is entirely `*`-preserved (not in either write footprint) in the same grid.

The mask is the only conflict mechanism; the runtime special-cases no other "conflict." It is reset at the start of each `snapshot` pass (and each `stabilize` sweep).

### 6.9 Termination

**Bounded strategies over a rule always terminate** (for sequences, see below): `one`; `some(max=N)` and `some(percent=P)` under any policy; and `all(policy=snapshot)` (a single pass over a finite candidate set).

**Fixpoint strategies run until nothing changes:** `all(policy=incremental)` and `all(policy=stabilize)`. These terminate **iff the rule is reductive** - iff repeated application cannot keep producing new matches. A reductive rule (fills empties, removes tags, narrows values) reaches a fixpoint; a generative one (a rule whose write recreates its own match) may not, and `all` with a fixpoint policy will then loop forever.

The language does not verify reductivity in general - a fixpoint strategy is a loop with a computed exit, like `while (...)`: the construct is provided, and not writing a divergent rule is the author's responsibility. This is a deliberate, scoped weakening of universal termination: bounded strategies keep it; fixpoint strategies trade it for expressive iteration.

**The reductivity warning.** The compiler does, however, flag the *guaranteed*-divergent case (a LevelScript addition over MGSL, which left this entirely to the author). For an `all(policy=incremental)` statement, consider each sub-rule's **unconditional** writes - the write leaves that occur on every resolution of the write tree; leaves under an `{ any }` do **not** count, since the pick may avoid them. If some sub-rule has no unconditional write that **invalidates its own LHS** - i.e. no write that overwrites an LHS-constrained cell of the same grid with a value that no longer matches that cell's requirement - then an applied anchor re-matches forever and the fixpoint is unreachable: the compiler emits the warning *"'all(policy=incremental)' over rule '<name>' may never terminate: a sub-rule's write leaves its own match intact, so the fixpoint is unreachable."*

The analysis is conservative in both directions:
- **Uncertainty suppresses the warning.** A computed (expression) match or write cell, and any `where` guard, make the invalidation question undecidable at compile time; such a sub-rule is assumed to invalidate and produces no warning. Only the statically certain case warns.
- **Silence proves nothing.** Passing the check does not prove termination; a rule can still diverge through interactions the per-sub-rule check cannot see.

`all(policy=stabilize)` is **exempt**: its loop exits on a no-change sweep, so an idempotent write (one that re-matches but rewrites the same value) still reaches the fixpoint - the guaranteed-loop argument does not apply.

The warning is a compile-time **warning**, not an error (section 7.4); it is surfaced to embedders via `generator::warnings()` (Appendix A) and to CLI users on stderr.

**Sequences** (section 6.10). `one S` and `some(max=N) S` bound the number of **iterations**; each iteration terminates iff its body statements do, so a fixpoint statement inside the body can still diverge. `all S` is a fixpoint strategy: it terminates iff some iteration is stable. As with rules, that is the author's responsibility; the compiler flags only the case that is certain to diverge by changing dimensions (section 6.10).

### 6.10 Sequences

A **sequence** is a named, reusable statement list, applied by name like a rule.

```ls
sequence smooth {
    all erode
    all grow
}

program {
    resize(40, 25)
    all fill
    some(max=4) smooth    // up to 4 iterations of the body
    all smooth            // iterate until an iteration changes nothing
}
```

**Grammar:**
```
sequence_decl ::= 'sequence' IDENT '{' statement_list '}'
```
The body is the program's `statement_list` (section 6), unchanged: rule applications, sequence applications, and operation calls, each with an optional `when` guard. There are no inline rules. Rules and sequences share one namespace; an `apply_stmt`'s `IDENT` resolves to either one (section 7.3, check 39). A sequence may apply other sequences. A sequence that applies itself, directly or through others, is a compile error (check 38). Declaration order does not matter, and an unapplied sequence is legal, like an unused rule. An empty body is legal; every iteration of it is stable.

**Iteration.** Applying a sequence runs its body from top to bottom; one run of the body is an **iteration**. Each body statement executes exactly as it would in `program`, with its own count, policy, and guard, and sees every prior write. A sequence adds no snapshot, write mask, or pass of its own.

**Stability.** An iteration is **stable** iff the grid stack at its end equals the stack at its start: the same dimensions, and every cell of every layer holding the same value. Stability compares states, not writes. Writing an unchanged value does not count as a change, and neither does a cell changed and changed back within the iteration. Stability is judged on the iteration just run. A stable iteration ends the statement even if a further iteration could have changed the stack through different draws (`random`, `{ any }`, `path` ties); the same holds for `stabilize` sweeps (section 6.7).

**Counts.** The count unit is one iteration:

| statement | runs |
|---|---|
| `one S` | exactly one iteration |
| `some(max=N) S` | up to N iterations; stops early after a stable iteration |
| `all S` | iterations until one is stable (**fixpoint**, section 6.9) |

The early stop under `some(max=N)` mirrors `some(max=N, policy=stabilize)`: the count is an upper bound, and a stable iteration ends the statement. `policy=` is not valid on a sequence application, because each body statement carries its own policy. `some(percent=P)` is not valid either: its denominator would be the number of iterations a fixpoint run takes, which may be unbounded (check 37). For a fraction of each inner batch, write `percent` on the inner statements.

**Guards.** A `when` on a sequence application gates the whole application. It is evaluated once, when the statement is reached (section 6). A `when` on a statement inside the body is evaluated each time that statement is reached, which is once per iteration. Guards read params only, so the value is constant unless the guard calls `random`, which draws on every evaluation.

**Draw order.** A sequence application has no draw sites of its own, and the stability check draws nothing. Body statements draw per section 10.6 as each one is reached, iteration after iteration.

**The dimension warning.** For `all S`, the compiler warns when no iteration can be stable because every iteration changes the grid dimensions. This happens when S's body contains an `upscale` whose factors are not both 1, or a `pad` whose margin is greater than 0, and that operation is **reached on every iteration**. That requires two things: neither the operation nor any enclosing nested sequence application carries a `when` guard, and every enclosing nested application runs at least one iteration, which every count does (`some(max=0)` is an error, check 28). The warning reads: *"'all' over sequence '<name>' may never terminate: '<operation>' changes the grid dimensions on every iteration, so no iteration can be stable."*

Like the reductivity warning, this check is conservative. A guard suppresses the warning, because it may be false. Silence proves nothing, because a rule in the body can still keep recreating its own match. Bounded applications (`one`, `some(max=N)`) are never warned: `some(max=3) grow_and_upscale` is legitimate intended growth.

---

## 7. Execution model

### 7.1 Snapshot semantics

Matching is always evaluated against a **snapshot** of the grid, never against a partially-written live grid. The unit that shares a snapshot is the **pass**, and passes differ by policy: `snapshot` is one pass (every candidate sees statement-entry state); `stabilize` is a sequence of passes (each sweep re-snapshots, seeing the prior sweep); `incremental` re-snapshots every application (each sees all prior writes). Within any single pass, all candidates see the same input and earlier applications' writes are invisible; the write-protection mask (section 6.8) prevents conflicting writes within a batch pass. At end of statement the live grid becomes the next statement's input.

Implementations may realize a pass's snapshot as a full copy, copy-on-write per touched cell, or any equivalent mechanism (section 10.3); only the visibility rules above are observable.

The snapshot mechanism is independent of the write-protection mask (section 6.8). The snapshot guarantees match consistency *across* candidates within a pass; the mask (used by the batch-family policies) prevents conflicting *writes* among the candidates that have already succeeded.

### 7.2 Determinism

A LevelScript run's output is a pure function of **(seed, params, implementation version)**. Seed and params are part of the runtime invocation, not the program source; given the same seed and the same parameter values (section 4.2), a given build of the implementation produces the same output, bit for bit.

The mechanism is a **single sequential PRNG stream** (the reference implementation uses `mt19937_64`, seeded from the invocation seed) with a **pinned draw order**: every stochastic decision - candidate shuffling, weighted `{ any }` selection, `random(...)` calls, `path` tie keys - consumes draws from that one stream at points fixed by the execution model (section 10.6). There is **no position-keyed PRNG**: this was a reopened decision for LevelScript and one stream was deliberately kept for simplicity, accepting the relaxed cross-version claim below (a position-keyed PRNG would decouple draws from evaluation order and permit parallel matching; it remains a possible future change).

Consequences:
- `when` guards and param expressions are evaluated **once** (params at startup, a guard when its statement is reached), so each is constant for the remainder of the run; a `random` there draws once, reproducibly.
- Changing an input param may shift the draw sequence and cascade through the rest of the run - expected in PCG, not a determinism defect (a small input change is not expected to produce a small output change).
- Determinism is **per implementation version**: a future version may change evaluation/scan/shuffle order and remain internally deterministic; cross-version (and cross-implementation) reproducibility of specific outputs is not promised. What *is* promised across versions is the semantics of this document, not the byte-identical artifact of a given seed.

### 7.3 Compile-time checks

The compiler must reject:

1. **Pattern bodies with inconsistent row widths.** All rows must have the same number of cells.
2. **Empty pattern bodies.** A pattern body with zero cells is a syntax error; with zero meaningful rows it is a semantic error.
3. **Shape mismatch.** The match and write sides of a `pattern_pair` must have identical dimensions, recursively through nested write blocks (section 5.4).
4. **Empty combinator block.** A combinator block (`{ all }`, `{ any }`, or a combinator rule body) with zero items. (Single-item blocks are legal, section 5.2.)
5. **Weight scope.** `(weight=N)` is rejected inside `{ all ... }` blocks and on bare patterns (parse error).
6. **Tag/grid resolution.** Every identifier in a pattern cell must resolve per section 5.8 (for a bare mask literal: a tag value or union of the grid's declared tagset).
7. **Attribute validation.** Attribute names and values must be in the table of section 5.6.
8. **Reference resolution.** Grid names, tag names, rule names, and sequence names must be declared somewhere in the file. (Forward references in the program block are allowed; declaration order does not matter.)
9. **Single program per file.** Exactly one `program_decl` per source file.
10. **Tagset over cap.** A tagset with more than 30 values.
11. **`where` on RHS.** A `where` pattern on the write side.
12. **Reserved-name collision.** A tag value, grid, or param named `x`, `y`, `width`, or `height`; or a param colliding with a grid name.
13. **Bare computed expression.** A computed expression (comparison/arithmetic/logical/call) in a cell without enclosing `( ... )`.
14. **Expression type errors.** An operator applied to the wrong value kind; an expression in a real cell not evaluating to the grid's type; comparing across distinct tagsets; a `.` with no inferable type.
15. **Complement write.** `!`tag used on the RHS (complement is match-side only).
16. **Unknown function.** A call to a name that is not a section 5.10 built-in.
17. **Duplicate params block.** More than one `params` block per file.
18. **Invalid transform value.** A `rotation=` value that is not `none`/`all`, a bare `90`/`180`/`270`, or a set `{...}` of those angles; a `symmetry=` value other than `none`/`horizontal`/`vertical`/`all`; or an angle set containing a value other than 90/180/270. (An empty set `{}` and a `symmetry` set are also errors.)
19. **Function arity.** A built-in call with the wrong number of arguments.
20. **Function argument type.** A built-in argument of the wrong kind (e.g. `abs` of a tag); or `if` whose two branches differ in type; or `if` whose condition is not bool.
21. **Built-in name collision.** A tag value, grid, or param named `if`, `min`, `max`, `abs`, `clamp`, or `random`.
22. **Guard scope.** A `when` guard that reads a grid, `x`/`y`, or `width`/`height`; or whose expression is not boolean.
23. **Derived param scope.** A derived param expression that reads a grid, `x`/`y`, or `width`/`height`; or that forward-references a later param; or that participates in a cycle.
24. **Derived param type.** A derived param whose expression is not a `number`.
25. **Invalid union.** A `tag_union` whose member is not a `tag_value` or earlier `tag_union` of the same tagset; a member that forward-references a later union; or a union cycle. (A union name colliding with a `tag_value` name, a grid, a param, a reserved identifier, or a built-in is caught by the existing duplicate/collision checks - a union shares the tagset's value namespace.)
26. **Default param scope.** A default expression (section 4.2) that reads a grid, `x`/`y`, or `width`/`height`; that forward-references a later param; or that participates in a cycle. (Same discipline as check 23; a default may reference earlier params and may call `random`.)
27. **Input param without a default.** An `input_param` written as `name : number` with no `= expr` (every input param must carry a default, section 4.2).
28. **Invalid count/policy combination.** For a rule application: `percent` with a policy other than `snapshot`; `one` with `policy=stabilize`; `max` and `percent` both in one `some(...)`. For any application: `some(max=0)`, which applies nothing. (Sequence applications: check 37.)
29. **`ordered` misplacement.** `ordered` used as a match-side or write-side combinator (it is body-level only).
30. **Unknown policy.** A `policy=` value not in {`snapshot`, `incremental`, `stabilize`}.
31. **Same-grid simultaneous write.** Within one `{ all }` write block, two items whose write footprints overlap on the **same grid** at the same cell (e.g. `{ all g[wall] g[floor] }`). Simultaneous writes to one cell are ambiguous; use a union `|` (section 5.5) to store multiple values. (Different grids at the same position are fine - the mask is (grid, cell)-keyed, section 6.8.)
32. **Unknown operation.** An `op_call` (section 6.0) whose name is not in the operation table.
33. **Operation argument arity/binding.** More positional arguments than the operation's positional parameters; a named-only parameter supplied positionally; a named argument preceding a positional one; or a parameter supplied both positionally and by name.
34. **Missing required operation argument.** A required parameter not supplied.
35. **Operation argument kind / enum.** An argument whose kind does not match the parameter (e.g. `grid` given a non-grid); an `enum` value outside its set (`mirror` axis not in {horizontal, vertical}; `connectivity` not in {4, 8}); or an ambiguous bare-tag `pred` (no layer, or more than one layer, could hold the tag - section 6.6).
36. **Operation value constraint.** `resize`/`upscale` dimensions must be positive; a `pad` margin must be non-negative. (Value checks applied after kind resolution.)
37. **Invalid sequence count.** `policy=` on a sequence application (any policy, including an explicit `snapshot`), or `some(percent=P)` of a sequence (section 6.10).
38. **Sequence cycle.** A sequence that applies itself, directly or through other sequences.
39. **Duplicate rule or sequence name.** Rules and sequences share one namespace: two declarations with the same name, whether both are rules, both are sequences, or one of each.

### 7.4 Warnings

Warnings never stop compilation or execution; the runtime degrades with a warning and continues, never faults.

**Compile-time warnings** (surfaced via `generator::warnings()`, Appendix A):

1. **Reductivity.** `all(policy=incremental)` over a rule where no unconditional write invalidates its own LHS - "may never terminate" (section 6.9). Not an error: the analysis is conservative, and a build pipeline may still choose to treat warnings as failures.
2. **Unreachable sequence fixpoint.** `all` over a sequence where every iteration is certain to change the grid dimensions, through an `upscale` or `pad` reached on every iteration (section 6.10).

**Runtime warnings** (logged; the statement becomes a no-op):

- `trim()` on an entirely empty stack (section 6.3).
- `path(...)` with no reachable route, or an empty `from`/`to` set (section 6.6); `path` before any `resize` (zero-sized stack).

---

## 8. Worked example

A complete fill-and-carve dungeon generator (see `examples/` for this and smaller single-feature programs):

```ls
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

rule start {
    algo[.]
    =>
    algo[S]
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

rule fill_geo { all
    algo[W] => level[wall]
    algo[F] => level[floor]
    algo[S] => level[floor]
}

rule place_enemies {
    { all
      algo[F]
      enemies[.]
    }
    =>
    { any
      enemies[goblin]
      enemies[troll]
      enemies[dragon]
    }
}

rule decorate { all
    level[floor] => tiles[1]
    level[wall]  => tiles[2]
}

program {
    resize(60, 40)
    some(max=5)   start
    some(max=100, policy=incremental) rwalk
    upscale(2, 2)
    all reduce
    all reward
    all fill_geo
    some(max=10) place_enemies
    all decorate
}
```

Reading the program: `start` seeds up to five `S` markers into one snapshot batch; `rwalk` under `policy=incremental` grows a drunkard's walk from them, each step seeing the last (the `rotation=all` variants walk in all four directions); `upscale` doubles the resolution; `reduce` erodes 2x2 seed blocks; then a cascade of snapshot batches converts the `algo` sketch into geometry, rewards, enemies, and tile indices.

A structural-operation companion (`examples/corridor.ls`): scatter a numeric cost field, place a door and an exit with `where`-pinned rules, then

```ls
path(from=door, to=exit, into=site, write=road,
     passable=((0 == 0)), cost=(1 + rocks))
```

carves the cheapest corridor; equal-cost routes vary by seed, reproducibly (section 6.6).

---

## 9. Future directions

Draft 0.5 covers roadmap steps 1-5 of the implementation. Deferred, in rough priority order:

**Graphs.** The reason for the language's forward-looking design (and its name making room for more than grids): graph layers alongside grid layers, with rewrite semantics over nodes/edges and integration with grid-derived graphs. `path`'s `over=` parameter (section 6.6) is the prepared seam - a graph reference there routes over its edges, the same operation with one swapped input.

**The structural-operation family.** `path` is the first of a family conforming to the section 6.0 envelope (predicate/grid arguments in, result written into a grid, seeded, table-resolved): BSP subdivision, flood-fill regions, MST/room-linking, Voronoi, a full distance field. The graph-generalizable members - `path`, MST, distance - share the `over` seam; BSP and Voronoi are grid-native. Each is a table row plus an executor; none needs a grammar change.

**Templates and imports.** Named constant grids stamped by rules (`template Vault3x3 { ... }`), and `use "path/file.ls"` to share templates and tag declarations across files (Spelunky-style chunk libraries). Deferred together; imports are only compelling once templates exist.

**Inline rules.** Anonymous rules in statement position (in `program` and `sequence` bodies alike, never in only one of them). Open costs: diagnostics and the observe channel identify rules by name, so an inline rule would need a synthesized one (`smooth#2`); rule attributes would have to mix with the statement's count options; `all { all ... }` stacks the count and the body combinator on one word; and design principle 5 favours rules that read in isolation. Parked, not rejected.

**Applicability-driven sequences.** The planned answer to data-dependent `if`, in the MarkovJunior style: a statement **succeeds** if it made any change, and an `ordered` sequence runs its first item that succeeds, then restarts from the top. This gives `else if` over grid state with no condition expressions and no aggregate queries (those belong to graph layers). Parameter-driven branching is already served by `when` guards on sequence applications. Parked.

**Multiple programs per file.** Currently one program per file; multiple named programs (a file as a library of generators) is a likely future need.

**Position-keyed PRNG.** Rejected for 0.5 (section 7.2) but recorded: hashing (seed, x, y) instead of drawing from one stream would decouple match-side `random` from evaluation order and restore parallel matching (section 10.4). It would change every seeded output; if ever adopted, it is a major-version change.

**Rule attribute extensibility.** `symmetry` and `rotation` are the only attributes specified; the parenthesized attribute syntax accommodates future ones (e.g. `probability`) without grammar changes.

---

## 10. Implementation guidance

This section is non-normative; it records how the reference implementation is built and which choices are load-bearing for the semantics above.

### 10.1 Architecture

- **One execution core.** The interpreter is a single C++20 coroutine that yields step events at application and statement boundaries. Batch generation (`generator::generate`), progressive generation (`generation::step`, with granularity a filter on the event puller), and any debug/visualization UI all pull the same coroutine. There is never a second interpreter to drift from the first.
- **The compiled artifact is self-contained.** Semantic analysis copies everything the runtime and the embedding API need - names, tables, compiled rules, the operation schedule - into one immutable object shared by reference counting; the AST is discarded after analysis. Generated levels are likewise self-contained values that outlive their generator (Appendix A).
- **Ids over strings.** Grids, tag values, and rules are integer indices into vectors; names resolve once at the API boundary. Conflict masks are hashed sets keyed by a packed (grid id, flat cell index).
- **Tables over keyword grammar.** Operations (section 6.0) and expression built-ins (section 5.10) resolve by name against schema tables; each layer has one generic call production (`IDENT '(' arg_list? ')'` in expressions, `op_call` in programs), and adding an entry is a table row plus an executor.
- **No faults.** Compilation collects diagnostics; execution degrades with a warning and continues (section 7.4). The embedding API never throws.

### 10.2 Symmetry expansion

Symmetry and rotation are expanded at compile time. `symmetry` contributes up to four dimension-preserving flips (identity, H, V, both-axis); `rotation` contributes turns (identity, 90, 180, 270, or a set). The two are composed and the resulting variant set is **de-duplicated by structural equality of the transformed (match side, write tree) pair, scoped per sub-rule** (so two sub-rules with the same LHS both survive). Structural equality means the same pattern cells (expression cells compared as expression trees), the same `{ any }`/`{ all }` nesting in the same item order, and the same weights. The both-axis flip and the 180-degree rotation are the same transform, so `symmetry=all, rotation=all` collapses to the 8 variants of D4 (fewer when the rule is itself symmetric). The runtime treats each surviving variant as an independent pattern (section 6.7). Elimination happens at compile time, but its result is observable, because each variant is a separate candidate weighting its anchor in the shuffle and in the `incremental` pick. That is why the key is normative (section 5.6.2).

### 10.3 Snapshot implementation

The per-pass snapshot (section 7.1) is implementable several ways; a pass is the whole statement for `policy=snapshot`, one sweep for `stabilize`, and one application for `incremental`. The reference runtime uses a **full copy** (double buffering: matches read the front buffer, writes go to the back, then swap); copy-on-write side tables or per-cell versioning are equivalent and cheaper when few cells are written. A full copy is recommended until profiling says otherwise.

**Empty in number grids.** A `grid of number` stores an **empty sentinel** distinct from every representable integer (a reserved value or a parallel presence bit). Reads coerce the sentinel to `0` for arithmetic and comparison (section 5.8); the `(g == .)` / `(g != .)` tests inspect the sentinel directly, before coercion. Writing `.` stores the sentinel. Tag grids need no sentinel - empty is bit 0 of the mask (section 4.1).

### 10.4 Candidate collection and selection

All policies build from one structure: a vector of **candidate slots**, each a small record of indices - `(anchor_x, anchor_y, sub_rule_idx, variant_idx)`. The chosen `{ any }` alternative is **not** stored; it is drawn at application (section 10.6). Slots reference compile-time tables (sub-rules, expanded variants), so a candidate is a few small integers.

**Eager collection.** The reference implementation collects and matches the whole candidate vector per pass (row-major anchors; cells tested with short-circuit per candidate; `where`/expression cells - including `random` draws - evaluate during this scan). A semi-lazy split (enumerate slots cheaply, match only when pulled, so `one`/`some`/`incremental` never match the whole grid) is an available optimization - note that it would move match-side `random` draw sites, which the per-implementation-version determinism claim (section 7.2) permits but a golden-output corpus will notice.

**Selection per policy:**
- `snapshot` / `stabilize` (per sweep): collect, seeded-shuffle, pull in shuffle order under the (grid, cell) write mask, stop at count. Shuffling is what gives variants equal priority.
- `incremental`: collect, pick **one** uniformly (a single draw; under `ordered`, uniformly within the highest-priority group), apply, **re-collect** (the write changed the match set); repeat.
- `ordered` is a **sort of the vector**, not a per-anchor short-circuit: group by sub-rule priority, shuffle within each group, then consume exactly as the shuffled vector would be by the active policy.

**Parallelism (future).** The split makes the parallelizable phase explicit - independent match-testing over the candidate vector: `snapshot` matching is embarrassingly parallel (all vs one snapshot) except when the match side draws (`random` shares the one stream and serializes; a position-keyed PRNG would lift this, section 9); `stabilize` is parallel within a sweep, serial across sweeps; `incremental` is inherently sequential.

### 10.5 Match enumeration

For small patterns and small grids (the common case), a per-position brute-force match is adequate: test each pulled slot cell-by-cell against the snapshot, short-circuiting on first mismatch. Optimization (indexing, bitmap-accelerated scans) is deferred until profiling justifies it.

When a `where` or match cell contains `random`, the matcher's cell-test order, variant-test order, and short-circuit behaviour become **observable** (they determine which draws fire and in what sequence, section 10.6); such a rule cannot have its slots matched in parallel or reordered without changing output. Rules with no `random` on the match side keep that freedom.

### 10.6 Random seeding and draw order

A single PRNG stream (`mt19937_64` in the reference implementation), seeded from the invocation seed, drives all stochastic decisions. Every draw site is ordered by the execution model, so the sequence is fixed per (seed, params):

1. **Param expressions** (section 4.2) evaluate once at startup, in declaration order (an input's default only when the param was not supplied) - their draws come first.
2. A **`when` guard** (section 6) evaluates once when its statement is reached, in program order.
3. **Match-cell and `where`-cell** expressions evaluate during the collection scan, in row-major anchor order and, within an anchor, in sub-rule/variant declaration order; cells test in row-major order, short-circuiting on first mismatch - a `random` in a cell not reached does not draw.
4. **Candidate ordering** draws next: the pass's seeded shuffle (batch family) or the single uniform pick (`incremental`), per section 10.4.
5. **At application**: one draw per `{ any }` node on the resolved write path, outer before inner, earlier sibling first (a single-item `{ any }` still draws, section 5.2); then write-cell expressions, row-major per resolved leaf.
6. **`path` statements** follow the section 6.6 draw contract: predicate passes (`from`, `to`, `passable`), per-cell `cost`, exactly one tie-key draw, then the start-to-goal `write` stamps.

A sequence application (section 6.10) adds no draw sites: its body statements draw per items 2-6 as each one is reached, and the stability check draws nothing.

Determinism is per (seed, params, implementation version) - section 7.2.

### 10.7 Sequence stability

The reference approach copies the grid stack at the start of an iteration and compares it at the end. That costs one extra stack, the same order of cost as the snapshot double buffer (section 10.3). A cheaper equivalent keeps a per-(grid, cell) set of cells written during the iteration, and compares only those against their start values, plus the dimensions. It must still compare values, because a cell written back to its original value is not a change (section 6.10). Both approaches are unobservable. `one S` never needs the comparison.

---

## Appendix A: The embedding API

The reference implementation is a C++ library first and a CLI second. A game embeds `ls.hpp` and sees exactly four types - `generator`, `level`, `grid`, `generation` - in namespace `ls`. Nothing throws: a failed compile yields a falsy `generator` carrying `error()`, and every query on an invalid object reads as empty.

```cpp
auto gen   = ls::generator::compile(source);        // once, at load
int  wall  = gen.tag("geometry.wall");              // resolve names once
auto level = gen.generate(seed);                    // pure in (seed, params)
auto geo   = level["level"];
if (geo.at(x, y) == wall) ...
```

**The factory-vs-value split.** A `generator` is a **stateless, reusable factory**: one per `.ls` file, shareable across threads; each `generate`/`begin` call is independent and carries its own run state. A `level` is a **self-contained owned value**: it owns its cells and outlives the generator that made it. A `grid` **shares ownership** of the level data it views, so a grid handed to a garbage-collected scripting host can outlive the `level` object it came from. Internally, one coroutine-backed execution core (section 10.1) serves both `generate` (drain to completion) and `begin`/`step` (pull incrementally); they cannot diverge.

### `generator` - one compiled program

| member | meaning |
|--------|---------|
| `static generator compile(const std::string& source, const std::string& name = "generator")` | Compile source text. The game owns file/asset IO; `name` labels diagnostics (`"dungeon.ls:12:3: error: ..."`). |
| `explicit operator bool()` | True iff compilation succeeded. |
| `std::string error()` | Formatted diagnostics when compile failed; `""` on success. |
| `std::string warnings()` | Formatted warnings of a successful compile (e.g. the reductivity warning, section 7.4); `""` when none. |
| `int tag(const std::string& qualified)` | Tag value id, qualified by tagset: `tag("geometry.wall")`. Ids are per tagset, valid for every layer of that tagset. `-1` if unknown. |
| `level generate(uint64_t seed, const std::vector<std::pair<std::string, int>>& params = {})` | Run the whole program: (seed, params) -> `level`, deterministically (section 7.2). Params override declared defaults; unknown names are ignored. |
| `generation begin(uint64_t seed, step_mode mode = step_mode::statement, observe obs = observe::off, const std::vector<std::pair<std::string, int>>& params = {})` | Start a progressive run; pull it with `generation::step()`. |
| `int statement_count()` | Number of top-level program statements (progress denominators). Statements inside sequences are not counted; a sequence application counts as one. |

### `level` - one generated outcome: the stack of co-registered grids

| member | meaning |
|--------|---------|
| `int width()`, `int height()` | Final dimensions. |
| `int layer_count()` | Number of declared layers. |
| `grid operator[](const std::string& layer_name)` | Layer by name. |
| `grid layer(int index)` | Layer by declaration order. |
| `std::string layer_name(int index)` | Name of the layer at `index`. |

### `grid` - one layer of a generated level

| member | meaning |
|--------|---------|
| `int at(int x, int y)` | The tag value id at (x, y), or the stored number for number layers; `-1` when empty (or out of range / invalid). |
| `bool empty(int x, int y)` | True iff the cell is empty. |
| `bool is_number()` | True for `grid of number` layers. |
| `std::string name(int value_id)` | Name of a tag value id (`""` for number layers or out of range). |

### `generation` - one in-flight progressive run

| member | meaning |
|--------|---------|
| `bool step()` | Advance by the granularity fixed at `begin()`; `false` when done. |
| `level snapshot()` | Copy of the current state - committed statements plus the current batch's applications so far. |
| `level finish()` | Drain whatever remains and return the finished level (the "skip" path). |
| `int stmt_index()` | Index of the top-level program statement the last step worked on (`-1` before the first step). Inside a sequence, this is the index of the top-level statement that applied it. Observe channel. |
| `std::vector<stmt_frame> stmt_stack()` | Position of the last step through nested sequences, outermost first. Frame 0 is the top-level statement (its `index` equals `stmt_index()`); each further frame is a statement inside the sequence applied by the frame before it. Empty before the first step. Observe channel. |
| `std::vector<cell_highlight> highlights()` | Matched/written cells of the last application. Observe channel; populated only when begun with `observe::on`. |

Supporting types:

- `enum class step_mode { statement, application }` - the pull granularity: one **leaf** statement per `step()`, or one rule application per `step()` (operations and atomic batches still advance whole). A leaf statement is a rule application or an operation, at top level or inside a sequence. Applying a sequence is not itself a step, and neither is its stability check.
- `enum class observe { off, on }` - whether the run records the observe channel (`stmt_index`, `stmt_stack`, `highlights`); off costs nothing.
- `struct stmt_frame { int index; int iteration; }` - one level of the statement stack. `index` is the statement's position in its enclosing list (the program, or a sequence body). `iteration` is the 0-based iteration of the enclosing sequence application (always 0 for the top-level frame).
- `struct cell_highlight { enum class kind { match, write }; int layer; int x, y; kind what; }` - one highlighted cell: which layer/cell the last application matched or wrote.

`generation` is move-only (it owns the in-flight coroutine state). Dropping it mid-run is safe; `finish()` is the explicit skip-to-end.
