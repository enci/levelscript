import * as vscode from 'vscode';
import { getCached } from './cache';

function findTokenAt(tokens: any[], line: number, col: number) {
    if (!tokens) return null;
    for (const t of tokens) {
        if (t.line === line && col >= t.col && col < t.col + t.len) {
            return t;
        }
    }
    return null;
}

function spanToLocation(uri: vscode.Uri, loc: { line: number, col: number, len: number }): vscode.Location {
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
                    const sym = tag.values[t.value] ?? tag.unions[t.value - tag.values.length];
                    if (sym && sym.loc) return spanToLocation(doc.uri, sym.loc);
                }
            }
            return null;
        }

        if (ref.kind === 'rule') {
            const rule = cached.symbols.rules.find(r => r.name === ref.target);
            if (rule && rule.loc) return spanToLocation(doc.uri, rule.loc);
        } else if (ref.kind === 'layer') {
            const layer = cached.symbols.layers.find(l => l.name === ref.target);
            if (layer && layer.loc) return spanToLocation(doc.uri, layer.loc);
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
                const isUnion = t.value >= tag.values.length;
                const name = isUnion ? tag.unions[t.value - tag.values.length]?.name
                                     : tag.values[t.value]?.name;
                const md = new vscode.MarkdownString();
                md.appendMarkdown(`**${name}** — *${isUnion ? 'union in ' : ''}${tag.name}*\n\n`);

                const valueList = [...tag.values, ...tag.unions]
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
                } else if (ref.kind === 'rule') {
                    md.appendMarkdown(`**rule** \`${ref.target}\``);
                    return new vscode.Hover(md);
                }
            }
        }

        return null;
    }
};

export const completionProvider: vscode.CompletionItemProvider = {
    provideCompletionItems(doc, pos, token, context) {
        const cached = getCached(doc.uri.toString());
        if (!cached || !cached.ok) return null;

        const items: vscode.CompletionItem[] = [];
        
        // simple keyword completions 
        cached.symbols.ops.forEach(op => {
            items.push(new vscode.CompletionItem(op, vscode.CompletionItemKind.Function));
        });
        cached.symbols.builtins.forEach(bi => {
            items.push(new vscode.CompletionItem(bi, vscode.CompletionItemKind.Function));
        });

        // inside patterns, suggest values. We can just throw all values for now
        cached.symbols.tags.forEach(tag => {
            tag.values.forEach(v => {
                items.push(new vscode.CompletionItem(v.name, vscode.CompletionItemKind.EnumMember));
            });
            tag.unions.forEach(u => {
                items.push(new vscode.CompletionItem(u.name, vscode.CompletionItemKind.Enum));
            });
        });

        cached.symbols.layers.forEach(l => {
            items.push(new vscode.CompletionItem(l.name, vscode.CompletionItemKind.Class));
        });

        return items;
    }
};
