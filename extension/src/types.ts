export interface Location {
    line: number;
    col: number;
    len: number;
}

export interface DecorationSpan extends Location {
    tag: number;
    value: number;
}

export interface InspectorDiagnostic {
    line: number;
    col: number;
    severity: 'error' | 'warning';
    message: string;
}

export interface RefSpan extends Location {
    kind: 'layer' | 'rule';
    target: string;
}

export interface InspectionResult {
    ok: boolean;
    diagnostics: InspectorDiagnostic[];
    tokens: DecorationSpan[];
    refs?: RefSpan[];
    symbols: {
        tags: { name: string, values: { name: string, loc?: Location }[], unions: string[] }[];
        layers: { name: string, type: string, loc?: Location }[];
        params: { name: string, derived: boolean }[];
        rules: { name: string, loc?: Location }[];
        ops: string[];
        builtins: string[];
    };
}

// ── run/debug wasm bindings (see src/wasm_bind.cpp) ─────────────────────────

export interface RunLayer {
    name: string;
    isNumber: boolean;
    // Row-major, width*height entries. Tag cells: array of value names
    // (empty array = empty cell, more than one = a union write). Number
    // cells: the number, or null when empty.
    cells: (string[] | number | null)[];
}

export interface RunHighlight {
    layer: number;
    x: number;
    y: number;
    what: 'match' | 'write';
}

export interface RunLevel {
    width: number;
    height: number;
    layers: RunLayer[];
}

export interface RunState {
    done: boolean;
    seed: number;
    statementIndex: number;
    statementCount: number;
    atStatementBoundary: boolean;
    appsInStatement: number;
    highlights: RunHighlight[];
    level: RunLevel;
}
