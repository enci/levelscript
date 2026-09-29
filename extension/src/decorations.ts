import * as vscode from 'vscode';
import { DecorationSpan, InspectionResult } from './types';
import { tagColor, emptyColor, anyColor, numberCellColor } from './palette';

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

// A cell's background also covers the one whitespace character after it, so
// a pattern row reads as a solid strip rather than separated chips.
function cellLen(doc: vscode.TextDocument, t: DecorationSpan): number {
    const text = doc.lineAt(t.line - 1).text;
    const next = text.charAt(t.col - 1 + t.len);
    return next === ' ' || next === '\t' ? t.len + 1 : t.len;
}

// Colors pattern cells (tag values, unions, number literals, `*`, `.`) and,
// using the same palette, the value and union names in their `tag { ... }`
// declaration — the declaration doubles as the legend.
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
        } else if (t.tag === -3) {
            color = numberCellColor(t.value, isDark);
        } else {
            color = tagColor(t.value + t.tag * 12, isDark);
        }
        addRange(map, color, t.line, t.col, cellLen(editor.document, t));
    }

    tags.forEach((tag, tagIdx) => {
        tag.values.forEach((v, valueIdx) => {
            if (!v.loc) return;
            const color = tagColor(valueIdx + tagIdx * 12, isDark);
            addRange(map, color, v.loc.line, v.loc.col, v.loc.len);
        });
        tag.unions.forEach((u, unionIdx) => {
            if (!u.loc) return;
            const color = tagColor(tag.values.length + unionIdx + tagIdx * 12, isDark);
            addRange(map, color, u.loc.line, u.loc.col, u.loc.len);
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
