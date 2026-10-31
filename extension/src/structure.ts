import * as vscode from 'vscode';
import { getCached } from './cache';
import { OutlineSymbol, SourceRange } from './types';

// Outline, folding and expand-selection, all from the node ranges the compiler
// reports for the file.

function toRange(r: SourceRange): vscode.Range {
    return new vscode.Range(r.line - 1, r.col - 1, r.end_line - 1, r.end_col - 1);
}

const KINDS: Record<OutlineSymbol['kind'], vscode.SymbolKind> = {
    tag: vscode.SymbolKind.Enum,
    value: vscode.SymbolKind.EnumMember,
    mask: vscode.SymbolKind.EnumMember,
    layers: vscode.SymbolKind.Struct,
    grid: vscode.SymbolKind.Field,
    graph: vscode.SymbolKind.Interface,
    params: vscode.SymbolKind.Struct,
    param: vscode.SymbolKind.Variable,
    rule: vscode.SymbolKind.Function,
    sequence: vscode.SymbolKind.Module,
};

function toSymbol(s: OutlineSymbol): vscode.DocumentSymbol {
    const sym = new vscode.DocumentSymbol(s.name, '', KINDS[s.kind], toRange(s.range), toRange(s.select));
    sym.children = (s.children ?? []).map(toSymbol);
    return sym;
}

export const documentSymbolProvider: vscode.DocumentSymbolProvider = {
    provideDocumentSymbols(doc) {
        return (getCached(doc.uri.toString())?.outline ?? []).map(toSymbol);
    }
};

// A node spanning lines folds up to its last line, which stays visible - the
// closing '}' or ']'. Nodes starting on one line fold as the largest of them.
export const foldingRangeProvider: vscode.FoldingRangeProvider = {
    provideFoldingRanges(doc) {
        const ends = new Map<number, number>();   // 0-based start line -> last hidden line
        for (const s of getCached(doc.uri.toString())?.spans ?? []) {
            const start = s.line - 1, hidden = s.end_line - 2;
            if (hidden > start) ends.set(start, Math.max(ends.get(start) ?? 0, hidden));
        }
        return [...ends].map(([start, end]) => new vscode.FoldingRange(start, end));
    }
};

// Each expansion selects the next enclosing node: a cell's expression, the
// cell, its pattern, the sub-rule, the rule.
export const selectionRangeProvider: vscode.SelectionRangeProvider = {
    provideSelectionRanges(doc, positions) {
        const size = (r: vscode.Range) => doc.offsetAt(r.end) - doc.offsetAt(r.start);
        const spans = (getCached(doc.uri.toString())?.spans ?? []).map(toRange);
        return positions.map(pos => {
            const around = spans.filter(r => r.contains(pos)).sort((a, b) => size(b) - size(a));
            const word = doc.getWordRangeAtPosition(pos);
            if (word) around.push(word);
            let sel: vscode.SelectionRange | undefined;   // outermost first, each the next's parent
            for (const r of around)
                if (!sel || (sel.range.contains(r) && !sel.range.isEqual(r)))
                    sel = new vscode.SelectionRange(r, sel);
            return sel ?? new vscode.SelectionRange(new vscode.Range(pos, pos));
        });
    }
};
