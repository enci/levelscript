import * as vscode from 'vscode';
import { DecorationSpan } from './types';

// Palette of colors for tag values (modulo cycle)
const colors = [
    '#f44336', '#e91e63', '#9c27b0', '#673ab7', '#3f51b5',
    '#2196f3', '#03a9f4', '#00bcd4', '#009688', '#4caf50',
    '#8bc34a', '#cddc39', '#ffeb3b', '#ffc107', '#ff9800',
    '#ff5722', '#795548', '#9e9e9e', '#607d8b'
];

const decTypes = new Map<string, vscode.TextEditorDecorationType>();

function getDecType(colorIndex: number): vscode.TextEditorDecorationType {
    const key = colorIndex.toString();
    let dt = decTypes.get(key);
    if (!dt) {
        const color = colors[colorIndex % colors.length];
        dt = vscode.window.createTextEditorDecorationType({
            color: color,
            fontWeight: 'bold'
        });
        decTypes.set(key, dt);
    }
    return dt;
}

export function applyDecorations(editor: vscode.TextEditor, tokens: DecorationSpan[]) {
    clearAll(editor);

    const map = new Map<number, vscode.Range[]>();
    for (const t of tokens) {
        // use a combo of tag id and value id for coloring
        const colorHash = t.tag * 31 + t.value;
        const ranges = map.get(colorHash) || [];
        const start = new vscode.Position(t.line - 1, t.col - 1);
        const end = new vscode.Position(t.line - 1, t.col - 1 + t.len);
        ranges.push(new vscode.Range(start, end));
        map.set(colorHash, ranges);
    }

    for (const [hash, ranges] of map.entries()) {
        editor.setDecorations(getDecType(hash), ranges);
    }
}

export function clearAll(editor: vscode.TextEditor) {
    for (const dt of decTypes.values()) {
        editor.setDecorations(dt, []);
    }
}
