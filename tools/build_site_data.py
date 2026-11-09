#!/usr/bin/env python3
"""Generate docs/js/samples-data.js for the website.

Besides the samples it carries the version from src/version.hpp (LVS_VERSION), which
the pages show wherever an element has data-ls-version (see docs/js/main.js).

The site runs every program live through the WASM build, so this only needs the
sources: each sample's text, pinned seed, one-line description and declared
params, from samples/{tutorial,techniques,games}/.
It also carries the lab's Reference: names and signatures from the spec's tables,
the user-facing text from docs/reference.md (see that file's header). Every sample file also goes
into LVS_FILES under its folder path, so `use "other.lvs"` resolves next to it.

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

# Gallery sections, in order; samples within one sort by path (tutorials are
# numbered, a game of several modules sits in its own subfolder).
GROUPS = ["tutorial", "techniques", "games"]


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


def api_entries(md: str) -> list:
    """The names, kinds and signatures of the language reference, from the spec's own tables.

    One entry per function (5.10), operation (6.0), mode (6) and keyword (2.4):
    {name, kind, signature, insert, summary}. The summary here is the spec's terse one;
    with_docs() replaces it with the user-facing text. Fails if a table moves or empties.
    """
    out = []
    for call, sig, result in table(md, "### 5.10 Built-in functions"):
        name = re.match(r"`(\w+)", call).group(1)
        out.append({"name": name, "kind": "function", "signature": plain(call),
                    "summary": plain(result).split(". ")[0].rstrip("."), "detail": plain(sig)})
    for op, params, effect in table(md, "**Operation table.**"):
        name = plain(op)
        items = re.findall(r"`([^`]+)`", params)
        sig = name + "(" + ", ".join(items) + ")"
        # what the lab inserts: required parameters only, as name=name when all are named
        named = "all named" in params
        req = [i.split(":")[0].strip() for i in items if ":" in i and "?" not in i]
        insert = name + "(" + ", ".join(f"{r}={r}" if named else r for r in req) + ")"
        out.append({"name": name, "kind": "operation", "signature": sig, "insert": insert,
                    "summary": re.sub(r"\s*\(section [^)]*\)", "", plain(effect))})
    for mode, over_rule, _ in table(md, "**Modes.**"):
        for form in re.findall(r"`([^`]+)`", mode):
            out.append({"name": re.match(r"\w+", form).group(0), "kind": "mode", "signature": form,
                        "summary": plain(over_rule)})
    kw = re.search(r"### 2\.4 Reserved keywords\s+```\n(.*?)```", md, re.S)
    declared = {e["name"] for e in out}
    for word in kw.group(1).split() if kw else []:
        if word not in declared:
            out.append({"name": word, "kind": "keyword", "signature": word, "summary": ""})
    if not any(e["kind"] == "function" for e in out) or not any(e["kind"] == "keyword" for e in out):
        raise SystemExit("spec.md: API extraction incomplete (a table or the keyword list moved)")
    for e in out:
        e.setdefault("insert", e["signature"])
    # merge the forms of one name (scatter(N) / scatter(P%), count(n, s) / count(s)) into one
    # entry that lists every signature and inserts the first
    uniq = {}
    for e in out:
        k = (e["name"], e["kind"])
        if k in uniq:
            uniq[k]["signature"] += ", " + e["signature"]
        else:
            uniq[k] = e
    return sorted(uniq.values(), key=lambda e: (e["kind"], e["name"]))


def reference_sections(md: str) -> dict:
    """The sections of docs/reference.md: name -> {summary, doc, related[, kind, signature]}.

    doc is a list of blocks, {"p": text}, {"ul": [text]} or {"code": text}, whose text keeps
    its inline markdown (`code`, **bold**, *italic*) for the page to render.
    """
    body = md.split("<!-- entries -->", 1)
    if len(body) != 2:
        raise SystemExit("reference.md: the '<!-- entries -->' marker is missing")
    sections = {}
    for chunk in re.split(r"^## ", body[1], flags=re.M)[1:]:
        head, _, text = chunk.partition("\n")
        m = re.fullmatch(r"(\w+)(?: \((\w+): (.+)\))?", head.strip())
        if not m:
            raise SystemExit(f"reference.md: bad heading: '## {head.strip()}'")
        name = m.group(1)
        if name in sections:
            raise SystemExit(f"reference.md: '{name}' is described twice")
        # blank lines separate blocks, except inside a fenced example
        raw, cur, fenced = [], [], False
        for ln in text.strip().splitlines():
            if ln.startswith("```"):
                fenced = not fenced
            if not ln.strip() and not fenced:
                if cur:
                    raw.append("\n".join(cur))
                cur = []
            else:
                cur.append(ln)
        if fenced:
            raise SystemExit(f"reference.md: unclosed example in '{name}'")
        if cur:
            raw.append("\n".join(cur))
        blocks, related = [], []
        for block in raw:
            if block.startswith("```"):
                blocks.append({"code": re.sub(r"^```\w*\n|\n```$", "", block)})
            elif block.startswith("See also:"):
                related = [r.strip() for r in block[len("See also:"):].split(",") if r.strip()]
            elif block.startswith("- "):
                blocks.append({"ul": [re.sub(r"\s+", " ", item).strip()
                                      for item in re.split(r"^- ", block, flags=re.M) if item.strip()]})
            else:
                blocks.append({"p": re.sub(r"\s+", " ", block)})
        if not blocks or "p" not in blocks[0]:
            raise SystemExit(f"reference.md: '{name}' must start with a one-paragraph summary")
        sec = {"summary": blocks[0]["p"], "doc": blocks[1:], "related": related}
        if m.group(2):
            sec.update(kind=m.group(2), signature=m.group(3))
        sections[name] = sec
    return sections


def with_docs(entries: list, sections: dict) -> list:
    """Merge the user-facing reference into the spec's entries; fail on any gap or stray name."""
    names = {e["name"] for e in entries}
    missing = sorted(names - sections.keys())
    if missing:
        raise SystemExit(f"reference.md: no section for: {', '.join(missing)}")
    for name, sec in sections.items():
        if name not in names and "kind" not in sec:
            raise SystemExit(f"reference.md: '{name}' is not in the spec; declare it as "
                             f"'## {name} (kind: signature)' to add it")
        if name in names and "kind" in sec:
            raise SystemExit(f"reference.md: '{name}' is in the spec; drop the (kind: signature)")
    out = []
    for e in entries:
        sec = sections[e["name"]]
        e = {**e, "summary": sec["summary"], "doc": sec["doc"], "related": sec["related"]}
        e.pop("detail", None)
        out.append(e)
    for name, sec in sections.items():
        if name not in names:
            out.append({"name": name, "kind": sec["kind"], "signature": sec["signature"],
                        # an attribute inserts its `all` form where it has one: the usual choice
                        "insert": re.sub(r"=.*", "=all", sec["signature"]) if "|all" in sec["signature"]
                        else sec["signature"].split("|")[0],
                        "summary": sec["summary"], "doc": sec["doc"], "related": sec["related"]})
    known = {e["name"] for e in out}
    for e in out:
        stray = [r for r in e["related"] if r not in known]
        if stray:
            raise SystemExit(f"reference.md: '{e['name']}' sees also unknown: {', '.join(stray)}")
    return sorted(out, key=lambda e: (e["kind"], e["name"]))


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


def version() -> str:
    """MAJOR.MINOR.PATCH from src/version.hpp, parsed the way the installer scripts do."""
    text = (ROOT / "src" / "version.hpp").read_text(encoding="utf-8")
    return ".".join(re.search(rf"#define\s+LS_VERSION_{k}\s+(\d+)", text).group(1)
                    for k in ("MAJOR", "MINOR", "PATCH"))


def main() -> None:
    samples, files = [], {}
    for group in GROUPS:
        for lvs in sorted((ROOT / "samples" / group).rglob("*.lvs")):
            src = lvs.read_text(encoding="utf-8")
            seed = re.search(r"//\s*@seed=(\d+)", src)
            file = lvs.relative_to(ROOT / "samples").as_posix()
            files[file] = src
            samples.append({"name": lvs.stem, "group": group, "file": file,
                            "seed": int(seed.group(1)) if seed else 42,
                            "about": header(src), "params": params(src),
                            "source": src.rstrip() + "\n"})
    names = [s["name"] for s in samples]
    dupes = sorted({n for n in names if names.count(n) > 1})
    if dupes:
        raise SystemExit(f"sample names must be unique across folders: {', '.join(dupes)}")

    editor = editor_data()
    api = with_docs(api_entries((ROOT / "docs" / "spec.md").read_text(encoding="utf-8")),
                    reference_sections((ROOT / "docs" / "reference.md").read_text(encoding="utf-8")))

    out = ROOT / "docs" / "js" / "samples-data.js"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("// Generated by tools/build_site_data.py - do not edit or commit.\n"
                   "window.LVS_VERSION = " + json.dumps(version()) + ";\n"
                   "window.LVS_SAMPLES = " + json.dumps(samples, separators=(",", ":")) + ";\n"
                   "window.LVS_FILES = " + json.dumps(files, separators=(",", ":")) + ";\n"
                   "window.LVS_API = " + json.dumps(api, separators=(",", ":")) + ";\n"
                   "window.LVS_EDITOR = " + json.dumps(editor, separators=(",", ":")) + ";\n",
                   encoding="utf-8")
    print(f"wrote {out.relative_to(ROOT)}: {len(samples)} samples, {len(files)} files, {len(api)} api entries")


if __name__ == "__main__":
    main()
