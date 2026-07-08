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
        tags: { name: string, values: string[], unions: string[] }[];
        layers: { name: string, type: string, loc?: Location }[];
        params: { name: string, derived: boolean }[];
        rules: { name: string, loc?: Location }[];
        ops: string[];
        builtins: string[];
    };
}
