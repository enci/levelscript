// Shared color palette for tag values — used both for in-editor tag-value
// decorations and for grid visualization, so a value reads as the same
// color in the source and in a rendered level.

const PALETTE_DARK  = ['#7c3d3d','#7c5c3d','#7c7c3d','#4a7c3d','#3d7c5c','#3d7c7c',
                       '#3d5c7c','#3d3d7c','#5c3d7c','#7c3d7c','#7c3d5c','#5c4a3d'];
const PALETTE_LIGHT = ['#ffd7d7','#ffe7d7','#fffbd7','#d7ffd7','#d7ffe7','#d7ffff',
                       '#d7e7ff','#d7d7ff','#e7d7ff','#ffd7ff','#ffd7e7','#ffe7cc'];

export function tagColor(index: number, dark: boolean): string {
    return (dark ? PALETTE_DARK : PALETTE_LIGHT)[index % 12];
}

// Number-grid literals cycle through the same 12 colors by value.
export function numberCellColor(n: number, dark: boolean): string {
    return tagColor(((n % 12) + 12) % 12, dark);
}

export function emptyColor(dark: boolean): string {
    return dark ? '#3a3a46' : '#e4e4ea';   // slate blue-gray
}

export function anyColor(dark: boolean): string {
    return dark ? '#5a5042' : '#ddd0b8';   // warm gray
}
