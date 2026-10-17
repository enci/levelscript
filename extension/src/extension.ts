import * as vscode from 'vscode';
import { applyDecorations, clearAll } from './decorations';
import { setCached, deleteCached } from './cache';
import { definitionProvider, hoverProvider, completionProvider } from './providers';
import { initWasm, inspectJson } from './wasm';
import { editorResolver } from './resolve';
import * as path from 'path';
import { registerDebugger } from './debugger';
import { registerAstView } from './ast_view';
import { registerTilesetView } from './tileset_view';
import { documentSymbolProvider, foldingRangeProvider, selectionRangeProvider } from './structure';

const LS_LANG = 'levelscript';
let diagnosticCollection: vscode.DiagnosticCollection;

export async function activate(ctx: vscode.ExtensionContext) {
  await initWasm();

  diagnosticCollection = vscode.languages.createDiagnosticCollection(LS_LANG);
  ctx.subscriptions.push(diagnosticCollection);

  ctx.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument(e => {
      if (e.document.languageId === LS_LANG) analyse(e.document);
    })
  );

  ctx.subscriptions.push(
    vscode.window.onDidChangeActiveTextEditor(e => {
      if (e?.document.languageId === LS_LANG) analyse(e.document);
    })
  );

  ctx.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument(doc => {
      if (doc.languageId === LS_LANG) deleteCached(doc.uri.toString());
    })
  );

  ctx.subscriptions.push(
    vscode.languages.registerDefinitionProvider({ language: LS_LANG }, definitionProvider),
    vscode.languages.registerHoverProvider({ language: LS_LANG }, hoverProvider),
    vscode.languages.registerCompletionItemProvider(
      { language: LS_LANG },
      completionProvider,
      // never ' ' or '\n': Enter would open a list and the next Enter accept it
      '[', '(', '=', ',', '|'
    ),
    vscode.languages.registerDocumentSymbolProvider({ language: LS_LANG }, documentSymbolProvider),
    vscode.languages.registerFoldingRangeProvider({ language: LS_LANG }, foldingRangeProvider),
    vscode.languages.registerSelectionRangeProvider({ language: LS_LANG }, selectionRangeProvider),
  );

  registerDebugger(ctx);
  registerAstView(ctx);
  registerTilesetView(ctx);

  if (vscode.window.activeTextEditor?.document.languageId === LS_LANG) {
    analyse(vscode.window.activeTextEditor.document);
  }
}

export function deactivate() {
  diagnosticCollection.clear();
}

function analyse(doc: vscode.TextDocument) {
  try {
    const result = inspectJson(doc.getText(), doc.uri.fsPath, editorResolver());
    setCached(doc.uri.toString(), result);

    const vsDiags = result.diagnostics.map(d => {
      // A diagnostic in another module of the closure has no position in this
      // file: pin it to the top, naming where it is.
      if (d.module && d.module !== doc.uri.fsPath) {
        const severity = d.severity === 'error'
          ? vscode.DiagnosticSeverity.Error
          : vscode.DiagnosticSeverity.Warning;
        return new vscode.Diagnostic(new vscode.Range(0, 0, 0, 1),
          `${path.basename(d.module)}:${d.line}:${d.col}: ${d.message}`, severity);
      }
      // 1-based lines and columns; without an end, one character is underlined
      const start = new vscode.Position(Math.max(0, d.line - 1), Math.max(0, d.col - 1));
      const end = d.end_line > 0
        ? new vscode.Position(d.end_line - 1, d.end_col - 1)
        : start.translate(0, 1);
      const severity = d.severity === 'error'
        ? vscode.DiagnosticSeverity.Error
        : vscode.DiagnosticSeverity.Warning;
      return new vscode.Diagnostic(new vscode.Range(start, end), d.message, severity);
    });
    diagnosticCollection.set(doc.uri, vsDiags);

    const editor = vscode.window.activeTextEditor;
    if (editor?.document.uri.toString() === doc.uri.toString()) {
      const hasTagDecls = result.symbols.tags.some(t => t.values.some(v => v.loc));
      if ((result.tokens && result.tokens.length > 0) || hasTagDecls)
        applyDecorations(editor, result.tokens, result.symbols.tags);
      else
        clearAll(editor);
    }
  } catch (err) {
    console.error("WASM inspection failed", err);
  }
}
