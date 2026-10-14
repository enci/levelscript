import * as vscode from 'vscode';
import { astDump } from './wasm';

// "LevelScript: Show AST": the active file's syntax tree, read-only beside it
// and refreshed as the file changes. A view for working on the language - the
// dump's format is not an interface other features build on.
const SCHEME = 'levelscript-ast';

function viewOf(doc: vscode.TextDocument): vscode.Uri {
    return vscode.Uri.from({ scheme: SCHEME, path: doc.fileName + '.ast', query: doc.uri.toString() });
}

export function registerAstView(ctx: vscode.ExtensionContext) {
    const changed = new vscode.EventEmitter<vscode.Uri>();
    const provider: vscode.TextDocumentContentProvider = {
        onDidChange: changed.event,
        provideTextDocumentContent(uri) {
            const doc = vscode.workspace.textDocuments.find(d => d.uri.toString() === uri.query);
            return doc ? astDump(doc.getText(), doc.fileName) : '';
        },
    };

    ctx.subscriptions.push(
        changed,
        vscode.workspace.registerTextDocumentContentProvider(SCHEME, provider),
        vscode.workspace.onDidChangeTextDocument(e => {
            if (e.document.languageId === 'levelscript') changed.fire(viewOf(e.document));
        }),
        vscode.commands.registerCommand('levelscript.showAst', async () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'levelscript') {
                vscode.window.showWarningMessage('Open a .lvs file to see its AST.');
                return;
            }
            const view = await vscode.workspace.openTextDocument(viewOf(editor.document));
            await vscode.window.showTextDocument(view, {
                viewColumn: vscode.ViewColumn.Beside,
                preview: false,
                preserveFocus: true,
            });
        }),
    );
}
