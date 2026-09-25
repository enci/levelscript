import * as vscode from 'vscode';
import { DecorationSpan, InspectionResult } from './types';
import { tagColor, emptyColor, anyColor } from './palette';

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

function addRange(map: Map<string, vscode.Range[]>, color: string, line: number, col: number, len: number) {
    const ranges = map.get(color) || [];
    const start = new vscode.Position(line - 1, col - 1);
    const end = new vscode.Position(line - 1, col - 1 + Math.max(len, 1));
    ranges.push(new vscode.Range(start, end));
    map.set(color, ranges);
}

// Colors pattern-cell tag values (spec's occurrences of `wall`, `floor`, ...
// inside rules/patterns) and, using the same palette, the value names in
// their `tag { ... }` declaration — the declaration doubles as the legend.
export function applyDecorations(editor: vscode.TextEditor, tokens: DecorationSpan[], tags: InspectionResult['symbols']['tags']) {
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
        addRange(map, color, t.line, t.col, t.len);
    }

    tags.forEach((tag, tagIdx) => {
        tag.values.forEach((v, valueIdx) => {
            if (!v.loc) return;
            const color = tagColor(valueIdx + tagIdx * 12, isDark);
            addRange(map, color, v.loc.line, v.loc.col, v.loc.len);
        });
    });

    for (const [color, ranges] of map.entries()) {
        editor.setDecorations(getDecType(color), ranges);
    }
}

export function clearAll(editor: vscode.TextEditor) {
    for (const dt of decTypes.values()) {
        editor.setDecorations(dt, []);
    }
}
