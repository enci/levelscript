#!/usr/bin/env python3
"""Generate docs/js/samples-data.js for the website.

The site runs every program live through the WASM build, so this only needs the
sources: each sample's text, pinned seed, one-line description and declared
params, plus the modules samples `use` (tests/golden/programs/modules/).

    python tools/build_site_data.py

Local preview of the whole site (CI does the same, see .github/workflows/pages.yml):

    source ~/emsdk/emsdk_env.sh
    emcmake cmake -B build/wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build/wasm
    mkdir -p docs/wasm && cp extension/ls_wasm.js extension/ls_wasm.wasm docs/wasm/
    python tools/build_site_data.py
    npm --prefix web ci && npm --prefix web run build     # the editor: docs/js/cm-editor.js
    python -m http.server -d docs

While working on the editor, `npm --prefix web run watch` rebuilds the bundle on every save.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Gallery order; anything not listed follows alphabetically.
ORDER = ["dungeon", "cave", "island", "rooms", "drunkard", "arena", "pathways",
         "graph_rooms", "corridor", "decorate", "terrain", "sprout", "flood"]


def header(src: str) -> str:
    lines = []
    for ln in src.splitlines():
        if not ln.startswith("//"):
            break
        text = ln[2:].strip()
        if not text.startswith("@"):
            lines.append(text)
    return " ".join(l for l in lines if l)


def params(src: str) -> list:
    m = re.search(r"\bparams\s*\{(.*?)\}", src, re.S)
    if not m:
        return []
    return [{"name": p.group(1), "default": int(p.group(2))}
            for p in re.finditer(r"(\w+)\s*:\s*number\s*=\s*(-?\d+)", m.group(1))]


def slug(heading: str) -> str:
    """The id the spec viewer gives a heading (docs/js/main.js, spec viewer)."""
    return re.sub(r"^-|-$", "", re.sub(r"[^\w]+", "-", heading.lower())) or "section"


def table(md: str, heading: str) -> list:
    """Rows of the first pipe table after `heading`, as lists of cell text."""
    start = md.find(heading)
    if start < 0:
        raise SystemExit(f"spec.md: heading not found: {heading!r}")
    rows, seen = [], False
    for ln in md[start:].splitlines()[1:]:
        if ln.startswith("|"):
            seen = True
            cells = [c.strip() for c in ln.strip().strip("|").split("|")]
            if not all(re.fullmatch(r":?-+:?", c) for c in cells):
                rows.append(cells)
        elif seen:
            break
    return rows[1:]  # drop the header row


def plain(text: str) -> str:
    return re.sub(r"\s+", " ", re.sub(r"[`*]", "", text)).strip()


def section_of(md: str, ref: str, default: str) -> str:
    """Anchor of the spec section a table row cites, e.g. 'section 6.6' -> 6-6-...."""
    m = re.search(r"section (\d+\.\d+)", ref)
    if not m:
        return default
    h = re.search(rf"^#+ {re.escape(m.group(1))} .*$", md, re.M)
    return slug(h.group(0).lstrip("# ")) if h else default


def api_entries(md: str) -> list:
    """Searchable language reference, distilled from the spec's own tables.

    One entry per function (5.10), operation (6.0), mode (6) and keyword (2.4):
    {name, kind, signature, summary, anchor}. Fails if a table moves or empties.
    """
    out = []
    fn_anchor = slug("5.10 Built-in functions")
    for call, sig, result in table(md, "### 5.10 Built-in functions"):
        name = re.match(r"`(\w+)", call).group(1)
        out.append({"name": name, "kind": "function", "signature": plain(call),
                    "summary": plain(result).split(". ")[0].rstrip("."), "anchor": fn_anchor,
                    "detail": plain(sig)})
    for op, params, effect in table(md, "**Operation table.**"):
        name = plain(op)
        items = re.findall(r"`([^`]+)`", params)
        sig = name + "(" + ", ".join(items) + ")"
        # what the sandbox inserts: required parameters only, as name=name when all are named
        named = "all named" in params
        req = [i.split(":")[0].strip() for i in items if ":" in i and "?" not in i]
        insert = name + "(" + ", ".join(f"{r}={r}" if named else r for r in req) + ")"
        out.append({"name": name, "kind": "operation", "signature": sig, "insert": insert,
                    "summary": re.sub(r"\s*\(section [^)]*\)", "", plain(effect)),
                    "anchor": section_of(md, effect, slug("6.0 Built-in operations"))})
    mode_anchor = slug("6.7 Rule application semantics")
    for mode, over_rule, _ in table(md, "**Modes.**"):
        for form in re.findall(r"`([^`]+)`", mode):
            out.append({"name": re.match(r"\w+", form).group(0), "kind": "mode", "signature": form,
                        "summary": plain(over_rule), "anchor": mode_anchor})
    kw = re.search(r"### 2\.4 Reserved keywords\s+```\n(.*?)```", md, re.S)
    declared = {e["name"] for e in out}
    for word in kw.group(1).split() if kw else []:
        if word not in declared:
            out.append({"name": word, "kind": "keyword", "signature": word, "summary": KEYWORDS.get(word, ""),
                        "anchor": slug("2.4 Reserved keywords")})
    missing = [e["name"] for e in out if e["kind"] == "keyword" and not e["summary"]]
    if missing or not any(e["kind"] == "function" for e in out):
        raise SystemExit(f"API extraction incomplete; add KEYWORDS entries for: {missing}")
    for e in out:
        e.setdefault("insert", e["signature"])
    # dedupe: a mode keyword listed twice (scatter(N) / scatter(P%)) keeps the first
    seen, uniq = set(), []
    for e in out:
        k = (e["name"], e["kind"])
        if k not in seen:
            seen.add(k)
            uniq.append(e)
    return sorted(uniq, key=lambda e: (e["kind"], e["name"]))


# One-line meaning for keywords, which the spec lists but does not tabulate.
KEYWORDS = {
    "tag": "declare a tagset: named cell values and named masks", "layers": "declare the grids a generator works on",
    "grid": "a layer of cells", "graph": "a graph layer: regions as nodes, adjacency as edges",
    "of": "binds a grid to its tagset (grid of tagset)", "number": "numeric cell type",
    "rule": "a pattern => pattern rewrite", "sequence": "a named, reusable list of statements",
    "params": "inputs and derived values of a generator", "where": "pseudo-layer matching on a boolean expression",
    "when": "guard: run the statement only if the param expression holds", "use": "import another module",
    "any": "pick one alternative at random", "all": "every sub-rule must match",
    "ordered": "sub-rules tried in priority order", "weight": "relative chance of an alternative",
    "is": "mask match: a is m holds when a overlaps the mask m, as a pattern cell matches",
}


def editor_data() -> dict:
    """Vocabulary and palette for the site's editor, single-sourced from the extension.

    Keywords and builtins come from the TextMate grammar; the palette from palette.ts.
    Both the dark and the light set are read (the site has both themes). Fails if a pattern is missing.
    """
    grammar = json.loads((ROOT / "extension" / "syntaxes" / "levelscript.tmLanguage.json").read_text(encoding="utf-8"))

    def alternation(name: str) -> list:
        m = re.search(r"\(([\w|]+)\)", grammar["repository"][name]["match"])
        words = m.group(1).split("|") if m else []
        if not words:
            raise SystemExit(f"grammar: no word list in repository.{name}")
        return words

    ts = (ROOT / "extension" / "src" / "palette.ts").read_text(encoding="utf-8")

    def palette_of(name: str) -> list:
        m = re.search(rf"{name}\s*=\s*\[(.*?)\]", ts, re.S)
        colors = re.findall(r"'(#[0-9a-fA-F]{6})'", m.group(1)) if m else []
        if len(colors) != 12:
            raise SystemExit(f"palette.ts: expected 12 colors in {name}, found {len(colors)}")
        return colors

    def both_of(fn: str) -> tuple:
        m = re.search(rf"function {fn}\(dark: boolean\)[^{{]*\{{\s*return dark \? '(#[0-9a-fA-F]{{6}})'\s*:\s*'(#[0-9a-fA-F]{{6}})'", ts)
        if not m:
            raise SystemExit(f"palette.ts: dark/light values of {fn} not found")
        return m.group(1), m.group(2)

    empty, any_ = both_of("emptyColor"), both_of("anyColor")
    return {"keywords": alternation("keyword"), "builtins": alternation("builtin"),
            "palette": palette_of("PALETTE_DARK"), "empty": empty[0], "any": any_[0],
            "light": {"palette": palette_of("PALETTE_LIGHT"), "empty": empty[1], "any": any_[1]}}


def main() -> None:
    samples = []
    for lvs in sorted((ROOT / "tests" / "golden" / "programs").glob("*.lvs")):
        src = lvs.read_text(encoding="utf-8")
        seed = re.search(r"//\s*@seed=(\d+)", src)
        samples.append({"name": lvs.stem, "seed": int(seed.group(1)) if seed else 42,
                        "about": header(src), "params": params(src),
                        "source": src.rstrip() + "\n"})
    rank = {n: i for i, n in enumerate(ORDER)}
    samples.sort(key=lambda s: (rank.get(s["name"], len(ORDER)), s["name"]))

    files = {f"modules/{p.name}": p.read_text(encoding="utf-8")
             for p in sorted((ROOT / "tests" / "golden" / "programs" / "modules").glob("*.lvs"))}

    editor = editor_data()
    api = api_entries((ROOT / "docs" / "spec.md").read_text(encoding="utf-8"))

    out = ROOT / "docs" / "js" / "samples-data.js"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("// Generated by tools/build_site_data.py - do not edit or commit.\n"
                   "window.LVS_SAMPLES = " + json.dumps(samples, separators=(",", ":")) + ";\n"
                   "window.LVS_FILES = " + json.dumps(files, separators=(",", ":")) + ";\n"
                   "window.LVS_API = " + json.dumps(api, separators=(",", ":")) + ";\n"
                   "window.LVS_EDITOR = " + json.dumps(editor, separators=(",", ":")) + ";\n",
                   encoding="utf-8")
    print(f"wrote {out.relative_to(ROOT)}: {len(samples)} samples, {len(files)} modules, {len(api)} api entries")


if __name__ == "__main__":
    main()
