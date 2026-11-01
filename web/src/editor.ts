// The website's LevelScript editor: CodeMirror 6, bundled by esbuild to docs/js/cm-editor.js
// (npm run build). It exposes window.LVS_CM.create(); main.js owns inspection and running, and
// hands the editor what to draw:
//   - lexical classes come from editor-model.js (window.LVS_model.lexRuns), recomputed per edit,
//   - cell backgrounds and diagnostics are pushed in by main.js after each inspection,
//   - completion uses the VS Code extension's own classifier (extension/src/completion.ts),
//   - hover and go-to-definition read the last inspection's tokens / refs / symbols.

import { EditorState, StateEffect, StateField, RangeSetBuilder, Range, Extension } from '@codemirror/state';
import {
    EditorView, Decoration, DecorationSet, ViewPlugin, ViewUpdate, keymap, lineNumbers,
    highlightActiveLineGutter, highlightActiveLine, drawSelection, hoverTooltip, Tooltip,
} from '@codemirror/view';
import { defaultKeymap, history, historyKeymap, indentWithTab, toggleLineComment } from '@codemirror/commands';
import { indentUnit, bracketMatching, indentOnInput } from '@codemirror/language';
import {
    autocompletion, closeBrackets, closeBracketsKeymap, completionKeymap, snippetCompletion,
    Completion, CompletionContext as CMContext, CompletionResult,
} from '@codemirror/autocomplete';
import { setDiagnostics, lintGutter, Diagnostic } from '@codemirror/lint';
import { highlightSelectionMatches, searchKeymap } from '@codemirror/search';
import { completionContext, suggestions, SuggestionKind, attributeAt, ATTRIBUTE_DETAIL } from '../../extension/src/completion';

type Insp = any;   // inspect_json output (extension/src/types.ts InspectionResult)

declare global {
    interface Window { LVS_model: any; LVS_EDITOR: any; LVS_CM: any; }
}

// ---------- line comments: '//' ----------
const commentTokens = EditorState.languageData.of(() => [{ commentTokens: { line: '//' } }]);

// ---------- lexical highlighting (editor-model.js lexRuns) ----------
const markCache = new Map<string, Decoration>();
const mark = (cls: string) => {
    let d = markCache.get(cls);
    if (!d) markCache.set(cls, d = Decoration.mark({ class: cls }));
    return d;
};

function lexDecorations(doc: string): DecorationSet {
    const b = new RangeSetBuilder<Decoration>();
    for (const r of window.LVS_model.lexRuns(doc, window.LVS_EDITOR)) if (r.to > r.from) b.add(r.from, r.to, mark(r.cls));
    return b.finish();
}

const lexPlugin = ViewPlugin.fromClass(class {
    decorations: DecorationSet;
    constructor(view: EditorView) { this.decorations = lexDecorations(view.state.doc.toString()); }
    update(u: ViewUpdate) { if (u.docChanged) this.decorations = lexDecorations(u.state.doc.toString()); }
}, { decorations: v => v.decorations });

// ---------- pattern-cell backgrounds (editor-model.js cellRanges), pushed after inspection ----------
const setCells = StateEffect.define<{ from: number; to: number; color: string }[]>();
const cellField = StateField.define<DecorationSet>({
    create: () => Decoration.none,
    update(deco, tr) {
        deco = deco.map(tr.changes);
        for (const e of tr.effects) if (e.is(setCells)) {
            const len = tr.state.doc.length;
            const ranges: Range<Decoration>[] = [];
            for (const r of e.value) {
                const from = Math.min(r.from, len), to = Math.min(r.to, len);
                if (to > from) ranges.push(Decoration.mark({ attributes: { style: 'background:' + r.color }, class: 'cm-cell' }).range(from, to));
            }
            deco = Decoration.set(ranges, true);
        }
        return deco;
    },
    provide: f => EditorView.decorations.from(f),
});

// ---------- the last inspection, for hover / definition / completion ----------
const setInsp = StateEffect.define<Insp | null>();
const inspField = StateField.define<Insp | null>({
    create: () => null,
    update(v, tr) { for (const e of tr.effects) if (e.is(setInsp)) v = e.value; return v; },
});

// 1-based line / byte column -> offset. Columns are bytes in the compiler; the site's sources are
// ASCII in practice, and offsets are clamped to the line.
function offsetOf(state: EditorState, line: number, col: number): number {
    if (line < 1 || line > state.doc.lines) return -1;
    const l = state.doc.line(line);
    return Math.min(l.from + col - 1, l.to);
}
function lineColOf(state: EditorState, pos: number) {
    const l = state.doc.lineAt(pos);
    return { line: l.number, col: pos - l.from + 1 };
}
function tokenAt(list: any[] | undefined, line: number, col: number) {
    return (list || []).find(t => t.line === line && col >= t.col && col < t.col + t.len) || null;
}
function esc(s: string) {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// The declaration a position refers to (port of definitionProvider): a loc, or null.
function definitionAt(state: EditorState, pos: number, fileName: string): any {
    const insp = state.field(inspField);
    if (!insp || !insp.symbols) return null;
    const { line, col } = lineColOf(state, pos);
    const sym = insp.symbols;
    const ref = tokenAt(insp.refs, line, col);
    let loc: any = null;
    if (!ref) {
        const t = tokenAt(insp.tokens, line, col);
        if (t && t.tag >= 0 && t.value >= 0) {
            const tag = sym.tags[t.tag];
            const s = tag && (tag.values[t.value] ?? tag.masks[t.value - tag.values.length]);
            loc = s && s.loc;
        }
    } else {
        const lists: Record<string, any[]> = { rule: sym.rules, sequence: sym.sequences, layer: sym.layers, graph: sym.graphs };
        const hit = (lists[ref.kind] || []).find((s: any) => s.name === ref.target);
        loc = hit && hit.loc;
    }
    if (!loc) return null;
    return loc.module && loc.module !== fileName ? Object.assign({ other: true }, loc) : loc;
}

// Hover text (port of hoverProvider), as HTML.
function hoverHtml(state: EditorState, pos: number): string | null {
    const insp = state.field(inspField);
    if (!insp || !insp.symbols) return null;
    const { line, col } = lineColOf(state, pos);
    const sym = insp.symbols;
    const t = tokenAt(insp.tokens, line, col);
    if (t && t.tag >= 0 && t.value >= 0) {
        const tag = sym.tags[t.tag];
        if (!tag) return null;
        const isMask = t.value >= tag.values.length;
        const name = isMask ? tag.masks[t.value - tag.values.length]?.name : tag.values[t.value]?.name;
        const display = isMask ? undefined : (insp.directives || []).find((d: any) =>
            d.key === 'name' && d.target.owner === tag.name && d.target.name === name)?.value;
        const list = [...tag.values, ...tag.masks].map((v: any, i: number) => i === t.value ? '<b>' + esc(v.name) + '</b>' : esc(v.name)).join(', ');
        return '<div><b>' + esc(name) + '</b>' + (display ? ' (' + esc(display) + ')' : '') +
            ' — <i>' + (isMask ? 'named mask in ' : '') + esc(tag.name) + '</i></div>' +
            '<code>tag ' + esc(tag.name) + ' { ' + list + ' }</code>';
    }
    const ref = tokenAt(insp.refs, line, col);
    if (!ref) {   // a node attribute: `size` in `n.size` / `map.size`
        const attr = attributeAt(state.doc.toString(), pos);
        return attr ? '<b>' + attr + '</b> — ' + esc(ATTRIBUTE_DETAIL[attr]) : null;
    }
    if (ref.kind === 'layer') {
        const l = sym.layers.find((x: any) => x.name === ref.target);
        return l ? '<b>' + esc(l.name) + '</b>: grid of <code>' + esc(l.type) + '</code>' : null;
    }
    if (ref.kind === 'graph') {
        const g = (sym.graphs || []).find((x: any) => x.name === ref.target);
        return g ? '<b>' + esc(g.name) + '</b>: graph of <code>' + esc(g.source) + '</code>' : null;
    }
    if (ref.kind === 'rule' || ref.kind === 'sequence') return '<b>' + ref.kind + '</b> <code>' + esc(ref.target) + '</code>';
    return null;
}

// ---------- completion (port of completionProvider) ----------
// VS Code snippet bodies -> CodeMirror: `$0` -> `${0}`, a choice `${1|a,b|}` -> its first option.
function cmSnippet(body: string) {
    return body.replace(/\$\{(\d+)\|([^,|}]*)[^}]*\|\}/g, '${$1:$2}').replace(/\$(\d+)/g, '${$1}');
}

// The shared suggestions (extension/src/completion.ts), as CodeMirror completions.
const CM_TYPES: Record<SuggestionKind, string> = {
    keyword: 'keyword', function: 'function', enum: 'enum', enumMember: 'enum', class: 'class',
    interface: 'interface', variable: 'variable', constant: 'constant', property: 'property',
    method: 'method', module: 'namespace', type: 'type',
};

function completionsAt(state: EditorState, pos: number): Completion[] | null {
    const ctx = completionContext(state.doc.toString(), pos);
    if (ctx.kind === 'none') return null;
    const sym = state.field(inspField)?.symbols;
    return suggestions(ctx, sym).map(s => {
        const info = { label: s.label, type: CM_TYPES[s.kind], detail: s.detail };
        return s.snippet ? snippetCompletion(cmSnippet(s.snippet), info) : info;
    });
}

function lvsCompletion(context: CMContext): CompletionResult | null {
    const w = context.matchBefore(/\w*/);
    // only on a word being typed, right after `n.`, or when asked (Ctrl-Space):
    // never on a bare space or newline
    const member = /\w\.$/.test(context.state.sliceDoc(Math.max(0, context.pos - 2), context.pos));
    if (!w || (w.from === w.to && !context.explicit && !member)) return null;
    const options = completionsAt(context.state, context.pos);
    if (!options || !options.length) return null;
    return { from: w.from, options, validFor: /^\w*$/ };
}

// ---------- the editor ----------
export interface EditorOptions {
    parent: HTMLElement;
    doc: string;
    fileName: string;
    onChange?: (src: string) => void;      // after the edit is applied (not inside the update)
    onCursor?: (pos: number) => void;
    label?: string;
}

function create(o: EditorOptions) {
    let view: EditorView;
    const gotoDefinition = (pos: number) => {
        const loc = definitionAt(view.state, pos, o.fileName);
        if (!loc || loc.other) return false;
        const at = offsetOf(view.state, loc.line, loc.col);
        if (at < 0) return false;
        view.dispatch({ selection: { anchor: at, head: Math.min(at + loc.len, view.state.doc.length) }, scrollIntoView: true });
        view.focus();
        return true;
    };

    const extensions: Extension[] = [
        lineNumbers(),
        highlightActiveLineGutter(),
        highlightActiveLine(),
        history(),
        drawSelection(),
        indentOnInput(),
        bracketMatching(),
        closeBrackets(),
        highlightSelectionMatches(),
        indentUnit.of('    '),
        EditorState.tabSize.of(4),
        commentTokens,
        lexPlugin,
        cellField,
        inspField,
        lintGutter(),
        autocompletion({ override: [lvsCompletion], icons: false }),
        hoverTooltip((v, pos): Tooltip | null => {
            const html = hoverHtml(v.state, pos);
            if (!html) return null;
            return { pos, above: true, create: () => { const dom = document.createElement('div'); dom.className = 'cm-lvs-hover'; dom.innerHTML = html; return { dom }; } };
        }),
        keymap.of([
            ...closeBracketsKeymap, ...completionKeymap, ...searchKeymap, ...historyKeymap,
            { key: 'Mod-/', run: toggleLineComment },
            { key: 'F12', run: v => gotoDefinition(v.state.selection.main.head) },
            indentWithTab,
            ...defaultKeymap,
        ]),
        // Cmd/Ctrl-click: go to the definition
        EditorView.domEventHandlers({
            mousedown(e, v) {
                if (!(e.metaKey || e.ctrlKey) || e.button !== 0) return false;
                const pos = v.posAtCoords({ x: e.clientX, y: e.clientY });
                if (pos == null) return false;
                if (gotoDefinition(pos)) { e.preventDefault(); return true; }
                return false;
            },
        }),
        EditorView.contentAttributes.of({ 'aria-label': o.label || 'LevelScript source', spellcheck: 'false' }),
        EditorView.updateListener.of(u => {
            if (u.docChanged && o.onChange) { const src = u.state.doc.toString(); queueMicrotask(() => o.onChange!(src)); }
            if ((u.selectionSet || u.docChanged) && o.onCursor) { const p = u.state.selection.main.head; queueMicrotask(() => o.onCursor!(p)); }
        }),
    ];

    view = new EditorView({ parent: o.parent, state: EditorState.create({ doc: o.doc, extensions }) });

    return {
        view,
        value: () => view.state.doc.toString(),
        cursor: () => view.state.selection.main.head,
        // another document: fresh undo history, cursor at the top. No onChange (nothing was edited).
        reset(src: string) {
            view.setState(EditorState.create({ doc: src, extensions }));
            view.scrollDOM.scrollTop = 0;
        },
        // replace the whole text as one undoable edit
        setValue(src: string) {
            view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: src }, selection: { anchor: 0 }, scrollIntoView: true });
        },
        insert(text: string) {
            const at = view.state.selection.main.head;
            view.dispatch({ changes: { from: at, insert: text }, selection: { anchor: at + text.length }, scrollIntoView: true });
            view.focus();
        },
        goTo(at: number) {
            at = Math.min(at, view.state.doc.length);
            view.dispatch({ selection: { anchor: at }, effects: EditorView.scrollIntoView(at, { y: 'center' }) });
            view.focus();
        },
        // after an inspection: cell backgrounds, diagnostics (lint), and the data for hover etc.
        // diags: [{from, to, severity, message}] in this file.
        paint(insp: Insp | null, cells: { from: number; to: number; color: string }[], diags: Diagnostic[]) {
            const len = view.state.doc.length;
            const list = diags.map(d => ({ ...d, from: Math.min(d.from, len), to: Math.min(d.to, len) }));
            view.dispatch(setDiagnostics(view.state, list), { effects: [setCells.of(cells), setInsp.of(insp)] });
        },
        focus: () => view.focus(),
        destroy: () => view.destroy(),
    };
}

window.LVS_CM = { create };
