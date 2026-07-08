import * as vscode from 'vscode';
import { getCached } from './cache';

export const definitionProvider: vscode.DefinitionProvider = {
    provideDefinition(doc, pos, token) {
        return null; // TODO
    }
};

export const hoverProvider: vscode.HoverProvider = {
    provideHover(doc, pos, token) {
        return null; // TODO
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
                items.push(new vscode.CompletionItem(v, vscode.CompletionItemKind.EnumMember));
            });
            tag.unions.forEach(u => {
                items.push(new vscode.CompletionItem(u, vscode.CompletionItemKind.Enum));
            });
        });

        cached.symbols.layers.forEach(l => {
            items.push(new vscode.CompletionItem(l.name, vscode.CompletionItemKind.Class));
        });

        return items;
    }
};
