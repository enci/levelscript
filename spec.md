# LevelScript

**Status**: Draft 0.8.0 (implementation version `0.7.x`, see `ls-versioning.md`)
**File extension**: `.ls`
**CLI**: `levelscript [--seed N] [--entry name] [--param name=value ...] <file.ls>` (`--entry` defaults to `main`, a tool convention; section 6)
**Embedding**: namespace `ls::` (see Appendix A)
**Scope**: Core language semantics for grid-based procedural generation.

LevelScript is a domain-specific language for procedural generation of grid-based content (roguelike dungeons, puzzle layouts, tile maps). A generator describes how to transform a stack of correlated grids through a sequence of pattern-rewrite rules, orchestrated by a small set of built-in operations.

The reference implementation is a C++ library first and a CLI second, built around a single coroutine-backed execution core, a self-contained compiled artifact, and an embedding-first API (section 10, Appendix A). Graph layers are the planned next step (section 9). This document has no changelog.

---

## 1. Design principles

1. **Declarative**: a generator describes *what to match and write*, not *how to iterate*.
2. **Visual**: pattern bodies look like the grids they match; an editor can render them with colored backgrounds.
3. **Minimal surface area**: a small set of orthogonal concepts (tags, grids, rules, sequences, operations, modules) covers the design space.
4. **Predictable execution**: snapshot-based rule application; one well-defined collect-then-pick model for all modes.
5. **Cognitive load over terseness**: a rule should read clearly in isolation, even if that costs a few characters. Boilerplate is required only when ambiguity would otherwise exist.

---

## 2. Lexical and file structure

Formal grammar productions throughout this document use EBNF: `::=` defines a production; `|` separates alternatives; `?` is zero or one; `*` is zero or more; `+` is one or more; `( )` groups; quoted strings are literal terminals; `UPPER_CASE` names are lexical tokens; lowercase names are non-terminals. Whitespace and comments are skipped between tokens everywhere except inside pattern cell grids, where newlines and commas act as row separators (section 5.3).

**Grammar** - top-level structure:
```
source_file ::= use_decl* top_decl*
top_decl    ::= tag_decl | layers_decl | params_decl | rule_decl | sequence_decl
```

### 2.1 Source files

LevelScript source files have the extension `.ls` and are UTF-8 encoded. Identifiers are ASCII (see section 2.3); non-ASCII bytes are only valid inside comments. Unicode identifiers are deferred to a future version. Each source file is a module (section 2.6).

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
STRING         ::= '"' <any chars except '"' and NEWLINE> '"'
COMMENT        ::= '//' <any chars> NEWLINE
```
A `STRING` has no escape sequences and is ASCII (section 2.1). Its only use is the module path of a `use` declaration (section 2.6).

### 2.4 Reserved keywords

```
tag  layers  grid  of  number
rule  sequence  params  where  when  use
any  all  ordered
once  scatter  everywhere  grow  settle
weight
```

Keywords are ASCII and match exactly.

```
RESERVED_CHARS ::= '[' | ']' | '{' | '}' | '(' | ')' | ',' | '='
                 | '*' | '.' | '"' | '%' | WS | NEWLINE
                 | <ASCII letters, digits, and '_'>
```
`?` is not reserved; it is held for future syntax and lexes as an error.

**Contextual names.** A few grammar terminals are matched by identifier text rather than reserved: the attribute names and values of section 5.6 (`symmetry`, `rotation`, `none`, `horizontal`, `vertical`). They lex as `IDENT` and may also be used as names, since each slot resolves them against its own table.

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

### 2.6 Modules

Every source file is a **module**. A module may begin with `use` declarations, each naming another module whose declarations it may reference:

```ls
// schema.ls
tag algo { R, W }
layers { level: grid of algo }

// carve.ls
use "schema.ls"
rule seed_room { level[.] => level[R] }
sequence carve {
    resize(16, 7)
    once seed_room
}

// dungeon.ls
use "carve.ls"
sequence main {
    once carve
}
```

`dungeon.ls` sees `seed_room` and `carve`, but not `level` or `algo`. Those are declared in `schema.ls`, which `dungeon.ls` does not use itself.

**Grammar:**
```
use_decl ::= 'use' STRING
```
`use` declarations precede all other declarations of a file (`source_file`, section 2). `STRING` is defined in section 2.3.

**Resolution.** The string is a module path. The embedder's resolver (Appendix A) maps it to a module, meaning a **canonical name** and its source text; the resolver receives the path and the canonical name of the using module. The tools resolve paths relative to the using file's directory, with `/` as the separator. Within one compile, a module is identified by its canonical name and loaded once: paths that resolve to the same canonical name denote the same module. A path the resolver cannot map is a compile error (section 7.3, check 9).

**The closure.** The module passed to the compiler is the **root**. The root and every module it uses, directly or through other modules, form its **closure**, which compiles to one generator (Appendix A). The `use` graph must be acyclic (check 40), and a module may use a given module only once (check 41). The closure's declarations are merged; at run time, nothing records which module declared what.

**Names.** Names are unique across the closure. Two tagsets, two grids, or two params with the same name are a compile error wherever they are declared (check 42); so are two rules or sequences (check 39). There are no qualified names (`schema.level`) and there is no selective `use`. Because a name denotes the same declaration in every module, `use` determines only which names a module may reference.

**Visibility.** A module **sees** its own declarations and the declarations of the modules it uses directly. A `use` is not re-exported: in the example above, `carve.ls` sees `schema.ls` and `dungeon.ls` sees `carve.ls`, but `dungeon.ls` does not see `schema.ls`. Every tagset, grid, param, rule, and sequence name a module writes must resolve to a declaration it sees (check 8). Tag values and tag unions need no visibility of their own; they resolve through the declared tagset of the grid they are written in (sections 3 and 5.8). A module's meaning therefore depends only on its own `use` declarations. It compiles identically as the root or as a dependency, and it can be compiled on its own.

**Canonical order.** The closure's modules are put in **canonical order** by a depth-first, post-order walk from the root. The walk follows each module's `use` declarations in written order and visits each module once, so every module comes after the modules it uses, and the root comes last (in the example: `schema.ls`, `carve.ls`, `dungeon.ls`). Declarations are ordered by module in canonical order, then by position in their file. Canonical order fixes the layer order (section 4), the param evaluation order (section 4.2), and the sequence ids of the embedding API (Appendix A). Everything a module sees from another module precedes it in this order.

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
tag_item_list  ::= tag_item (list_sep? tag_item)* list_sep?
tag_item       ::= tag_value | tag_union
tag_value      ::= IDENT
tag_union      ::= IDENT '=' union_expr
union_expr     ::= union_atom ('|' union_atom)*
union_atom     ::= IDENT
list_sep       ::= ','
```
Commas between list items are optional, and a trailing comma is permitted. Line breaks are ordinary whitespace (section 2), so items may share a line or each take their own. The grammar delimits items: each list item ends unambiguously - a pattern at `]`, a block at `}`, a pattern pair after its write side, and a name, union, or expression at the first token that cannot continue it.

A `tag_union` names a mask over the tagset's own members. Each `union_atom` is a `tag_value` of the same tagset, or a `tag_union` declared **earlier** in the same block (so unions may build on unions - `hazard = blocker | lava` - while remaining acyclic). A union references only members of its own tagset; there is no cross-tagset union.

```ls
tag geometry { wall, floor, door, blocker = wall | door }
```

**Encoding and cap.** A tagset holds at most **30** `tag_value`s (compile error if exceeded); these occupy bits 1..30 **in declaration order** - the first value declared is bit 1, the second bit 2, and so on. Bit 0 is reserved for **empty** (section 4.1), bit 31 is reserved. The bit assignment is normative: embedders may rely on it to compute a value's mask directly from its declaration position, without a name lookup. Each cell of a tag grid stores a 32-bit mask. A normal (single-valued) cell has exactly one value bit set; a cell may hold a multi-bit mask when written by a `|` expression or a named union (section 5.5).

A `tag_union` is a **named alias** for a mask over the tagset's value bits. It consumes **no** bit of its own and does **not** count toward the 30-value cap. A union name resolves, everywhere it may appear, to the bitwise OR of its members' masks - it is fully equivalent to writing that `|` expression inline. A union name shares the tagset's value namespace (redeclaring a value name is a duplicate error).

This encoding applies to **tag grids only**. `grid of number` cells store integers and are matched by value, not by mask.

---

## 4. Grids and the `layers` block

Grids are declared in `layers` blocks, at most one per module (section 2.6):

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

Grids have no declared size; size is set at run time via `resize`. All grids share the current size at all times.

**Layer order.** The grids of every module in the closure (section 2.6) form one stack. Its order, the **layer order**, follows canonical order: modules in canonical order, and within a module the order of its `layers` entries. The embedding API exposes layers in this order (Appendix A). Every grid of the closure is in the stack, whether or not a given module sees it; visibility governs only which grid names a module may write.

**Grammar:**
```
layers_decl      ::= 'layers' '{' layer_entry_list? '}'
layer_entry_list ::= layer_entry (list_sep? layer_entry)* list_sep?
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
param_entry_list ::= param_entry (list_sep? param_entry)* list_sep?
param_entry      ::= input_param | derived_param
input_param      ::= IDENT ':' 'number' '=' expr
derived_param    ::= IDENT '=' expr
```

Two kinds of param, distinguished solely by the `: number` annotation:

- An **input param** (`name: number = default`) may be supplied at runtime - via the embedding API's `generate(entry, seed, params)` / `begin(...)` or the CLI's `--param name=value` - alongside the seed. The `= expr` **default is required**: it is evaluated once at startup only when the runtime does not supply the param; a supplied value overrides it and the default expression is not evaluated. Because every input is defaulted, a run always has a complete configuration from the seed alone - **there is no "missing required param" runtime failure**.
- A **derived param** (`name = expr`) is computed once, at startup, when the input params bind. It is never settable from outside.

Both kinds carry an expression under the same scope discipline: literals, input params, and **earlier-declared** params only - no grid reads, no `x`/`y`, and no `width`/`height` (dimensions change during a run, so a startup-frozen dimension would be stale; read `width`/`height` directly in a cell expression instead). References resolve in declaration order; a forward reference or any cycle is a compile error. The expression may call `random` (section 5.10), drawing once at that single evaluation. Because both kinds are fixed once inputs bind, params are run-constant and determinism (section 7.2) holds.

At most one `params` block per module. Declaration order spans modules: the closure's params are ordered canonically (section 2.6), so a param expression may reference only params that are declared earlier **and** that its module sees. (A param of a directly used module is always earlier.) A param name (input or derived) must not collide with a grid name or a reserved identifier (section 2.4). Param values and expressions are numbers; a derived param whose expression is not a number is a compile error.

---

## 5. Rules

A rule is a match-write transformation, declared by name with `rule` or written inline in a statement (section 6). Its body declares one or more *match patterns* (left of `=>`) and one or more *write patterns* (right of `=>`).

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
pattern_pair_list ::= pattern_pair (list_sep? pattern_pair)* list_sep?
pattern_pair      ::= match_side '=>' write_side
```

### 5.2 Combinators (`any` / `all` / `ordered`)

**Combinator blocks are required when grouping more than one item at the same level; a single-item block is also legal.** A rule containing a single match-write pair with a single write pattern needs no combinator anywhere - but `{ all p }`, `{ any p }`, and a one-sub-rule combinator body all parse and run, with the obvious meaning. An **empty** combinator block (zero items) is a compile error.

> Single-item blocks run with zero special-casing, which matters for generated or heavily-edited sources where an alternative list shrinks to one entry. Semantics: `{ all p }` is identical to bare `p`; a single-item `{ any p }` always picks its one item but, having no special case, **still consumes its one PRNG draw** (section 6.7) like any other `{ any }` node.

A combinator block is needed for:

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

There is no default combinator: in any context that contains more than one item, `any`, `all` (or, at body level, `ordered`) must be stated explicitly. The match side is the exception, because it has only one meaning: consecutive patterns conjoin.

**Grammar** - match and write sides:
```
match_side      ::= pattern+
                  | '{' pattern+ '}'
write_side      ::= write_term
write_term      ::= pattern
                  | '{' 'all' all_item_list '}'
                  | '{' 'any' any_item_list '}'
all_item_list   ::= write_term (list_sep? write_term)* list_sep?
any_item_list   ::= any_item (list_sep? any_item)* list_sep?
any_item        ::= weight? write_term
combinator      ::= 'any' | 'all' | 'ordered'
weight          ::= '(' 'weight' '=' INTEGER ')'
```
All item lists are one-or-more. A match side is one or more patterns, optionally grouped in braces; a write side is a bare pattern or a braced combinator block:
- A **match side** conjoins its patterns across grids: every pattern before `=>` must match at the anchor. There is no match-side combinator; disjunctive matching would change the candidate model, read footprints, and conflict semantics, and is out of scope. Line breaks between the patterns are ordinary whitespace (section 2). Braces around a match side group it for the reader and change nothing: `{ level[floor] loot[.] } => loot[chest]` is `level[floor] loot[.] => loot[chest]`. A group holds one or more patterns, takes no combinator keyword, and does not nest.
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

**The `ordered` body-level combinator.** `ordered` is a **body-level** combinator only (not a write-side form). In `{ ordered s1 s2 ... }`, the sub-rules carry a **priority** in declaration order (s1 highest). `ordered` is purely an *ordering key on the candidate vector*: candidates are grouped by sub-rule priority (all of s1's, then s2's, ...) and shuffled only *within* each group. What that ordering *does* is inherited from the mode (section 6.7) that runs the statement - `ordered` is orthogonal to the mode, affecting only order:
- Under the **batch modes** (`scatter`, `everywhere`): pull in priority order under the write mask - s1 claims its cells first, s2 fills what's left, s3 last. Spatial priority / conflict resolution in one batch ("place big rooms; where you can't, small; else floor").
- Under the **step modes** (`once`, `grow`): re-collect each step and take the highest-priority available match - s1 keeps firing while it can; when a lower rule fires and reopens an s1 match, the next step prefers s1 again. This is **preemptive priority** (the ordered-rule / MarkovJunior loop), the behaviour statement-sequencing cannot express.
- Under **`settle`**: priority-ordered sweeps to fixpoint.

`any` stays a weighted random pick (order carries no meaning); `all` collects every matching sub-rule's candidates together (uniform priority). Because `ordered` is body-level only, using it as a write-side combinator is a compile error (section 7.3, check 29).

### 5.3 Pattern bodies

A pattern body is a rectangular grid of cells enclosed in `[ ]`. Whitespace between cells is required; line breaks within `[ ... ]` denote row breaks.

```ls
algo[
    * * *
    * S W
    * * * ]
```

The above is a 3x3 pattern. The center cell must be `S`, the right-center must be `W`, and the eight cells marked `*` match anything (including empty).

Single-cell patterns are written inline: `algo[S]`. Rows may also be separated by commas, so a tall pattern can be written on one line: `algo[* * *, * S W, * * *]` is the 3x3 pattern above, and `g[a, b, c]` is a 1x3 vertical one. A comma followed by a newline (with only whitespace between) is a single row break, so rows may end in commas across lines without creating empty rows.

Multi-row pattern grammar:
- Empty rows are illegal.
- Trailing whitespace on a row is ignored.
- The closing `]` may be on its own line or at the end of the last row.
- Inconsistent column counts across rows are a compile error.

**Grammar:**
```
pattern      ::= pattern_head '[' cell_grid ']'
pattern_head ::= IDENT | 'where'
cell_grid    ::= cell_row (row_sep cell_row)*
row_sep      ::= ',' WS* NEWLINE? | NEWLINE
cell_row     ::= WS* cell (WS+ cell)* WS*
cell         ::= '*' | '.' | INTEGER | tag_mask | '(' expr ')'
tag_mask     ::= mask_atom ('|' mask_atom)*
mask_atom    ::= '!'? IDENT
```
A `tag_mask` is whitespace-free (the cell ends at the next whitespace or comma). A comma inside `( ... )` belongs to the expression; only a comma outside parentheses separates rows. `expr` inside `( ... )` follows section 5.8 and may contain whitespace. `tag_mask` and the bare-`*`/`.` forms are exactly the mask literals; every other expression must be parenthesized.

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

A `where` cell matches at a position iff its expression evaluates to true there. `where` cells read but never write - they contribute to the read footprint (section 5.7) and never to the write footprint. A `where` pattern conjoins with real-grid patterns on the match side (section 5.2):

```ls
rule deep_water {
    level[floor]
    where[ (tiles > 5) ]
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

**Built-in names are not reserved.** A call is recognised syntactically (an identifier followed by `(`, section 5.8), and tag values, grids, and params are never called, so `if`, `min`, `max`, `abs`, `clamp`, and `random` may also be used as tag value, grid, or param names. Followed by `(`, such a name always denotes the built-in; otherwise it resolves per section 5.8. Like operation names (section 2.4), built-in names are not keywords.

**`random` is impure; the others are pure.** Because `if` is eager, `if(c, random(0,9), random(0,9))` evaluates **both** branches and therefore draws **twice**, in left-to-right order, discarding the unused value. This is defined behaviour, not a fault; if exactly one draw is wanted, place the `random` outside the `if`. (Eager `if` is deliberate: totality makes the dead branch safe, and the fixed two-draw cost keeps the draw sequence trivially pinned.) `random` may appear in any expression position (match cells, `where`, write cells, `when` guards, param expressions): it reads no grid, position, or dimension, so the config-scope restrictions of sections 4.2/6 do not exclude it. It draws once each time its expression is evaluated.

---

## 6. Statements and entries

Generation is orchestrated by **statements** (operation calls, rule applications, and sequence applications), written in the bodies of sequences (section 6.10). A run applies one sequence, the **entry**, to an empty stack:

```ls
sequence main {
    resize(60, 40)
    scatter(5)   start
    grow(100)    rwalk
    upscale(2, 2)
    everywhere   reduce
    everywhere   reward
    everywhere   fill_geo
    scatter(10)  place_enemies
    everywhere   decorate
}
```

Statements execute top to bottom.

**Entries.** The embedder names the entry when it starts a run (Appendix A). Any sequence can be the entry; the language designates none. A run first binds the params (section 4.2), then applies the entry exactly as `once S` (section 6.10) to a 0 x 0 stack: one iteration of its body. Applying the entry involves no guard, no draw, and no stability check. A rule is not an entry; to run a single rule, apply it from a sequence.

The tools (the `levelscript` CLI and `levelscript-debugger`) run the sequence named `main` unless told otherwise. That is a tool convention, not part of the language: `main` is an ordinary sequence name, and the embedding API always takes an explicit entry.

**Grammar:**
```
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
apply_stmt      ::= mode target
mode            ::= 'once'
                  | 'scatter' '(' scatter_count ')'
                  | 'everywhere'
                  | 'grow'   ( '(' expr ')' )?
                  | 'settle' ( '(' expr ')' )?
target          ::= IDENT | inline_rule
inline_rule     ::= rule_attrs? '{' rule_body '}'
scatter_count   ::= expr | percent
percent         ::= (INTEGER | IDENT | '(' expr ')') '%'
```
A statement is either an **operation call** (`op_call` - a built-in operation applied to the grid stack, section 6.0) or an **application** (`apply_stmt` - a mode keyword, with its count where the mode takes one, and a target). In an `op_call`, arguments are **positional first, then named**; the operation name and its arguments resolve **semantically** against the operation table (section 6.0), not by the grammar - there are no per-verb productions and no verb keywords. Newlines are permitted inside an argument list. A target is either a name, which must reference a rule or sequence its module sees (sections 2.6 and 6.10; semantic check; forward references are fine), or an **inline rule**, which begins with `{` or with its attributes. After `grow` or `settle`, a `(` followed by `IDENT '='` begins inline-rule attributes rather than a count. A guard (`when`) may follow any statement.

**Modes.** The mode fixes how many applications occur and what each one sees (section 6.7 for rules, section 6.10 for sequences):

| mode | over a rule | over a sequence |
|---|---|---|
| `once` | one application (a single uniform pick) | one iteration |
| `scatter(N)`, `scatter(P%)` | up to `N` applications from one batch; with `P%`, that percentage of the batch | invalid |
| `everywhere` | every non-conflicting application of one batch | invalid |
| `grow`, `grow(N)` | up to `N` steps, each seeing the last; without a count, until no candidate remains (fixpoint) | invalid |
| `settle`, `settle(N)` | batch sweeps until one changes nothing; with a count, at most `N` sweeps | iterations until one is stable; with a count, at most `N` |

A rule-only mode on a sequence is an error (check 37). Over a rule the modes fall into two kinds, plus `settle`: the **batch modes** (`scatter`, `everywhere`) apply from one snapshot in one shuffled pass under the write mask; the **step modes** (`once`, `grow`) re-snapshot after every application and use no mask; `settle` repeats batch sweeps until nothing changes.

**Counts.** A count is a statement-scope expression (section 5.8). Like a guard, it reads params only: a grid read, `x`/`y`, or `width`/`height` in a count is a compile error, as is a count that is not a number (check 21). It is evaluated each time its statement is reached, after the guard and only if the guard passed; a `random` in it draws on every evaluation. A count written as the literal `0` is an error (check 28), as is a `scatter` percentage written as a literal above `100` (check 30). A computed count below `0` is treated as `0`, so the statement applies nothing (over a sequence, it runs no iteration); a computed percentage above `100` is treated as `100`. Neither warns. The `%` follows a literal, a param name, or a parenthesized expression (`50%`, `density%`, `(n * 2)%`), so it always applies to the whole count; `n * 2%` is a parse error. It is part of `scatter`'s count, not of the expression: it selects a percentage of the batch (section 6.7) and is not a value.

**Inline rules.** A target may be a rule written in place, with the grammar of a named rule minus `rule IDENT` (section 5.1): optional attributes, then a braced body. An inline rule behaves exactly as a named rule with the same attributes and body, declared in the enclosing sequence's module and applied by name; its names resolve in that module. To apply one rule from several places, declare it with `rule`. Diagnostics and the debugger locate an inline rule by its source position (section 10.8); the observe channel identifies statements by position, not by rule name (Appendix A).

Reads:
```ls
everywhere fill                           // one batch, every non-conflicting match
settle smooth                             // CA: re-sweep until nothing changes
grow(200) walk                            // drunkard's walk, each step sees the last
scatter(50%) carve                        // half of one batch
scatter(budget / 10) place_enemies        // count computed from params
everywhere { algo[F|S] => level[floor] }  // inline rule
path(from=door, to=exit, into=site, write=road)  // structural op: carve a route
```

A `guard` may follow any statement. Its expression (section 5.8) is **boolean** and reads **params only** (input or derived); a grid read, `x`/`y`, or `width`/`height` in a `when` guard is a compile error (there is no candidate position or committed size at statement scope). The guard is evaluated **once**, when the statement is reached. If it is false, the statement is skipped in full - no operation, no rule application, no collection pass. If true, the statement runs exactly as it would unguarded; the guard gates *whether* the statement runs, never *which cells* within it (that is `where`, section 5.9). A guard on an operation call gates that operation the same way.

```ls
sequence main {
    resize(60, 40)
    scatter(5)   start
    everywhere   place_bosses  when (difficulty > 3)
    everywhere   cave_pass     when (style == 0)
    everywhere   room_pass     when (style == 1)
}
```
Selection among rules is expressed by complementary guards, as with `style` above. There is no `if`/`else` block; statements stay a flat, top-to-bottom list. A sequence (section 6.10) does not change this: it is a named statement list applied by name, like a rule, not a nested block.

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
sequence main {
    resize(60, 40)
    once start
    grow(100) rwalk
    trim()
}
```

Semantics:

- Compute the union bounding box of all cells that are non-empty *in any layer*. A cell counts as non-empty if any grid of the stack holds a value (not `.`) at that position.
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

The origin-side half (left / top) is the source and is never modified; the far half is overwritten with the reflection. Operating per (grid, cell) across all layers keeps co-registered layers aligned. `mirror` takes one axis; there is no `mirror(both)` (compose horizontal then vertical for quadrant symmetry) and no per-axis value (asymmetric/partial mirroring is out of scope - keep the grid small instead). `mirror` is the statement-level counterpart of the `symmetry` rule attribute (section 5.6.1) and reuses its axis vocabulary.

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

**Predicate resolution.** A **bare tag** (or named union) as a `pred` reads **the unique tag layer whose tagset declares that name**, among the grids its module sees (section 2.6), with pattern-cell overlap semantics (`(stored & mask) != 0`, section 4.1); if no seen layer's tagset declares it, or **more than one** seen layer could hold it, that is a compile error (section 7.3, check 35) - use the expression form and name the grid (e.g. `passable=(level == floor)`). An expression `pred` is any boolean expression over same-position reads, `x`/`y`/`width`/`height`, and params. The **default `passable`** - "any non-empty cell" - means non-empty in **at least one layer**, the same union-of-layers content notion `trim` uses (section 6.3). Note that expressions have no mask-overlap operator, so the expr form tests exact `==` while the bare-tag form is pattern-style any-overlap.

**The draw contract.** `path` pins its evaluation and PRNG behaviour exactly, so that the output is a pure function of (seed, params) regardless of how the search is implemented:

1. The `from`, `to`, and `passable` predicates each evaluate in **one row-major pass** over the current extents, **in that order** (`from` first, then `to`, then `passable`). A `random` inside a predicate draws in that pass order.
2. `cost` evaluates **once per traversable cell**, row-major, clamped to >= 1; the search runs on those fixed weights. A `random(...)` cost draws once per traversable cell, reproducibly.
3. Tie-breaking consumes **exactly one stream draw per `path` statement**. Per-cell tie keys are derived from that single draw by hashing it with the cell index (splitmix64) - there are **no per-discovery draws**, so the number of draws a `path` statement consumes never depends on grid content or search order.
4. The route is a **pure function of the distance field** the fixed weights induce. The nearest goal is the reachable `to` cell minimizing (distance, tie key) lexicographically. The route is derived by walking backward from that goal, at each step choosing the minimum-tie-key predecessor among those on an optimal (shortest) path. The route is then stamped **start -> goal**: if `write` is an expression, it evaluates once per route cell in that order.

Consequence, stated normatively: **the search algorithm is unobservable.** Any correct shortest-path implementation - BFS at uniform cost, Dijkstra, A* with an admissible heuristic - produces the identical distance field and hence the identical route, draw count, and output.

### 6.7 Rule application semantics

An application over a rule pairs a **mode** (section 6) with the rule. The mode bounds how many applications occur and governs what each application sees and how conflicts are handled. (An application over a sequence takes `once` or `settle` only; see section 6.10.)

All modes share **candidate collection**: for each anchor position in row-major `(y, x)` order, the rule's match side is tested - every sub-rule and every symmetry/rotation variant (section 5.6) - at that anchor. Each matching `(anchor, sub-rule, variant)` is a **candidate**. The anchor is the pattern's top-left origin (section 5.7); a candidate exists only where the (possibly reshaped) variant fits entirely within the grid - **matching is bounded, never wrapped**.

**Batch modes** (`scatter`, `everywhere`) - one frozen snapshot, one pass:
1. Snapshot the grid stack (section 7.1); collect all candidates against it.
2. Shuffle the candidates (seeded). Empty the write-protection mask (section 6.8).
3. In shuffled order, for each candidate: if its **write** footprint intersects a marked (grid, cell), skip; else apply the write and mark its write footprint. Count one application.
4. Stop at the cap (`scatter(N)` = N; `everywhere` = the whole set). For `scatter(P%)`, the cap is floor(P * A / 100) where **A is the number of applications a full pass would make** - the non-conflicting applied set, *not* the raw candidate count (raw candidates would let redundant variants and skipped conflicts inflate the denominator, so `50%` could cover far more than half the grid). Concretely: the pass first determines the non-conflicting sequence the batch would apply, then applies its first floor(P * A / 100) entries.

Because all candidates saw one snapshot, earlier writes are invisible to later applications; the mask alone prevents two applications writing the same cell. Only `scatter` takes a percentage, because a percentage needs one batch's applied set as its denominator.

**Step modes** (`once`, `grow`) - each application re-snapshots and therefore sees prior writes:
1. Snapshot; collect; pick one candidate uniformly (a single draw; under `ordered`, uniformly within the highest-priority group, section 10.4). Apply and commit; the `{ any }` alternatives on its write tree are drawn at application, weighted per section 5.5.
2. Repeat. Each step re-snapshots, so the next pick sees the last write.

One application per step means no intra-step conflict, so **step modes use no mask**. `once` stops after one application and `grow(N)` after N; `grow` without a count runs until a step finds no candidate (**fixpoint**, section 6.9). Over a rule, `once` and `grow(1)` make the same application.

**`settle`** - iterated batch sweeps to a fixpoint:
1. Run a full `everywhere` sweep (steps 1-4 above, applying the whole set).
2. If that sweep changed any cell, repeat from a fresh snapshot; else stop.

Each sweep is internally a batch (mask per sweep); between sweeps the grid re-snapshots, so a later sweep sees the previous sweep's result. This is the cellular-automaton case (smooth until stable). For `settle` the count unit is a **sweep**: `settle` = sweeps to fixpoint; `settle(N)` = at most N sweeps, stopping early after a sweep that changes nothing.

**Variants and alternatives.** Each matching variant is a **separate candidate**, so shuffling treats variants with equal priority - no top-left/first-declared bias; when several variants match one anchor, the one applied is whichever the shuffle selects. At application the runtime walks the resolved write tree and draws once per `{ any }` node on the resolved path (section 5.2), each weighted per section 5.5 and reproducible per seed (section 10.6).

All random choices are deterministic per (seed, params) and per implementation version (section 7.2).

### 6.8 The write-protection mask

The mask exists only in **batch passes** (`scatter`, `everywhere`, and each sweep of `settle`). The step modes (`once`, `grow`) apply one write per step and need no mask.

- The mask is keyed by **(grid, cell)** - a per-layer set, not a single grid-sized mask. Marking `level` at (x, y) does not protect `items` at (x, y).
- It tracks **write footprints only**. A candidate is skipped iff its **write** footprint intersects a cell already marked this pass. **Read footprints do not participate** - under snapshot semantics (section 7.1) a candidate reading a just-written cell already sees the pre-write snapshot value, so there is nothing to protect against, and skipping on reads would wrongly prevent a synchronous CA sweep (every cell that reads a neighbour's written value would be suppressed).
- Skipped candidates do not consume the count budget.
- Footprint intersection is per (grid, cell), never per bounding rectangle: two candidates whose rectangles overlap both apply if the overlap is entirely `*`-preserved (not in either write footprint) in the same grid.

The mask is the only conflict mechanism; the runtime special-cases no other "conflict." It is reset at the start of each batch pass (and each `settle` sweep).

### 6.9 Termination

**Bounded modes over a rule always terminate** (for sequences, see below): `once`, `scatter`, and `everywhere` (a single pass over a finite candidate set), `grow(N)`, and `settle(N)`.

**Fixpoint modes run until nothing changes:** `grow` and `settle` without a count. These terminate **iff the rule is reductive** - iff repeated application cannot keep producing new matches. A reductive rule (fills empties, removes tags, narrows values) reaches a fixpoint; a generative one (a rule whose write recreates its own match) may not, and a fixpoint mode will then loop forever.

The language does not verify reductivity in general - a fixpoint mode is a loop with a computed exit, like `while (...)`: the construct is provided, and not writing a divergent rule is the author's responsibility. This is a deliberate, scoped weakening of universal termination: bounded modes keep it; fixpoint modes trade it for expressive iteration.

**The reductivity warning.** The compiler does, however, flag the *guaranteed*-divergent case. For a `grow` statement without a count, consider each sub-rule's **unconditional** writes - the write leaves that occur on every resolution of the write tree; leaves under an `{ any }` do **not** count, since the pick may avoid them. If some sub-rule has no unconditional write that **invalidates its own LHS** - i.e. no write that overwrites an LHS-constrained cell of the same grid with a value that no longer matches that cell's requirement - then an applied anchor re-matches forever and the fixpoint is unreachable: the compiler emits the warning *"'grow' over rule '<name>' may never terminate: a sub-rule's write leaves its own match intact, so the fixpoint is unreachable."* For an inline rule, `<name>` is its source position.

The analysis is conservative in both directions:
- **Uncertainty suppresses the warning.** A computed (expression) match or write cell, and any `where` guard, make the invalidation question undecidable at compile time; such a sub-rule is assumed to invalidate and produces no warning. Only the statically certain case warns.
- **Silence proves nothing.** Passing the check does not prove termination; a rule can still diverge through interactions the per-sub-rule check cannot see.

`settle` is **exempt**: its loop exits on a no-change sweep, so an idempotent write (one that re-matches but rewrites the same value) still reaches the fixpoint - the guaranteed-loop argument does not apply.

The warning is a compile-time **warning**, not an error (section 7.4); it is surfaced to embedders via `generator::warnings()` (Appendix A) and to CLI users on stderr.

**Sequences** (section 6.10). `once S` and `settle(N) S` bound the number of **iterations**; each iteration terminates iff its body statements do, so a fixpoint statement inside the body can still diverge. `settle S` is a fixpoint mode: it terminates iff some iteration is stable. As with rules, that is the author's responsibility; the compiler flags only the case that is certain to diverge by changing dimensions (section 6.10).

### 6.10 Sequences

A **sequence** is a named, reusable statement list, applied by name like a rule.

```ls
sequence smooth {
    everywhere erode
    everywhere dilate
}

sequence main {
    resize(40, 25)
    everywhere fill
    settle(4) smooth    // up to 4 iterations of the body
    settle smooth       // iterate until an iteration changes nothing
}
```

**Grammar:**
```
sequence_decl ::= 'sequence' IDENT '{' statement_list '}'
```
The body is a `statement_list` (section 6): rule applications, sequence applications, and operation calls, each with an optional `when` guard. Rules may also be written inline in a statement (section 6). Rules and sequences share one namespace; an `apply_stmt`'s target name resolves to either one (section 7.3, check 39). A sequence may apply other sequences. A sequence that applies itself, directly or through others, is a compile error (check 38). Declaration order does not matter, and an unapplied sequence is legal, like an unused rule. An empty body is legal; every iteration of it is stable.

**Iteration.** Applying a sequence runs its body from top to bottom; one run of the body is an **iteration**. Each body statement executes as sections 6.0-6.9 specify, with its own mode, count, and guard, and sees every prior write. A sequence adds no snapshot, write mask, or pass of its own.

**Stability.** An iteration is **stable** iff the grid stack at its end equals the stack at its start: the same dimensions, and every cell of every layer holding the same value. Stability compares states, not writes. Writing an unchanged value does not count as a change, and neither does a cell changed and changed back within the iteration. Stability is judged on the iteration just run. A stable iteration ends the statement even if a further iteration could have changed the stack through different draws (`random`, `{ any }`, `path` ties); the same holds for `settle` sweeps over a rule (section 6.7).

**Modes.** A sequence application takes `once` or `settle`; the count unit is one iteration:

| statement | runs |
|---|---|
| `once S` | exactly one iteration |
| `settle(N) S` | up to N iterations; stops early after a stable iteration |
| `settle S` | iterations until one is stable (**fixpoint**, section 6.9) |

`settle` means the same over a sequence as over a rule (section 6.7): repeat until a repetition changes nothing, with `N` as an upper bound. `scatter`, `everywhere`, and `grow` are not valid on a sequence (check 37): they select among one rule's candidates, and a sequence has no candidates of its own. For a fraction of each inner batch, write `scatter(P%)` on the inner statements.

**Guards.** A `when` on a sequence application gates the whole application. It is evaluated once, when the statement is reached (section 6). A `when` on a statement inside the body is evaluated each time that statement is reached, which is once per iteration. Guards read params only, so the value is constant unless the guard calls `random`, which draws on every evaluation. Counts (section 6) are evaluated the same way.

**Draw order.** A sequence application has no draw sites of its own, and the stability check draws nothing. Body statements draw per section 10.6 as each one is reached, iteration after iteration.

**The dimension warning.** For `settle S` without a count, the compiler warns when no iteration can be stable because every iteration changes the grid dimensions. This happens when S's body contains an `upscale` whose factors are not both 1, or a `pad` whose margin is greater than 0, and that operation is **reached on every iteration**. That requires two things: neither the operation nor any enclosing nested sequence application carries a `when` guard, and every enclosing nested application runs at least one iteration, which holds for `once`, for `settle` without a count, and for a count written as a literal (a literal `0` is an error, check 28), but not for a computed count, which may be zero. The warning reads: *"'settle' over sequence '<name>' may never terminate: '<operation>' changes the grid dimensions on every iteration, so no iteration can be stable."*

Like the reductivity warning, this check is conservative. A guard suppresses the warning, because it may be false. Silence proves nothing, because a rule in the body can still keep recreating its own match. Bounded applications (`once`, `settle(N)`) are never warned: `settle(3) grow_and_upscale` is legitimate intended growth.

---

## 7. Execution model

### 7.1 Snapshot semantics

Matching is always evaluated against a **snapshot** of the grid, never against a partially-written live grid. The unit that shares a snapshot is the **pass**, and passes differ by mode: a batch mode (`scatter`, `everywhere`) is one pass (every candidate sees statement-entry state); `settle` is a sequence of passes (each sweep re-snapshots, seeing the prior sweep); a step mode (`once`, `grow`) re-snapshots every application (each sees all prior writes). Within any single pass, all candidates see the same input and earlier applications' writes are invisible; the write-protection mask (section 6.8) prevents conflicting writes within a batch pass. At end of statement the live grid becomes the next statement's input.

Implementations may realize a pass's snapshot as a full copy, copy-on-write per touched cell, or any equivalent mechanism (section 10.3); only the visibility rules above are observable.

The snapshot mechanism is independent of the write-protection mask (section 6.8). The snapshot guarantees match consistency *across* candidates within a pass; the mask (used by batch passes) prevents conflicting *writes* among the candidates that have already succeeded.

### 7.2 Determinism

A LevelScript run's output is a pure function of **(entry, seed, params, implementation version)**. Entry, seed, and params are part of the runtime invocation, not the source; given the same entry, seed, and parameter values (section 4.2), an implementation version produces the same output, bit for bit, **on every platform and compiler**. A seed therefore names one level everywhere the version runs, which is what curated seed pools rely on.

The mechanism is a **single sequential PRNG stream** - `mt19937_64` as specified by the C++ standard, seeded with the 64-bit invocation seed - with **pinned draw primitives** (section 10.6: every mapping from the stream to a decision is integer arithmetic defined in this document, never a standard-library distribution or shuffle, whose algorithms differ between libraries) and a **pinned draw order**: every stochastic decision - candidate shuffling, weighted `{ any }` selection, `random(...)` calls, `path` tie keys - consumes draws from that one stream at points fixed by the execution model (section 10.6). There is **no position-keyed PRNG**: one stream is deliberately kept for simplicity, accepting the relaxed cross-version claim below (a position-keyed PRNG would decouple draws from evaluation order and permit parallel matching; it remains a possible future change).

Consequences:
- Param expressions are evaluated **once**, at startup, so each param is constant for the run. A `when` guard or a count is evaluated each time its statement is reached, so it is constant unless it calls `random`, which draws on every evaluation, reproducibly.
- The source of a generator is its whole closure (section 2.6). Reordering `use` declarations can change canonical order, and with it the layer order and the param evaluation order; when a param expression calls `random`, that shifts the draw sequence.
- Changing an input param may shift the draw sequence and cascade through the rest of the run - expected in PCG, not a determinism defect (a small input change is not expected to produce a small output change).
- Determinism is **per implementation version**, not per build: every build of a version agrees, on every platform. A future version may change evaluation/scan/shuffle order and remain deterministic; cross-version (and cross-implementation) reproducibility of specific outputs is not promised. What *is* promised across versions is the semantics of this document, not the byte-identical artifact of a given seed.

### 7.3 Compile-time checks

The compiler must reject:

1. **Pattern bodies with inconsistent row widths.** All rows must have the same number of cells.
2. **Empty pattern bodies.** A pattern body with zero cells is a syntax error; with zero meaningful rows it is a semantic error.
3. **Shape mismatch.** The match and write sides of a `pattern_pair` must have identical dimensions, recursively through nested write blocks (section 5.4).
4. **Empty combinator block.** A combinator block (`{ all }`, `{ any }`, or a combinator rule body) with zero items. (Single-item blocks are legal, section 5.2.)
5. **Weight scope.** `(weight=N)` is rejected inside `{ all ... }` blocks and on bare patterns (parse error).
6. **Tag/grid resolution.** Every identifier in a pattern cell must resolve per section 5.8 (for a bare mask literal: a tag value or union of the grid's declared tagset).
7. **Attribute validation.** Attribute names and values must be in the table of section 5.6.
8. **Reference resolution.** Every tagset, grid, param, rule, and sequence name must resolve to a declaration the referencing module sees (section 2.6). A name declared in the closure but not seen is reported as not visible, naming its declaring module. Rules and sequences may be referenced before they are declared; declaration order does not matter for them.
9. **Unresolved module.** A `use` path that the resolver cannot map to a module (section 2.6, Appendix A).
10. **Tagset over cap.** A tagset with more than 30 values.
11. **`where` on RHS.** A `where` pattern on the write side.
12. **Reserved-name collision.** A tag value, grid, or param named `x`, `y`, `width`, or `height`; or a param colliding with a grid name.
13. **Bare computed expression.** A computed expression (comparison/arithmetic/logical/call) in a cell without enclosing `( ... )`.
14. **Expression type errors.** An operator applied to the wrong value kind; an expression in a real cell not evaluating to the grid's type; comparing across distinct tagsets; a `.` with no inferable type.
15. **Complement write.** `!`tag used on the RHS (complement is match-side only).
16. **Unknown function.** A call to a name that is not a section 5.10 built-in.
17. **Duplicate params block.** More than one `params` block per module.
18. **Invalid transform value.** A `rotation=` value that is not `none`/`all`, a bare `90`/`180`/`270`, or a set `{...}` of those angles; a `symmetry=` value other than `none`/`horizontal`/`vertical`/`all`; or an angle set containing a value other than 90/180/270. (An empty set `{}` and a `symmetry` set are also errors.)
19. **Function arity.** A built-in call with the wrong number of arguments.
20. **Function argument type.** A built-in argument of the wrong kind (e.g. `abs` of a tag); or `if` whose two branches differ in type; or `if` whose condition is not bool.
21. **Count scope.** A count (section 6) that reads a grid, `x`/`y`, or `width`/`height`; or whose expression is not a `number`.
22. **Guard scope.** A `when` guard that reads a grid, `x`/`y`, or `width`/`height`; or whose expression is not boolean.
23. **Derived param scope.** A derived param expression that reads a grid, `x`/`y`, or `width`/`height`; or that forward-references a later param; or that participates in a cycle.
24. **Derived param type.** A derived param whose expression is not a `number`.
25. **Invalid union.** A `tag_union` whose member is not a `tag_value` or earlier `tag_union` of the same tagset; a member that forward-references a later union; or a union cycle. (A union name colliding with a `tag_value` name, a grid, a param, a reserved identifier, or a built-in is caught by the existing duplicate/collision checks - a union shares the tagset's value namespace.)
26. **Default param scope.** A default expression (section 4.2) that reads a grid, `x`/`y`, or `width`/`height`; that forward-references a later param; or that participates in a cycle. (Same discipline as check 23; a default may reference earlier params and may call `random`.)
27. **Input param without a default.** An `input_param` written as `name : number` with no `= expr` (every input param must carry a default, section 4.2).
28. **Zero count.** A count written as the literal `0` (or `0%`), which applies nothing.
29. **`ordered` misplacement.** `ordered` used as a write-side combinator (it is body-level only).
30. **Percentage over 100.** A `scatter` percentage written as a literal above `100`.
31. **Same-grid simultaneous write.** Within one `{ all }` write block, two items whose write footprints overlap on the **same grid** at the same cell (e.g. `{ all g[wall] g[floor] }`). Simultaneous writes to one cell are ambiguous; use a union `|` (section 5.5) to store multiple values. (Different grids at the same position are fine - the mask is (grid, cell)-keyed, section 6.8.)
32. **Unknown operation.** An `op_call` (section 6.0) whose name is not in the operation table.
33. **Operation argument arity/binding.** More positional arguments than the operation's positional parameters; a named-only parameter supplied positionally; a named argument preceding a positional one; or a parameter supplied both positionally and by name.
34. **Missing required operation argument.** A required parameter not supplied.
35. **Operation argument kind / enum.** An argument whose kind does not match the parameter (e.g. `grid` given a non-grid); an `enum` value outside its set (`mirror` axis not in {horizontal, vertical}; `connectivity` not in {4, 8}); or an ambiguous bare-tag `pred` (no layer the module sees, or more than one, could hold the tag - section 6.6).
36. **Operation value constraint.** `resize`/`upscale` dimensions must be positive; a `pad` margin must be non-negative. (Value checks applied after kind resolution.)
37. **Mode on a sequence.** `scatter`, `everywhere`, or `grow` applied to a sequence (section 6.10).
38. **Sequence cycle.** A sequence that applies itself, directly or through other sequences.
39. **Duplicate rule or sequence name.** Rules and sequences share one namespace: two declarations with the same name anywhere in the closure (section 2.6), whether both are rules, both are sequences, or one of each.
40. **Module cycle.** A module that uses itself, directly or through other modules (section 2.6).
41. **Duplicate use.** A module with two `use` declarations that resolve to the same canonical name.
42. **Duplicate tagset, grid, or param name.** Two tagsets, two grids, or two params with the same name anywhere in the closure, in one module or in different ones (section 2.6).
43. **Duplicate layers block.** More than one `layers` block in a module.

### 7.4 Warnings

Warnings never stop compilation or execution; the runtime degrades with a warning and continues, never faults.

**Compile-time warnings** (surfaced via `generator::warnings()`, Appendix A):

1. **Reductivity.** `grow` without a count over a rule where no unconditional write invalidates its own LHS - "may never terminate" (section 6.9). Not an error: the analysis is conservative, and a build pipeline may still choose to treat warnings as failures.
2. **Unreachable sequence fixpoint.** `settle` without a count over a sequence where every iteration is certain to change the grid dimensions, through an `upscale` or `pad` reached on every iteration (section 6.10).

**Runtime warnings** (logged; the statement becomes a no-op):

- `trim()` on an entirely empty stack (section 6.3).
- `path(...)` with no reachable route, or an empty `from`/`to` set (section 6.6); `path` before any `resize` (zero-sized stack).

---

## 8. Worked example

A complete fill-and-carve dungeon generator (see `examples/` for this and smaller single-feature examples):

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
```

Reading `main`: the first inline rule fills `algo` with `W`, and the second seeds up to five `S` markers into it in one batch; `grow(100)` runs `rwalk` as a drunkard's walk from them, carving `F` through the `W` fill, each step seeing the last (the `rotation=all` variants walk in all four directions); `upscale` doubles the resolution; `reduce` erodes 2x2 seed blocks; then batches convert the `algo` sketch into rewards, geometry, enemies, and tile indices. One-line rules and plain fill passes are written inline; rules with attributes (`rwalk`, `reduce`) or with alternatives (`reward`, `place_enemies`) are named.

A structural-operation companion (`examples/corridor.ls`): scatter a numeric cost field, place a door and an exit with `where`-pinned rules, then

```ls
path(from=door, to=exit, into=site, write=road,
     passable=((0 == 0)), cost=(1 + rocks))
```

carves the cheapest corridor; equal-cost routes vary by seed, reproducibly (section 6.6).

---

## 9. Future directions

Deferred, in rough priority order:

**Graphs.** The reason for the language's forward-looking design (and its name making room for more than grids): graph layers alongside grid layers, with rewrite semantics over nodes/edges and integration with grid-derived graphs. `path`'s `over=` parameter (section 6.6) is the prepared seam - a graph reference there routes over its edges, the same operation with one swapped input.

**The structural-operation family.** `path` is the first of a family conforming to the section 6.0 envelope (predicate/grid arguments in, result written into a grid, seeded, table-resolved): BSP subdivision, flood-fill regions, MST/room-linking, Voronoi, a full distance field. The graph-generalizable members - `path`, MST, distance - share the `over` seam; BSP and Voronoi are grid-native. Each is a table row plus an executor; none needs a grammar change.

**Templates.** Named constant grids stamped by rules (`template Vault3x3 { ... }`). Modules (section 2.6) are their distribution mechanism: a Spelunky-style chunk library is a module of templates.

**Applicability-driven sequences.** The planned answer to data-dependent `if`, in the MarkovJunior style: a statement **succeeds** if it made any change, and an `ordered` sequence runs its first item that succeeds, then restarts from the top. This gives `else if` over grid state with no condition expressions and no aggregate queries (those belong to graph layers). Parameter-driven branching is already served by `when` guards on sequence applications. Parked.

**Staged runs.** `run(level, entry)`: apply an entry to an existing level instead of an empty stack, so a host can generate in stages and inspect the level between them (carve, assess, then decorate or regenerate). Open: whether a level carries its run state (PRNG stream and bound params), which would make staged runs bit-identical to one sequence applying the stages in order, or whether each stage takes a fresh seed. Parked.

**Position-keyed PRNG.** Rejected for 0.5 (section 7.2) but recorded: hashing (seed, x, y) instead of drawing from one stream would decouple match-side `random` from evaluation order and restore parallel matching (section 10.4). It would change every seeded output; if ever adopted, it is a major-version change.

**Rule attribute extensibility.** `symmetry` and `rotation` are the only attributes specified; the parenthesized attribute syntax accommodates future ones (e.g. `probability`) without grammar changes.

---

## 10. Implementation guidance

This section is non-normative; it records how the reference implementation is built and which choices are load-bearing for the semantics above.

### 10.1 Architecture

- **One execution core.** The interpreter is a single C++20 coroutine that yields step events at application and statement boundaries. Batch generation (`generator::generate`), progressive generation (`generation::step`, with granularity a filter on the event puller), and any debug/visualization UI all pull the same coroutine. There is never a second interpreter to drift from the first.
- **The compiled artifact is self-contained.** Semantic analysis copies everything the runtime and the embedding API need for the whole module closure (section 2.6) - names, tables, compiled rules, the operation schedule - into one immutable object shared by reference counting; the AST is discarded after analysis. Generated levels are likewise self-contained values that outlive their generator (Appendix A).
- **Ids over strings.** Grids, tag values, and rules are integer indices into vectors; names resolve once at the API boundary. Conflict masks are hashed sets keyed by a packed (grid id, flat cell index).
- **Tables over keyword grammar.** Operations (section 6.0) and expression built-ins (section 5.10) resolve by name against schema tables; each layer has one generic call production (`IDENT '(' arg_list? ')'` in expressions, `op_call` in statements), and adding an entry is a table row plus an executor.
- **No faults.** Compilation collects diagnostics; execution degrades with a warning and continues (section 7.4). The embedding API never throws.

### 10.2 Symmetry expansion

Symmetry and rotation are expanded at compile time. `symmetry` contributes up to four dimension-preserving flips (identity, H, V, both-axis); `rotation` contributes turns (identity, 90, 180, 270, or a set). The two are composed and the resulting variant set is **de-duplicated by structural equality of the transformed (match side, write tree) pair, scoped per sub-rule** (so two sub-rules with the same LHS both survive). Structural equality means the same pattern cells (expression cells compared as expression trees), the same `{ any }`/`{ all }` nesting in the same item order, and the same weights. The both-axis flip and the 180-degree rotation are the same transform, so `symmetry=all, rotation=all` collapses to the 8 variants of D4 (fewer when the rule is itself symmetric). The runtime treats each surviving variant as an independent pattern (section 6.7). Elimination happens at compile time, but its result is observable, because each variant is a separate candidate weighting its anchor in the shuffle and in the step-mode pick. That is why the key is normative (section 5.6.2).

### 10.3 Snapshot implementation

The per-pass snapshot (section 7.1) is implementable several ways; a pass is the whole statement for a batch mode, one sweep for `settle`, and one application for a step mode. The reference runtime uses a **full copy** (double buffering: matches read the front buffer, writes go to the back, then swap); copy-on-write side tables or per-cell versioning are equivalent and cheaper when few cells are written. A full copy is recommended until profiling says otherwise.

**Empty in number grids.** A `grid of number` stores an **empty sentinel** distinct from every representable integer (a reserved value or a parallel presence bit). Reads coerce the sentinel to `0` for arithmetic and comparison (section 5.8); the `(g == .)` / `(g != .)` tests inspect the sentinel directly, before coercion. Writing `.` stores the sentinel. Tag grids need no sentinel - empty is bit 0 of the mask (section 4.1).

### 10.4 Candidate collection and selection

All modes build from one structure: a vector of **candidate slots**, each a small record of indices - `(anchor_x, anchor_y, sub_rule_idx, variant_idx)`. The chosen `{ any }` alternative is **not** stored; it is drawn at application (section 10.6). Slots reference compile-time tables (sub-rules, expanded variants), so a candidate is a few small integers.

**Eager collection.** The reference implementation collects and matches the whole candidate vector per pass (row-major anchors; cells tested with short-circuit per candidate; `where`/expression cells - including `random` draws - evaluate during this scan). A semi-lazy split (enumerate slots cheaply, match only when pulled, so `once`/`scatter`/`grow` never match the whole grid) is an available optimization - note that it would move match-side `random` draw sites, which the per-implementation-version determinism claim (section 7.2) permits but a golden-output corpus will notice.

**Selection per mode:**
- Batch modes, and each `settle` sweep: collect, seeded-shuffle, pull in shuffle order under the (grid, cell) write mask, stop at count. Shuffling is what gives variants equal priority.
- Step modes (`once`, `grow`): collect, pick **one** uniformly (a single draw; under `ordered`, uniformly within the highest-priority group), apply, **re-collect** (the write changed the match set); repeat.
- `ordered` is a **sort of the vector**, not a per-anchor short-circuit: the whole vector is shuffled (one Fisher-Yates pass, section 10.6), then **stably** sorted by sub-rule priority - so each priority group keeps its shuffled order - then consumed exactly as the shuffled vector would be by the active mode.

**Parallelism (future).** The split makes the parallelizable phase explicit - independent match-testing over the candidate vector: batch matching is embarrassingly parallel (all vs one snapshot) except when the match side draws (`random` shares the one stream and serializes; a position-keyed PRNG would lift this, section 9); `settle` is parallel within a sweep, serial across sweeps; step modes are inherently sequential.

### 10.5 Match enumeration

For small patterns and small grids (the common case), a per-position brute-force match is adequate: test each pulled slot cell-by-cell against the snapshot, short-circuiting on first mismatch. Optimization (indexing, bitmap-accelerated scans) is deferred until profiling justifies it.

When a `where` or match cell contains `random`, the matcher's cell-test order, variant-test order, and short-circuit behaviour become **observable** (they determine which draws fire and in what sequence, section 10.6); such a rule cannot have its slots matched in parallel or reordered without changing output. Rules with no `random` on the match side keep that freedom.

### 10.6 Random seeding and draw order

A single PRNG stream (`mt19937_64` in the reference implementation), seeded from the invocation seed, drives all stochastic decisions. Every draw site is ordered by the execution model, so the sequence is fixed per (entry, seed, params):

1. **Param expressions** (section 4.2) evaluate once at startup, in canonical declaration order (section 2.6), an input's default only when the param was not supplied - their draws come first.
2. A **`when` guard** (section 6) evaluates once when its statement is reached, in statement order; if it passes, the statement's **count** (section 6), if any, evaluates next.
3. **Match-cell and `where`-cell** expressions evaluate during the collection scan, in row-major anchor order and, within an anchor, in sub-rule/variant declaration order; cells test in row-major order, short-circuiting on first mismatch - a `random` in a cell not reached does not draw.
4. **Candidate ordering** draws next: the pass's seeded shuffle (batch modes and `settle` sweeps) or the single uniform pick (step modes), per section 10.4.
5. **At application**: one draw per `{ any }` node on the resolved write path, outer before inner, earlier sibling first (a single-item `{ any }` still draws, section 5.2); then write-cell expressions, row-major per resolved leaf.
6. **`path` statements** follow the section 6.6 draw contract: predicate passes (`from`, `to`, `passable`), per-cell `cost`, exactly one tie-key draw, then the start-to-goal `write` stamps.

A sequence application (section 6.10) adds no draw sites: its body statements draw per items 2-6 as each one is reached, and the stability check draws nothing.

**Draw primitives.** Every draw site maps the stream to its decision with one of the following, in unsigned 64-bit integer arithmetic. They are part of the determinism contract (section 7.2): an implementation must reproduce them exactly, so it may not substitute library equivalents such as `std::shuffle` or `std::uniform_int_distribution`.

- **Raw draw**: the stream's next 64-bit output. Used once per `path` statement for the tie key (section 6.6).
- **`uniform(n)`**, an integer in `[0, n)` for `n >= 1`, without bias: let `t = (2^64 - n) mod n` (in 64-bit arithmetic, `(0 - n) % n`); take raw draws until one, `x`, satisfies `x >= t`; the result is `x mod n`. It may consume more than one raw draw, but it counts as one draw for the order above.
- **Shuffle** of a vector of `k` candidates (Fisher-Yates): for `i` from `k - 1` down to `1`, swap the items at `i` and `uniform(i + 1)`.
- **Uniform pick** of one of `k` candidates (step modes): the item at `uniform(k)`.
- **Weighted `{ any }`** over items with integer weights `w1..wm` and total `W > 0`: `r = uniform(W)`; the chosen item is the first whose running weight sum exceeds `r`.
- **`random(lo, hi)`**: `lo + uniform(hi - lo + 1)`. An empty range (`hi < lo`) still draws once, as `uniform(1)`, and yields `lo`.

**Collection order.** The candidate vector that a shuffle or pick works on is built in the order of item 3: anchors in row-major order, and at each anchor every variant whose pattern fits there, in sub-rule and variant declaration order (section 5.6.2). Its order is part of the contract because the shuffle permutes positions.

Determinism is per (entry, seed, params, implementation version), on every platform - section 7.2.

### 10.7 Sequence stability

The reference approach copies the grid stack at the start of an iteration and compares it at the end. That costs one extra stack, the same order of cost as the snapshot double buffer (section 10.3). A cheaper equivalent keeps a per-(grid, cell) set of cells written during the iteration, and compares only those against their start values, plus the dimensions. It must still compare values, because a cell written back to its original value is not a change (section 6.10). Both approaches are unobservable. `once S` never needs the comparison, and neither does applying the entry (section 6).

### 10.8 Module loading

The reference compiler loads the closure in one depth-first walk from the root. Each `use` calls the resolver with the path and the using module's canonical name. Results are cached by canonical name, so each module is read and parsed once. A module is marked in progress while its own `use` declarations are walked: a `use` that reaches an in-progress module is a cycle (check 40). Appending each module when its walk completes yields the canonical order (section 2.6) with no separate sort.

Names go into closure-wide tables built in canonical order. That one pass assigns layer indices, the param evaluation order, and sequence ids. Visibility is checked after resolution: each module keeps the set of modules it sees (itself plus its direct uses). A name whose declaring module is outside that set is reported as not visible, and the diagnostic names the declaring module, so the fix (`use "..."`) is in the message.

The compiled artifact stays self-contained (section 10.1). It holds the merged closure, the canonical name of each module, and a (module, line) source location for each statement and rule, so diagnostics and a debugger can map steps back to the right file after the AST is discarded. `generator::modules()` lists the closure for hot reload: when any listed module changes, recompile from the root. A failed compile still lists the modules it resolved, so a host can watch a broken file and retry once it is fixed.

---

## Appendix A: The embedding API

The reference implementation is a C++ library first and a CLI second. A game embeds `ls.hpp` and sees exactly four types - `generator`, `level`, `grid`, `generation` - in namespace `ls`. Nothing throws: a failed compile yields a falsy `generator` carrying `error()`, and every query on an invalid object reads as empty.

```cpp
auto gen   = ls::generator::compile(source, "dungeon.ls", resolve);  // once, at load
int  wall  = gen.tag("geometry.wall");              // resolve names once
int  entry = gen.sequence("main");                  // any sequence can be the entry
auto level = gen.generate(entry, seed);             // pure in (entry, seed, params)
auto geo   = level["level"];
if (geo.at(x, y) == wall) ...
```

**The factory-vs-value split.** A `generator` is a **stateless, reusable factory**: one per root module and its closure (section 2.6), shareable across threads; each `generate`/`begin` call names its entry, is independent, and carries its own run state. A `level` is a **self-contained owned value**: it owns its cells and outlives the generator that made it. A `grid` **shares ownership** of the level data it views, so a grid handed to a garbage-collected scripting host can outlive the `level` object it came from. Internally, one coroutine-backed execution core (section 10.1) serves both `generate` (drain to completion) and `begin`/`step` (pull incrementally); they cannot diverge.

### `generator` - one compiled module closure

| member | meaning |
|--------|---------|
| `static generator compile(const std::string& source, const std::string& name = "generator", resolver resolve = {})` | Compile `source` as the root module (section 2.6), with canonical name `name`. The game owns file/asset IO: each `use` is mapped to a module by `resolve` (see Module resolution below); with no resolver, every `use` is an unresolved module (section 7.3, check 9). Diagnostics are labelled with canonical module names (`"dungeon.ls:12:3: error: ..."`). |
| `explicit operator bool()` | True iff compilation succeeded. |
| `std::string error()` | Formatted diagnostics when compile failed; `""` on success. |
| `std::string warnings()` | Formatted warnings of a successful compile (e.g. the reductivity warning, section 7.4); `""` when none. |
| `int tag(const std::string& qualified)` | Tag value id, qualified by tagset: `tag("geometry.wall")`. Ids are per tagset, valid for every layer of that tagset. `-1` if unknown. |
| `int sequence(const std::string& name)` | Sequence id, for use as an entry (section 6). `-1` if unknown. Ids are `0 .. sequence_count() - 1` in canonical declaration order (section 2.6). |
| `int sequence_count()` | Number of sequences in the closure. |
| `std::string sequence_name(int id)` | Name of a sequence id (`""` if out of range). |
| `std::vector<std::string> modules()` | Canonical names of the closure's modules in canonical order, root last (section 2.6). After a failed compile, the modules resolved before the failure. For hot reload. |
| `level generate(int entry, uint64_t seed, const std::vector<std::pair<std::string, int>>& params = {})` | Run sequence `entry` as the entry (section 6): (entry, seed, params) -> `level`, deterministically (section 7.2). Params override declared defaults; unknown names are ignored. An invalid entry id yields an empty level. |
| `generation begin(int entry, uint64_t seed, step_mode mode = step_mode::statement, observe obs = observe::off, const std::vector<std::pair<std::string, int>>& params = {})` | Start a progressive run of `entry`; pull it with `generation::step()`. With an invalid entry id, the first `step()` returns `false`. |
| `int statement_count(int entry)` | Number of statements in the entry's body (progress denominators). Statements inside nested sequences are not counted; a sequence application counts as one. `0` for an invalid id. |

**Module resolution.** `compile` maps each `use` (section 2.6) through a host callback:

```cpp
struct module_source { std::string name; std::string source; };
using resolver = std::function<std::optional<module_source>(const std::string& path, const std::string& from)>;
```

`path` is the string written in the `use`; `from` is the canonical name of the using module (the root's is `compile`'s `name`). The resolver returns the module's canonical name and source text, or `std::nullopt` when it cannot resolve the path (check 9). The canonical name identifies the module within the compile and labels its diagnostics, so the resolver must return the same name for every path that denotes the same file. The CLI's resolver reads the filesystem, relative to the directory of `from`.

### `level` - one generated outcome: the stack of co-registered grids

| member | meaning |
|--------|---------|
| `int width()`, `int height()` | Final dimensions. |
| `int layer_count()` | Number of declared layers. |
| `grid operator[](const std::string& layer_name)` | Layer by name. |
| `grid layer(int index)` | Layer by layer order (section 4). |
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
| `void stop_at_begin(bool on)` | Also stop *before* each statement: `step()` then returns at the begin of every statement whose guard passed (a rule application, an operation, or a sequence application), with `stmt_stack()` naming it. Off by default. For debuggers: breaking or stepping to a statement before it changes anything. |
| `bool at_statement_begin()` | Whether the last step is a statement's begin (see `stop_at_begin`). Observe channel. |
| `level snapshot()` | Copy of the current state - committed statements plus the current batch's applications so far. |
| `level finish()` | Drain whatever remains and return the finished level (the "skip" path). |
| `int stmt_index()` | Index, in the entry's body, of the statement the last step worked on (`-1` before the first step). Inside a nested sequence, this is the index of the entry-body statement that applied it. Observe channel. |
| `std::vector<stmt_frame> stmt_stack()` | Position of the last step through nested sequences, outermost first. Frame 0 is a statement of the entry's body (its `index` equals `stmt_index()`); each further frame is a statement inside the sequence applied by the frame before it. Empty before the first step. Observe channel. |
| `std::vector<cell_highlight> highlights()` | Matched/written cells of the last application. Observe channel; populated only when begun with `observe::on`. |

Supporting types:

- `enum class step_mode { statement, application }` - the pull granularity: one **leaf** statement per `step()`, or one rule application per `step()` (operations and atomic batches still advance whole). A leaf statement is a rule application or an operation, in the entry's body or inside a nested sequence. Applying a sequence is not itself a step, and neither is its stability check.
- `enum class observe { off, on }` - whether the run records the observe channel (`stmt_index`, `stmt_stack`, `highlights`); off costs nothing.
- `struct stmt_frame { int index; int iteration; }` - one level of the statement stack. `index` is the statement's position in its enclosing list (the entry's body, or a nested sequence body). `iteration` is the 0-based iteration of the enclosing sequence application (always 0 for frame 0, since the entry runs once).
- `struct cell_highlight { enum class kind { match, write }; int layer; int x, y; kind what; }` - one highlighted cell: which layer/cell the last application matched or wrote.

`generation` is move-only (it owns the in-flight coroutine state). Dropping it mid-run is safe; `finish()` is the explicit skip-to-end.
