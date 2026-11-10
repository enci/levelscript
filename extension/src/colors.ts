import * as vscode from 'vscode';
import { getCached } from './cache';

// The color picker for `@color=#rrggbb` (spec B.9): a swatch beside the value,
// and the picker writes the chosen color back as #rrggbb. Only a `@color` the
// compiler attached to a tag value is offered, so what the swatch shows is what
// the tools use.
function hexOf(c: vscode.Color): string {
    const h = (x: number) => Math.round(x * 255).toString(16).padStart(2, '0');
    return `#${h(c.red)}${h(c.green)}${h(c.blue)}`;
}

export const colorProvider: vscode.DocumentColorProvider = {
    provideDocumentColors(doc) {
        const out: vscode.ColorInformation[] = [];
        for (const d of getCached(doc.uri.toString())?.directives ?? []) {
            if (d.key !== 'color' || d.target.kind !== 'tag_value') continue;
            if (d.loc.module && d.loc.module !== doc.uri.fsPath) continue;
            const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(d.value);
            if (!m) continue;
            // loc spans `@color=#rrggbb` (1-based): the value is its last 7 characters
            const line = d.loc.line - 1, end = d.loc.col - 1 + d.loc.len;
            const range = new vscode.Range(line, end - d.value.length, line, end);
            const [r, g, b] = [m[1], m[2], m[3]].map(x => parseInt(x, 16) / 255);
            out.push(new vscode.ColorInformation(range, new vscode.Color(r, g, b, 1)));
        }
        return out;
    },

    provideColorPresentations(color, ctx) {
        const hex = hexOf(color);
        const p = new vscode.ColorPresentation(hex);
        p.textEdit = vscode.TextEdit.replace(ctx.range, hex);
        return [p];
    },
};
