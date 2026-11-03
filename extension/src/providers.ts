import * as vscode from 'vscode';
import { getCached } from './cache';
import { completionContext, suggestions, SuggestionKind, attributeAt, ATTRIBUTE_DETAIL } from './completion';
import { InspectionResult } from './types';

function findTokenAt(tokens: any[], line: number, col: number) {
    if (!tokens) return null;
    for (const t of tokens) {
        if (t.line === line && col >= t.col && col < t.col + t.len) {
            return t;
        }
    }
    return null;
}

// A symbol may be declared in another module of the closure; its loc then names
// that module's file.
function spanToLocation(uri: vscode.Uri, loc: { line: number, col: number, len: number, module?: string }): vscode.Location {
    if (loc.module && loc.module !== uri.fsPath) uri = vscode.Uri.file(loc.module);
    const start = new vscode.Position(loc.line - 1, loc.col - 1);
    const end = new vscode.Position(loc.line - 1, loc.col - 1 + loc.len);
    return new vscode.Location(uri, new vscode.Range(start, end));
}

export const definitionProvider: vscode.DefinitionProvider = {
    provideDefinition(doc, pos) {
        const cached = getCached(doc.uri.toString());
        if (!cached || !cached.refs) return null;

        const line = pos.line + 1;
        const col = pos.character + 1;

        const ref = findTokenAt(cached.refs, line, col);
        if (!ref) {
            // Also check if we are hovering over a token that could be a tag value definition
            const t = findTokenAt(cached.tokens, line, col);
            if (t && t.tag >= 0 && t.value >= 0) {
                const tag = cached.symbols.tags[t.tag];
                if (tag) {
                    const sym = tag.values[t.value] ?? tag.masks[t.value - tag.values.length];
                    if (sym && sym.loc) return spanToLocation(doc.uri, sym.loc);
                }
            }
            return null;
        }

        if (ref.kind === 'rule') {
            const rule = cached.symbols.rules.find(r => r.name === ref.target);
            if (rule && rule.loc) return spanToLocation(doc.uri, rule.loc);
        } else if (ref.kind === 'sequence') {
            const seq = cached.symbols.sequences?.find(s => s.name === ref.target);
            if (seq && seq.loc) return spanToLocation(doc.uri, seq.loc);
        } else if (ref.kind === 'layer') {
            const layer = cached.symbols.layers.find(l => l.name === ref.target);
            if (layer && layer.loc) return spanToLocation(doc.uri, layer.loc);
        } else if (ref.kind === 'graph') {
            const graph = cached.symbols.graphs?.find(g => g.name === ref.target);
            if (graph && graph.loc) return spanToLocation(doc.uri, graph.loc);
        }

        return null;
    }
};

export const hoverProvider: vscode.HoverProvider = {
    provideHover(doc, pos, token) {
        const cached = getCached(doc.uri.toString());
        if (!cached || !cached.ok) return null;

        const line = pos.line + 1;
        const col = pos.character + 1;

        // Check if hovering over a tag value (token)
        const t = findTokenAt(cached.tokens, line, col);
        if (t && t.tag >= 0 && t.value >= 0) {
            const tag = cached.symbols.tags[t.tag];
            if (tag) {
                const isMask = t.value >= tag.values.length;
                const name = isMask ? tag.masks[t.value - tag.values.length]?.name
                                     : tag.values[t.value]?.name;
                const md = new vscode.MarkdownString();
                const display = isMask ? undefined : cached.directives?.find(d =>
                    d.key === 'name' && d.target.owner === tag.name && d.target.name === name)?.value;
                md.appendMarkdown(`**${name}**${display ? ` (${display})` : ''} — *${isMask ? 'named mask in ' : ''}${tag.name}*\n\n`);

                const valueList = [...tag.values, ...tag.masks]
                    .map((v, i) => i === t.value ? `**${v.name}**` : v.name).join(', ');
                md.appendMarkdown(`tag ${tag.name} { ${valueList} }`);
                return new vscode.Hover(md);
            }
        }

        // Check if hovering over a layer or rule reference
        if (cached.refs) {
            const ref = findTokenAt(cached.refs, line, col);
            if (ref) {
                const md = new vscode.MarkdownString();
                if (ref.kind === 'layer') {
                    const layer = cached.symbols.layers.find(l => l.name === ref.target);
                    if (layer) {
                        const type = layer.type === 'number' ? 'number' : layer.type;
                        md.appendMarkdown(`**${layer.name}**: grid of \`${type}\``);
                        return new vscode.Hover(md);
                    }
                } else if (ref.kind === 'graph') {
                    const graph = cached.symbols.graphs?.find(g => g.name === ref.target);
                    if (graph) {
                        md.appendMarkdown(`**${graph.name}**: graph of \`${graph.source}\``);
                        return new vscode.Hover(md);
                    }
                } else if (ref.kind === 'rule') {
                    md.appendMarkdown(`**rule** \`${ref.target}\``);
                    return new vscode.Hover(md);
                } else if (ref.kind === 'sequence') {
                    md.appendMarkdown(`**sequence** \`${ref.target}\``);
                    return new vscode.Hover(md);
                }
            }
        }

        // a node attribute: `size` in `n.size` / `map.size`
        const attr = attributeAt(doc.getText(), doc.offsetAt(pos));
        if (attr) return new vscode.Hover(new vscode.MarkdownString(`**${attr}** — ${ATTRIBUTE_DETAIL[attr]}`));

        return null;
    }
};

// Context-aware completion: only what the grammar allows at the cursor
// (see completion.ts, which also decides what to offer). Nothing at all where
// a name is being declared or the context is unclear - Enter must never land
// on a stray suggestion.
const KINDS: Record<SuggestionKind, vscode.CompletionItemKind> = {
    keyword: vscode.CompletionItemKind.Keyword, function: vscode.CompletionItemKind.Function,
    enum: vscode.CompletionItemKind.Enum, enumMember: vscode.CompletionItemKind.EnumMember,
    class: vscode.CompletionItemKind.Class, interface: vscode.CompletionItemKind.Interface,
    variable: vscode.CompletionItemKind.Variable, constant: vscode.CompletionItemKind.Constant,
    property: vscode.CompletionItemKind.Property, method: vscode.CompletionItemKind.Method,
    module: vscode.CompletionItemKind.Module, type: vscode.CompletionItemKind.Enum,
};

export const completionProvider: vscode.CompletionItemProvider = {
    provideCompletionItems(doc, pos, _token, context) {
        const ctx = completionContext(doc.getText(), doc.offsetAt(pos));
        if (ctx.kind === 'none') return undefined;
        // a typed '.' is the empty cell unless it follows a name: `n.`, `map.`
        if (context.triggerCharacter === '.' && ctx.kind !== 'member') return undefined;
        // best-effort symbols: a mid-edit file still has its tables
        const sym = getCached(doc.uri.toString())?.symbols;
        return suggestions(ctx, sym).map(s => {
            const it = new vscode.CompletionItem(s.label, KINDS[s.kind]);
            if (s.detail) it.detail = s.detail;
            if (s.snippet) it.insertText = new vscode.SnippetString(s.snippet);
            return it;
        });
    }
};
