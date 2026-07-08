export interface DecorationSpan {
    line: number;
    col: number;
    len: number;
    tag: number;
    value: number;
}

export interface InspectorDiagnostic {
    line: number;
    col: number;
    severity: 'error' | 'warning';
    message: string;
}

export interface InspectionResult {
    ok: boolean;
    diagnostics: InspectorDiagnostic[];
    tokens: DecorationSpan[];
    symbols: {
        tags: { name: string, values: string[], unions: string[] }[];
        layers: { name: string, type: string }[];
        params: { name: string, derived: boolean }[];
        rules: string[];
        ops: string[];
        builtins: string[];
    };
}
