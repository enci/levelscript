# LevelScript tutorial plan

The tutorial lives in `samples/tutorial/` and is listed in the website lab next to the game samples in `samples/games/`. Each step is one self-contained, runnable `.lvs` file that introduces one or two concepts on top of the previous step.

## Conventions

- **Names carry the step.** Files are named `NN_<topic>.lvs` with a two-digit step (`01_first_grid.lvs`), so a file listing and the site both show them in order; the `tutorial/` folder already says what they are. Two digits keep the sort correct past step 9. Inserting a step means renumbering the files after it, which also changes their `#sample=` links.
- **Header comment.** The leading `//` lines are the step's description on the site (`tools/build_site_data.py` takes them as `about`). One or two sentences: what the step builds and what it introduces.
- **Seed and checks.** Every step pins `// @seed=N` and carries at least one `// @expect` (Appendix B.7). The golden runner does not run the samples for now; `levelscript --check <file>` verifies a step by hand.
- **Pacing.** Use `@play` and `@hold` (B.5, B.6) so the step reads well when played in the lab: instant fills, visible walks, a hold on the result the step is about.
- **Exercise.** Each step ends with a commented exercise: a small change the reader makes in the lab.

## Arc

Steps 1-5 build a cave and cover the core rule model. Steps 6-11 build a room-based dungeon on a partition, and introduce number grids, expressions and parameters on top of it. Steps 12-14 add the remaining operations. Steps 15-18 cover graphs, modules and embedding; the dungeon from step 17 mirrors `samples/games/roguelike/`.

## Steps

### 1. First grid (`01_first_grid`)
- **Covers:** `tag`, `layers`, `resize`, a one-cell rule, `once`; `.` vs `*`, empty as bit 0.
- **Builds:** an empty grid with a single start cell.
- **Exercise:** place the start cell with a different pattern.
- **Spec:** 2.5, 3, 4, 4.1, 5.1, 6.1.

### 2. Random walks (`02_random_walks`)
- **Covers:** `grow(N)`, `rotation=all`, step modes; why `grow` needs a count.
- **Builds:** a random-walk cave in an `algo` grid.
- **Exercise:** restrict rotation and compare the shapes.
- **Spec:** 5.6.2, 6.7, 6.9.

### 3. Batch application (`03_batch`)
- **Covers:** `everywhere`, `scatter(N)` and `scatter(P%)`, snapshot semantics, the write-protection mask.
- **Builds:** walls around the walk; scattered decoration.
- **Exercise:** change `scatter(10)` to `scatter(10%)` and explain the difference.
- **Spec:** 6.7, 6.8, 7.1.

### 4. Multiple layers (`04_layers`)
- **Covers:** several grids in one stack, cross-grid matching, `all` bodies.
- **Builds:** translates `algo` into a `level` grid.
- **Exercise:** add a third layer for entities.
- **Spec:** 4, 5.2, 5.3.

### 5. Tag masks (`05_masks`)
- **Covers:** named unions, complements, `!.`, union writes refined later.
- **Builds:** a `solid` mask used by several rules; enemies written as a union and refined.
- **Exercise:** add a value and see which masks pick it up.
- **Spec:** 3, 5.5.

### 6. Partitions (`06_partitions`)
- **Covers:** `grid of number`, empty vs `0`, `bsp` and `voronoi`, number literals in patterns.
- **Builds:** a `zones` grid from `bsp`; one zone filled with floor by literal id.
- **Exercise:** swap `bsp` for `voronoi` over scattered sites.
- **Spec:** 4.1, 6.11, 6.12.

### 7. Conditions (`07_where`)
- **Covers:** expressions, `where` cells, `x`/`y`, `width`/`height`, built-in functions.
- **Builds:** rooms carved inside zones (a margin from each zone edge), zones selected by id and position.
- **Exercise:** carve only zones in the left half of the map.
- **Spec:** 5.8, 5.9, 5.10.

### 8. Parameters (`08_params`)
- **Covers:** `params`, input vs derived, statement `when`, `random`.
- **Builds:** room size and an optional flooded zone driven by params.
- **Exercise:** add a `difficulty` param that turns the flood on.
- **Spec:** 4.2, 6 (statement guards).

### 9. Pattern variables (`09_variables`)
- **Covers:** `?name`, order-free binding, two-phase matching, rule guards.
- **Builds:** walls between zones (`zones[?a ?b] when (?a != ?b)`), doors on shared walls.
- **Exercise:** write `fall` for loose rocks.
- **Spec:** 5.9.1, 5.11.

### 10. Choice and weights (`10_choice`)
- **Covers:** `{ any }`, weights, `{ all }`, nesting, draw order.
- **Builds:** room roles per zone (hub, reward, challenge) with weighted contents.
- **Exercise:** make reward rooms rarer.
- **Spec:** 5.2, 5.5, 10.6.

### 11. Cellular automata (`11_settle`)
- **Covers:** `settle` and `settle(N)`, sequences, stability, non-termination.
- **Builds:** a cave zone smoothed inside the dungeon.
- **Exercise:** find a rule that never settles and read the warning.
- **Spec:** 6.7, 6.9, 6.10.

### 12. Priority (`12_ordered`)
- **Covers:** `ordered`, and how it behaves under batch vs step modes.
- **Builds:** door placement with fallbacks.
- **Exercise:** reorder the alternatives and compare.
- **Spec:** 5.2.

### 13. Symmetry and framing (`13_symmetry`)
- **Covers:** the `symmetry` attribute, `mirror`, `pad`, `trim`, `upscale`.
- **Builds:** a symmetric arena.
- **Exercise:** mirror on the other axis.
- **Spec:** 5.6.1, 6.2-6.5.

### 14. Paths and terrain (`14_paths`)
- **Covers:** `path`, `noise`, rank vs range normalization.
- **Builds:** a corridor from start to exit over noise-weighted terrain.
- **Exercise:** add a `cost` expression so paths avoid water.
- **Spec:** 6.6, 6.13.

### 15. Graph layers (`15_graphs`)
- **Covers:** graph layers over partitions, extraction, graph patterns, graph functions, selectors.
- **Builds:** the zone adjacency graph; start and exit placed by distance.
- **Exercise:** place the boss in the farthest room from the start.
- **Spec:** 4.3, 5.12.

### 16. Graph rules (`16_graph_rules`)
- **Covers:** graph blocks, assignments, node-extent writes.
- **Builds:** a treasure room at a dead end.
- **Exercise:** lock dead ends behind a key.
- **Spec:** 5.13.

### 17. Modules (`17_modules`)
- **Covers:** `use`, visibility, canonical order; schema / layout / decoration split.
- **Builds:** the dungeon split into modules.
- **Exercise:** swap the decoration module.
- **Spec:** 2.6, 10.8.

### 18. Determinism and embedding (`18_embedding`)
- **Covers:** seeds, `@seed`, CLI params, the `ls::` API, Wren.
- **Builds:** the same level from the CLI and from host code.
- **Exercise:** run two seeds and diff the outputs.
- **Spec:** 7.2, Appendix A, Appendix B.

## Open items

- Steps 15-16 wait for the graph-rule redesign (`n.degree` attribute syntax, region writes).
- Step 7 may use `is` once its semantics settle.
- Sized patterns are being replaced; no step depends on them.
- The site build needs the changes listed in `samples/README.md`: scanning `samples/`, a `group` field and path-keyed `LVS_FILES` for `use`.
