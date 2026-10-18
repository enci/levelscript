# Sandbox page and API reference

Spec for a standalone sandbox page (`docs/sandbox.html`) with a searchable language reference below the editor. It builds on `web-editor-spec.md`, which upgrades the editor widget itself; this spec covers the page around it. It is written against `main` as of 2026-10-18.

## Motivation

The editor exists only as widgets embedded in the home page and the samples page. There is nowhere to just write a program, nothing keeps work when you navigate away, and finding an operation or built-in means reading the 150 KB spec.

## Scope

**In scope:**
- a `sandbox.html` page with one editor, the run view, params and seed
- a draft saved in `localStorage`
- a searchable reference generated from the spec
- entry points from the other pages

**Out of scope (for now):**
- multi-tab coordination
- shareable links
- multi-file editing, and `use` of anything beyond the bundled modules

## Decisions

**D1. Persistence is `localStorage`, one key.**
Key `lvs.sandbox.v1`, holding JSON: `{ source, seed, params, savedAt }`. `params` is the map of param name to value for the current source; values for names the source no longer declares are dropped on load.

- Every read and write is wrapped in `try/catch`. Storage can throw in private windows, with blocked site data, or on quota errors. If it is unavailable the page works as normal and shows a one-line note that the draft will not be kept.
- Writes are debounced at 400 ms after the last `input`, and flushed on `pagehide` and `visibilitychange` to hidden. They never run inside the per-keystroke inspection path of `web-editor-spec.md` D2.
- A stored value that fails to parse or has the wrong shape is ignored, not an error.
- `v1` is in the key so the shape can change later without migrating by hand.

**D2. Opening a sample into the sandbox.**
Sample widgets get an "open in sandbox" action that navigates to `sandbox.html#sample=<name>`. The sandbox applies the hash once on load and then removes it from the URL.

- If the stored draft is empty or unchanged from its starting sample, the sample replaces it.
- Otherwise the page asks first: "Replace your current draft with *name*?" with Replace and Keep draft. Keep leaves the draft untouched.
- First visit with no draft and no hash loads the `dungeon` sample.

**D3. Reset and one-level undo.**
A "reset" action loads the default sample. Before any replacement of a non-empty draft (reset, or opening a sample), the previous draft is kept under `lvs.sandbox.v1.prev`, and a "restore previous" action swaps it back. Only one level is kept.

**D4. Export and import.**
"Download .lvs" saves the source as `sandbox.lvs`. "Open .lvs" loads a local file into the editor through a file input. Nothing is uploaded anywhere. Importing goes through the same replace prompt as D2.

**D5. Modules.**
The sandbox edits a single file, named `sandbox.lvs`. `use "path"` resolves through the existing `resolver()` against the bundled `LVS_FILES`, so the two bundled modules work. Any other path fails like any unresolved import and the compiler's message is shown in the diagnostics list (`web-editor-spec.md` C3). No special handling and no extra explanation text.

**D6. The reference is generated from the spec.**
`tools/build_site_data.py` reads `docs/spec.md` and emits `window.LVS_API` into `samples-data.js`. The spec stays the only place the content lives.

Source tables:

| kind | spec source |
|---|---|
| `function` | the table in 5.10 Built-in functions |
| `operation` | the "Operation table" in 6.0 |
| `mode` | the "Modes" table in section 6 |
| `keyword` | the list in 2.4 Reserved keywords |

Entry shape:

```js
{
  name: "voronoi",                 // search key
  kind: "operation",               // function | operation | mode | keyword
  signature: "voronoi(sites:pred, into:grid, ...)",
  summary: "partition the cells by nearest site, ...",
  anchor: "6-12-voronoi",          // heading id in spec.html
  detail: "..."                    // functions only: the typed signature
}
```

- `anchor` is the id the spec viewer gives the heading (the same slug rule as `docs/js/main.js`), so a result links to `spec.html#<anchor>`.
- Keywords are listed but not tabulated in the spec, so their one-line meanings live in a `KEYWORDS` dict in the build script. The build fails if a keyword in 2.4 has no entry there, and if a table can't be found or comes back empty.
- The rule attributes (`symmetry`, `rotation`) and graph arguments (`nodes`, `edges`) are not included yet. They are contextual names rather than keywords, so they have no table to read.

**D7. Search is client-side and dependency-free.**
A text box filters `LVS_API` as the user types.

- Case-insensitive. A query matches an entry when every whitespace-separated term is a substring of `name`, `kind` or `summary`.
- Ranking, best first: exact name, name prefix, name substring, then summary/kind matches. Ties keep the sorted order (kind, then name).
- Kind chips (all, function, operation, mode, keyword) narrow the results.
- Each result shows `signature`, `summary`, a link to the spec section, and an **insert** button that puts the call at the editor's caret (the signature with its parameter names as placeholders, no selection handling).
- An empty query lists everything, grouped by kind.
- No match shows "No results for *query*" and a link to the full spec.

**D8. Layout.**
The editor and run view sit on top; the reference is below, on the same page. At phone width everything stacks in one column, with a 16 px gutter and no horizontal page scroll.

## Page behaviour

- The wasm module (about 700 KB) loads on page open. Until it resolves the editor is usable and Run is disabled with a "loading" label. If loading fails the page shows the same message the widgets show today.
- A run that is still going can be stopped. A run is also capped by a step limit, so an accidental non-terminating program (`grow` without a count) cannot lock the tab. The status says "stopped" with the limit reached.
- The nav gets a "sandbox" link on every page, and the home page hero and sample cards link to it.
- The seed field and the params panel persist with the draft (D1) so a rerun reproduces.

## Affects

- `docs/sandbox.html` (new): nav, editor host, reference section; loads `ls_wasm.js`, `samples-data.js`, `main.js`.
- `docs/js/main.js`: a `createSandbox` entry alongside `createWidget`, the storage helpers, search, and the "open in sandbox" action in the sample widgets.
- `docs/css/style.css`: sandbox layout, search box, chips, result rows.
- `docs/index.html`, `docs/getting-started.html`, `docs/samples.html`, `docs/spec.html`: add the nav link.
- `tools/build_site_data.py`: `api_entries()` and the `KEYWORDS` dict, emitting `window.LVS_API` (done).

## Acceptance

- **A1.** Edit the program, navigate to another page, come back: the edited source, seed and param values are restored.
- **A2.** With storage disabled (private window, or `localStorage` throwing), the sandbox works and shows the note. No uncaught errors in the console.
- **A3.** Opening a sample with an edited draft present asks before replacing it, and Keep draft changes nothing.
- **A4.** After Reset, "restore previous" brings the old draft back.
- **A5.** Searching `rand` lists `random`; `path` lists the `path` operation first; `zzz` shows the no-results message.
- **A6.** Insert on `resize` puts `resize(w, h)` at the caret, and the result link goes to the 6.1 heading in `spec.html`.
- **A7.** Deleting a row from the 5.10 table and rebuilding removes that entry from search. Emptying the table makes `build_site_data.py` fail.
- **A8.** A program using `use "does/not/exist.lvs"` shows the compiler's error in the diagnostics list and does not run.
- **A9.** A `grow` with no count that never reaches a fixpoint stops at the step limit and the page stays responsive.
- **A10.** At 360 px wide the page has no horizontal scroll.

## Parked

- **P1.** Multi-tab coordination (the `storage` event). Today the last tab to write wins.
- **P2.** Shareable links (source in the URL hash, compressed). Needs a size fallback for large programs.
- **P3.** Rule attributes and graph arguments in the reference (see D6).
- **P4.** Fuzzy matching or highlighting the matched text in results.
- **P5.** More than one level of undo, or named saved drafts.
