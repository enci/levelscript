import * as vscode from 'vscode';
import { getCached } from './cache';
import { completionContext, OPS } from './completion';
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
        } else if (ref.kind === 'sequence') {
            const seq = cached.symbols.sequences?.find(s => s.name === ref.target);
            if (seq && seq.loc) return spanToLocation(doc.uri, seq.loc);
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
                } else if (ref.kind === 'sequence') {
                    md.appendMarkdown(`**sequence** \`${ref.target}\``);
                    return new vscode.Hover(md);
                }
            }
        }

        return null;
    }
};

// Context-aware completion: only what the grammar allows at the cursor
// (see completion.ts). Nothing at all where a name is being declared or the
// context is unclear - Enter must never land on a stray suggestion.
export const completionProvider: vscode.CompletionItemProvider = {
    provideCompletionItems(doc, pos) {
        const ctx = completionContext(doc.getText(), doc.offsetAt(pos));
        if (ctx.kind === 'none') return undefined;
        // best-effort symbols: a mid-edit file still has its tables
        const sym = getCached(doc.uri.toString())?.symbols;
        const K = vscode.CompletionItemKind;
        const items: vscode.CompletionItem[] = [];

        const word = (label: string, kind: vscode.CompletionItemKind, detail?: string) => {
            const it = new vscode.CompletionItem(label, kind);
            if (detail) it.detail = detail;
            items.push(it);
            return it;
        };
        const snippet = (label: string, body: string, kind = K.Snippet, detail?: string) => {
            const it = word(label, kind, detail);
            it.insertText = new vscode.SnippetString(body);
            return it;
        };
        const tagOf = (grid: string) => {
            const layer = sym?.layers.find(l => l.name === grid);
            return layer ? sym?.tags.find(t => t.name === layer.type) : undefined;
        };
        const tagValues = (tag: InspectionResult['symbols']['tags'][number] | undefined) => {
            tag?.values.forEach(v => word(v.name, K.EnumMember, tag.name));
            tag?.unions.forEach(u => word(u.name, K.Enum, `union in ${tag.name}`));
        };
        const allTagValues = () => sym?.tags.forEach(t => tagValues(t));
        const grids = () => sym?.layers.forEach(l => word(l.name, K.Class, `grid of ${l.type}`));

        switch (ctx.kind) {
        case 'top':
            snippet('tag', 'tag ${1:name} { ${0} }', K.Keyword);
            snippet('layers', 'layers {\n\t$0\n}', K.Keyword);
            snippet('params', 'params {\n\t$0\n}', K.Keyword);
            snippet('rule', 'rule ${1:name} {\n\t$0\n}', K.Keyword);
            snippet('sequence', 'sequence ${1:name} {\n\t$0\n}', K.Keyword);
            snippet('program', 'program {\n\t$0\n}', K.Keyword);
            break;
        case 'unionMember':
            sym?.tags.find(t => t.name === ctx.tag)?.values.forEach(v => word(v.name, K.EnumMember));
            break;
        case 'gridOf':
            snippet('grid of', 'grid of ${1}', K.Keyword);
            break;
        case 'of':
            word('of', K.Keyword);
            break;
        case 'gridType':
            sym?.tags.forEach(t => word(t.name, K.Enum, 'tag'));
            word('number', K.Keyword);
            break;
        case 'paramType':
            snippet('number', 'number = ${1:0}', K.Keyword);
            break;
        case 'expr':
            sym?.builtins.forEach(b => snippet(b, `${b}(\${1})`, K.Function, 'built-in'));
            sym?.params.forEach(p => word(p.name, K.Variable, p.derived ? 'derived param' : 'param'));
            if (ctx.grids) grids();
            if (ctx.pos) ['x', 'y', 'width', 'height'].forEach(n => word(n, K.Constant, 'position'));
            allTagValues();
            break;
        case 'ruleAttr':
            snippet('symmetry', 'symmetry=${1|horizontal,vertical,all,none|}', K.Property);
            snippet('rotation', 'rotation=${1|all,90,180,270,none|}', K.Property);
            break;
        case 'attrValue':
            if (ctx.attr === 'symmetry') ['horizontal', 'vertical', 'all', 'none'].forEach(v => word(v, K.EnumMember));
            if (ctx.attr === 'rotation') ['all', '90', '180', '270', 'none'].forEach(v => word(v, K.EnumMember));
            break;
        case 'ruleBody':
            if (ctx.start) {
                word('any', K.Keyword, 'pick one sub-rule per match');
                word('all', K.Keyword, 'apply every sub-rule');
                word('ordered', K.Keyword, 'sub-rules in priority order');
            }
            grids();
            word('where', K.Keyword, 'position pseudo-layer');
            break;
        case 'combinator':
            word('all', K.Keyword);
            word('any', K.Keyword);
            break;
        case 'weight':
            snippet('weight', 'weight=${1:1}', K.Property);
            break;
        case 'cell':
            tagValues(tagOf(ctx.grid));
            break;
        case 'statement':
            snippet('one', 'one ${1:rule}', K.Keyword, 'apply once');
            snippet('all', 'all ${1:rule}', K.Keyword, 'apply to every match');
            snippet('some', 'some(max=${1:1}) ${2:rule}', K.Keyword, 'apply up to N times');
            OPS.forEach(op => snippet(op.name, op.snippet, K.Function, 'operation'));
            if (ctx.guard) snippet('when', 'when (${1})', K.Keyword, 'guard');
            break;
        case 'ruleName':
            sym?.rules.forEach(r => word(r.name, K.Method, 'rule'));
            sym?.sequences?.forEach(s => word(s.name, K.Module, 'sequence'));
            break;
        case 'strategyArg':
            // the target is not typed yet: offer everything a rule takes
            if (ctx.strategy === 'some') {
                snippet('max', 'max=${1:1}', K.Property);
                snippet('percent', 'percent=${1:50}', K.Property);
            }
            snippet('policy', 'policy=${1|snapshot,incremental,stabilize|}', K.Property);
            break;
        case 'policyValue':
            ['snapshot', 'incremental', 'stabilize'].forEach(v => word(v, K.EnumMember));
            break;
        case 'opArg': {
            const op = OPS.find(o => o.name === ctx.op);
            if (!op) break;
            const positional = op.params.filter(p => p.positional);
            const next = ctx.used.length === 0 ? positional[ctx.index] : undefined;
            if (next?.kind === 'enum') next.values!.forEach(v => word(v, K.EnumMember));
            for (const p of op.params)
                if (!p.positional && !ctx.used.includes(p.name))
                    snippet(p.name, `${p.name}=\${1}`, K.Property, p.required ? p.kind : `${p.kind}, optional`);
            break;
        }
        case 'opValue': {
            const p = OPS.find(o => o.name === ctx.op)?.params.find(q => q.name === ctx.param);
            if (p?.kind === 'grid') grids();
            else if (p?.kind === 'pred' || p?.kind === 'value') allTagValues();
            else if (p?.kind === 'enum') p.values!.forEach(v => word(v, K.EnumMember));
            break;
        }
        }
        return items;
    }
};
