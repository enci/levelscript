export interface Location {
    line: number;
    col: number;
    len: number;
    module?: string;   // declaring module's canonical name (an absolute path here)
}

// tag >= 0: index into symbols.tags, value = palette slot (the tag's values
// first, then its unions); tag -1 '*', -2 '.', -3 a number-grid literal
// (value = the number).
export interface DecorationSpan extends Location {
    tag: number;
    value: number;
}

export interface InspectorDiagnostic {
    line: number;
    col: number;
    module?: string;   // the module the position is in
    severity: 'error' | 'warning';
    message: string;
}

export interface RefSpan extends Location {
    kind: 'layer' | 'rule' | 'sequence';
    target: string;
}

export interface InspectionResult {
    ok: boolean;
    diagnostics: InspectorDiagnostic[];
    tokens: DecorationSpan[];
    refs?: RefSpan[];
    symbols: {
        modules?: string[];
        tags: { name: string, values: { name: string, loc?: Location }[], unions: { name: string, loc?: Location }[] }[];
        layers: { name: string, type: string, loc?: Location }[];
        params: { name: string, derived: boolean }[];
        rules: { name: string, loc?: Location }[];
        sequences: { name: string, loc?: Location }[];
        ops: string[];
        builtins: string[];
    };
}
