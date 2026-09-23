import * as vscode from 'vscode';
import { DecorationSpan } from './types';

// Palette of colors for tag values (modulo cycle)
const PALETTE_DARK  = ['#853232','#855b32','#858532','#438532','#32855b','#328585',
                       '#325b85','#323285','#5b3285','#853285','#85325b','#664d3b'];
const PALETTE_LIGHT = ['#ffadad','#ffd6ad','#ffffad','#beffad','#adffd6','#adffff',
                       '#add6ff','#adadff','#d6adff','#ffadff','#ffadd6','#ffd19e'];

function tagColor(index: number, dark: boolean): string {
    return (dark ? PALETTE_DARK : PALETTE_LIGHT)[index % 12];
}

function emptyColor(dark: boolean): string {
    return dark ? '#3a3a46' : '#e4e4ea';   // slate blue-gray
}

function anyColor(dark: boolean): string {
    return dark ? '#40392f' : '#ece3d4';   // warm gray
}

const decTypes = new Map<string, vscode.TextEditorDecorationType>();

function getDecType(color: string): vscode.TextEditorDecorationType {
    let dt = decTypes.get(color);
    if (!dt) {
        dt = vscode.window.createTextEditorDecorationType({
            backgroundColor: color,
            borderRadius: '2px',
        });
        decTypes.set(color, dt);
    }
    return dt;
}

export function applyDecorations(editor: vscode.TextEditor, tokens: DecorationSpan[]) {
    clearAll(editor);

    const isDark = vscode.window.activeColorTheme.kind === vscode.ColorThemeKind.Dark 
                || vscode.window.activeColorTheme.kind === vscode.ColorThemeKind.HighContrast;

    const map = new Map<string, vscode.Range[]>();
    for (const t of tokens) {
        let color: string;
        if (t.tag === -1) {
            color = anyColor(isDark);
        } else if (t.tag === -2) {
            color = emptyColor(isDark);
        } else {
            color = tagColor(t.value + t.tag * 12, isDark);
        }

        const ranges = map.get(color) || [];
        const start = new vscode.Position(t.line - 1, t.col - 1);
        const end = new vscode.Position(t.line - 1, t.col - 1 + Math.max(t.len, 1));
        ranges.push(new vscode.Range(start, end));
        map.set(color, ranges);
    }

    for (const [color, ranges] of map.entries()) {
        editor.setDecorations(getDecType(color), ranges);
    }
}

export function clearAll(editor: vscode.TextEditor) {
    for (const dt of decTypes.values()) {
        editor.setDecorations(dt, []);
    }
}
