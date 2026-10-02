import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';

// Module resolver for `use "path"` (spec section 2.6): a path is relative to
// the using module's directory. The canonical name is the absolute file path,
// so diagnostics and symbol locations can be opened directly. An open editor
// buffer wins over the file on disk, so unsaved edits are seen at once.
export type ModuleSource = { name: string; source: string };
export type Resolver = (usePath: string, from: string) => ModuleSource | null;

export function editorResolver(): Resolver {
    return (usePath, from) => {
        const full = path.resolve(path.dirname(from), usePath);
        const open = vscode.workspace.textDocuments.find(d => d.uri.fsPath === full);
        if (open) return { name: full, source: open.getText() };
        try {
            return { name: full, source: fs.readFileSync(full, 'utf8') };
        } catch {
            return null;
        }
    };
}
