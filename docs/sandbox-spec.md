# Sandbox page and API reference

Spec for a standalone sandbox page (`docs/sandbox.html`) with a searchable language reference below the editor. It covers the page around the editor widget. It is written against `main` as of 2026-10-18.

## Motivation

The editor exists only as widgets embedded in the home page and the samples page. There is nowhere to just write a program, nothing keeps work when you navigate away, and finding an operation or built-in means reading the 150 KB spec.

## Scope

**In scope:**
- a `sandbox.html` page with one editor, the run view, params and seed
- named scripts saved in `localStorage`
- a searchable reference generated from the spec
- entry points from the other pages

**Out of scope (for now):**
- multi-tab coordination
- shareable links
- multi-file editing, and `use` of anything beyond the bundled modules

## Decisions

**D1. Persistence is `localStorage`, one key: a library of named scripts.**
Key `lvs.scripts.v1`, holding JSON: `{ current, scripts: [{ id, name, source, seed, params, base, updated }] }`. `current` is the id of the open script. `base` is the sample a script was opened from, or null. `params` is the map of param name to value for that script's source; values for names the source no longer declares are dropped on load.

- Every read and write is wrapped in `try/catch`. Storage can throw in private windows, with blocked site data, or on quota errors. If it is unavailable the page works as normal and shows a one-line note that scripts will not be kept.
- Edits are debounced at 400 ms after the last change, and flushed on `pagehide`, on `visibilitychange` to hidden, and before switching scripts. They never run inside the per-keystroke inspection path.
- Every write re-reads the stored library and changes only its own entry, so a second tab does not wipe scripts made in the first. A `storage` event refreshes the script list; the open script stays as it is.
- A stored value that fails to parse or has the wrong shape is ignored, not an error.
- Migration: on the first visit without `lvs.scripts.v1`, the single draft of earlier versions (`lvs.sandbox.v1`) becomes a script named after its sample (or "my draft"), and its `.prev` becomes "previous draft". The old keys are left in place.

**D2. Script picker: new, rename, delete.**
The toolbar has a picker of the scripts (most recently edited first), and New, Rename and Delete.

- New asks for a name and starts from a small starter program that runs.
- Names are unique: a taken name gets " 2", " 3", ... appended.
- Delete asks first, since it cannot be undone. Deleting the last script creates a new one.
- Switching scripts loads the other script with a fresh undo history, so undo never reaches back into the script that was left.

**D3. Opening a sample or a file makes a new script.**
"Open a sample" and "Open .lvs" never overwrite: each creates a new script named after the sample or the file. So there is no replace prompt, no reset and no "restore previous".

- Sample widgets link to `sandbox.html#sample=<name>`. The sandbox applies the hash once on load and then removes it from the URL. If a script is still an untouched copy of that sample, it is opened; otherwise a new copy is made.
- First visit with no scripts and no hash starts with a copy of the `dungeon` sample.

**D4. Export.**
"Download .lvs" saves the open script's source as `<script name>.lvs`. Nothing is uploaded anywhere.

**D5. Modules.**
The sandbox edits a single file, named `sandbox.lvs`. `use "path"` resolves through the existing `resolver()` against the bundled `LVS_FILES`, so the two bundled modules work. Any other path fails like any unresolved import and the compiler's message is shown in the diagnostics list. No special handling and no extra explanation text.

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
  insert: "voronoi(sites=sites, into=into)",  // what the insert button puts at the caret
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
- Each result shows `signature`, `summary`, a link to the spec section, and an **insert** button that puts `insert` at the editor's caret (required parameters only, as `name=name` for operations whose parameters are all named; no selection handling).
- An empty query lists everything, grouped by kind.
- No match shows "No results for *query*" and a link to the full spec.

**D8. Layout.**
The editor and run view sit on top; the reference is below, on the same page. At phone width everything stacks in one column, with a 16 px gutter and no horizontal page scroll.

## Page behaviour

- The wasm module (about 700 KB) loads on page open. Until it resolves the editor is usable and Run is disabled with a "loading" label. If loading fails the page shows the same message the widgets show today.
- Runs happen in a Web Worker (`docs/js/run-worker.js`). A run is cut off after 5 s by terminating the worker, so an accidental non-terminating program (`grow` without a count) cannot lock the tab. The status says "stopped" and the error box says why. The step, statement and play buttons still run on the main thread, one application at a time.
- The nav gets a "sandbox" link on every page, and the home page hero and sample cards link to it.
- The seed field and the params panel persist with each script (D1) so a rerun reproduces.

## Affects

- `docs/sandbox.html` (new): nav, toolbar, editor host, reference section; loads `ls_wasm.js`, `samples-data.js`, `api-search.js`, `main.js`.
- `docs/js/main.js`: sandbox setup next to `createWidget` (storage, toolbar, reference), widget options for the sandbox (`initial`, `onChange`, `worker`, `dynamicParams`), and the "open in sandbox" link on the samples page.
- `docs/js/api-search.js` (new): the pure search function.
- `docs/js/run-worker.js` (new): runs a program off the main thread.
- `docs/css/style.css`: sandbox layout, search box, chips, result rows.
- `docs/index.html`, `docs/getting-started.html`, `docs/samples.html`, `docs/spec.html`: add the nav link.
- `tools/build_site_data.py`: `api_entries()` and the `KEYWORDS` dict, emitting `window.LVS_API` (done).

## Acceptance

- **A1.** Edit the program, navigate to another page, come back: the edited source, seed and param values are restored.
- **A2.** With storage disabled (private window, or `localStorage` throwing), the sandbox works and shows the note. No uncaught errors in the console.
- **A3.** Opening a sample or a file creates a new script and leaves the open one unchanged. Following the same `#sample=` link twice opens the untouched copy instead of making another.
- **A4.** New, Rename and Delete update the picker and survive a reload; after switching scripts, undo does not bring back the previous script's text. An old `lvs.sandbox.v1` draft shows up as a script.
- **A5.** Searching `rand` lists `random`; `path` lists the `path` operation first; `zzz` shows the no-results message.
- **A6.** Insert on `resize` puts `resize(w, h)` at the caret, and the result link goes to the 6.1 heading in `spec.html`.
- **A7.** Deleting a row from the 5.10 table and rebuilding removes that entry from search. Emptying the table makes `build_site_data.py` fail.
- **A8.** A program using `use "does/not/exist.lvs"` shows the compiler's error in the diagnostics list and does not run.
- **A9.** A `grow` with no count that never reaches a fixpoint stops after 5 s and the page stays responsive.
- **A10.** At 360 px wide the page has no horizontal scroll.

## Parked

- **P1.** Full multi-tab coordination. Writes re-read the library (D1), but two tabs editing the same script still race: the last write wins.
- **P2.** Shareable links (source in the URL hash, compressed). Needs a size fallback for large programs.
- **P3.** Rule attributes and graph arguments in the reference (see D6).
- **P4.** Fuzzy matching or highlighting the matched text in results.
- **P5.** Undo for Delete (a trash), and folders or tags for scripts.
