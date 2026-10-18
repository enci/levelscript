// What the site's editor draws over the source: lexical classes, pattern-cell backgrounds and
// diagnostics. Pure functions from (source, inspection result) to ranges; no DOM, so a later
// switch to CodeMirror can reuse them. See docs/web-editor-spec.md.
//
// Offsets are UTF-16 code units into the source. The compiler reports 1-based lines and
// byte columns (tab and \r count 1), so every position goes through `offsetOf`.

(function (root) {
  'use strict';

  const LEX = ['', 'tk-cm', 'tk-str', 'tk-num', 'tk-kw', 'tk-fn', 'tk-op', 'tk-var'];
  const SEVERITY = { warning: 1, error: 2 };

  // ---------- positions ----------

  function indexLines(src) {
    const starts = [0];
    for (let i = 0; i < src.length; i++) if (src.charCodeAt(i) === 10) starts.push(i + 1);
    return { src, starts, ascii: [] };
  }

  // Offset of the 0-based byte column `col0` on 1-based `line`, clamped to the end of the line.
  function offsetOf(ix, line, col0) {
    const { src, starts } = ix;
    if (line < 1) return 0;
    if (line > starts.length) return src.length;
    const ls = starts[line - 1];
    const le = line < starts.length ? starts[line] - 1 : src.length;
    if (ix.ascii[line] === undefined) ix.ascii[line] = !/[^\x00-\x7f]/.test(src.slice(ls, le));
    if (ix.ascii[line]) return Math.min(ls + col0, le);
    let bytes = 0, i = ls;
    while (i < le && bytes < col0) {
      const cp = src.codePointAt(i);
      const size = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
      if (bytes + size > col0) break;   // a column inside a character belongs to that character
      bytes += size;
      i += cp > 0xffff ? 2 : 1;
    }
    return i;
  }

  // ---------- lexical classes ----------

  const TOKEN = /(\/\/[^\n]*)|("[^"\n]*"?)|(=>|==|!=|<=|>=|&&|\|\||[+\-*/|!<>=%])|(\d+%?)|(\?[A-Za-z_]\w*)|([A-Za-z_]\w*)|([\s\S])/g;

  // vocab: { keywords: [...], builtins: [...] } (window.LVS_EDITOR). Returns [{from, to, cls}].
  function lexRuns(src, vocab) {
    const kw = new Set(vocab && vocab.keywords || []);
    const fn = new Set(vocab && vocab.builtins || []);
    const runs = [];
    let brackets = 0, parens = 0;
    let m;
    TOKEN.lastIndex = 0;
    while ((m = TOKEN.exec(src))) {
      const from = m.index, to = from + m[0].length;
      if (m[1]) runs.push({ from, to, cls: 'tk-cm' });
      else if (m[2]) runs.push({ from, to, cls: 'tk-str' });
      else if (m[3]) {
        // `*` is the any-wildcard in a pattern row and multiplication inside ( ) or outside one
        if (m[3] === '*' && brackets > 0 && parens === 0) continue;
        runs.push({ from, to, cls: 'tk-op' });
      } else if (m[4]) runs.push({ from, to, cls: 'tk-num' });
      else if (m[5]) runs.push({ from, to, cls: 'tk-var' });
      else if (m[6]) {
        // the grammar's lookahead is (?=\s*\(): whitespace before the paren is allowed
        if (kw.has(m[6])) runs.push({ from, to, cls: 'tk-kw' });
        else if (fn.has(m[6]) && /^\s*\(/.test(src.slice(to, to + 64))) runs.push({ from, to, cls: 'tk-fn' });
      } else {
        const c = m[7];
        if (c === '[') brackets++;
        else if (c === ']') brackets = Math.max(0, brackets - 1);
        else if (c === '(') parens++;
        else if (c === ')') parens = Math.max(0, parens - 1);
      }
    }
    return runs;
  }

  // ---------- pattern-cell backgrounds ----------

  function paletteColor(editor, slot) {
    return editor.palette[((slot % 12) + 12) % 12];
  }

  // A cell's background runs across the whitespace up to the next cell on the same line, so a
  // row reads as a solid strip even when columns are aligned with several spaces. The last cell
  // of a row covers just the one whitespace character after it, never a `]` or a comment.
  // Port of applyDecorations/cellLen in extension/src/decorations.ts.
  function cellRanges(src, insp, editor, fileName) {
    const ix = indexLines(src);
    const out = [];
    const toks = (insp.tokens || []).slice().sort((a, b) => a.line - b.line || a.col - b.col);
    toks.forEach((t, k) => {
      let color;
      if (t.tag === -1) color = editor.any;
      else if (t.tag === -2) color = editor.empty;
      else if (t.tag === -3) color = paletteColor(editor, t.value);
      else color = paletteColor(editor, t.value + t.tag * 12);
      const from = offsetOf(ix, t.line, t.col - 1);
      const end = offsetOf(ix, t.line, t.col - 1 + t.len);
      let to = end;
      const next = toks[k + 1];
      if (next && next.line === t.line) {
        const ns = offsetOf(ix, next.line, next.col - 1);
        if (ns > end && /^[ \t]+$/.test(src.slice(end, ns))) to = ns;
        else if (src[end] === ' ' || src[end] === '\t') to = end + 1;
      } else if (src[end] === ' ' || src[end] === '\t') to = end + 1;
      if (to > from) out.push({ from, to, color });
    });

    // the tag declarations are the legend: same palette slots, only the ones in this file
    const here = loc => !!loc && (!loc.module || loc.module === fileName);
    const legend = (loc, slot) => {
      const from = offsetOf(ix, loc.line, loc.col - 1);
      const to = offsetOf(ix, loc.line, loc.col - 1 + loc.len);
      if (to > from) out.push({ from, to, color: paletteColor(editor, slot) });
    };
    ((insp.symbols && insp.symbols.tags) || []).forEach((tag, ti) => {
      tag.values.forEach((v, vi) => { if (here(v.loc)) legend(v.loc, vi + ti * 12); });
      tag.unions.forEach((u, ui) => { if (here(u.loc)) legend(u.loc, tag.values.length + ui + ti * 12); });
    });
    return out;
  }

  // ---------- diagnostics ----------

  // ranges: underlines inside this file. points: a marker at an offset that has no character to
  // underline (end of line or file). rows: every diagnostic, for the list; `at` is the start
  // offset for those in this file, null for other modules.
  function diagnosticRanges(src, insp, fileName) {
    const ix = indexLines(src);
    const ranges = [], points = [], rows = [];
    (insp.diagnostics || []).forEach(d => {
      const sev = SEVERITY[d.severity] || 1;
      const mine = !d.module || d.module === fileName;
      const row = { line: d.line, col: d.col, message: d.message, severity: d.severity, module: mine ? null : d.module,
        at: null, from: null, to: null };
      rows.push(row);
      if (!mine) return;
      const from = offsetOf(ix, d.line, d.col - 1);
      let to = d.end_line > 0 ? offsetOf(ix, d.end_line, d.end_col - 1) : from;
      row.at = from;
      if (to <= from) {
        const c = src[from];
        if (c === undefined || c === '\n' || c === '\r') { points.push({ at: from, sev }); row.from = row.to = from; return; }
        to = from + (src.charCodeAt(from) >= 0xd800 && src.charCodeAt(from) < 0xdc00 ? 2 : 1);
      }
      ranges.push({ from, to, sev });
      row.from = from;
      row.to = to;
    });
    return { ranges, points, rows };
  }

  // ---------- rendering ----------

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // Three independent layers (lexical class, background colour, diagnostic severity) are split at
  // the union of their boundaries into flat runs: one <span> per run, never nested, so nothing
  // changes the advance of a glyph. Runs with no attributes are bare text.
  function renderHtml(src, lex, bgs, diag) {
    const n = src.length;
    const lexArr = new Uint8Array(n);
    lex.forEach(r => { const c = LEX.indexOf(r.cls); for (let i = r.from; i < r.to && i < n; i++) lexArr[i] = c; });
    const colors = [];
    const bgArr = new Int32Array(n).fill(-1);
    (bgs || []).forEach(r => {
      let ci = colors.indexOf(r.color);
      if (ci < 0) ci = colors.push(r.color) - 1;
      for (let i = r.from; i < r.to && i < n; i++) bgArr[i] = ci;
    });
    const dgArr = new Uint8Array(n);
    ((diag && diag.ranges) || []).forEach(r => { for (let i = r.from; i < r.to && i < n; i++) dgArr[i] = Math.max(dgArr[i], r.sev); });
    const marks = new Map();
    ((diag && diag.points) || []).forEach(p => marks.set(p.at, Math.max(marks.get(p.at) || 0, p.sev)));

    let out = '';
    const mark = at => {
      if (marks.has(at)) out += '<span class="dg-point ' + (marks.get(at) === 2 ? 'dg-error' : 'dg-warning') + '"></span>';
    };
    let i = 0;
    while (i < n) {
      mark(i);
      const l = lexArr[i], b = bgArr[i], d = dgArr[i];
      let j = i + 1;
      while (j < n && lexArr[j] === l && bgArr[j] === b && dgArr[j] === d && !marks.has(j)) j++;
      const text = esc(src.slice(i, j));
      if (!l && b < 0 && !d) out += text;
      else {
        const cls = [LEX[l], d ? (d === 2 ? 'dg-error' : 'dg-warning') : ''].filter(Boolean).join(' ');
        out += '<span' + (cls ? ' class="' + cls + '"' : '') + (b >= 0 ? ' style="background:' + colors[b] + '"' : '') + '>' + text + '</span>';
      }
      i = j;
    }
    mark(n);
    return out;
  }

  // Lexical highlighting only, as HTML: for static code blocks, which have no inspection.
  function highlightHtml(src, vocab) {
    return renderHtml(src, lexRuns(src, vocab), [], null);
  }

  root.LVS_model = { indexLines, offsetOf, lexRuns, cellRanges, diagnosticRanges, renderHtml, highlightHtml };
  if (typeof module !== 'undefined' && module.exports) module.exports = root.LVS_model;
})(typeof window !== 'undefined' ? window : globalThis);
