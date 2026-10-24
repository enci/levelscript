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
    end_line: number;   // one past what is wrong; 0: just the point
    end_col: number;
    module?: string;   // the module the position is in
    severity: 'error' | 'warning';
    message: string;
}

export interface RefSpan extends Location {
    kind: 'layer' | 'graph' | 'rule' | 'sequence';
    target: string;
}

// A node's source range: 1-based, the end one past its last character.
export interface SourceRange {
    line: number;
    col: number;
    end_line: number;
    end_col: number;
}

// One outline entry: a declaration's whole range, and its name's.
export interface OutlineSymbol {
    kind: 'tag' | 'value' | 'union' | 'layers' | 'grid' | 'graph' | 'params' | 'param' | 'rule' | 'sequence';
    name: string;
    range: SourceRange;
    select: SourceRange;
    children?: OutlineSymbol[];
}

// A tool directive (`// @key=value`, spec Appendix B), attached to its target.
export interface Directive {
    key: 'seed' | 'name' | 'tile' | 'tileset' | 'play' | string;
    value: string;   // as written
    loc: Location;
    target: {
        kind: 'file' | 'tag_value' | 'layer' | 'statement';
        range?: SourceRange;
        owner?: string;   // tag_value: the tagset; statement: the sequence
        name?: string;    // tag_value: the value; layer: the grid
        index?: number;   // statement: its position in the sequence body
    };
}

export interface InspectionResult {
    ok: boolean;
    diagnostics: InspectorDiagnostic[];
    directives?: Directive[];
    directive_warnings?: Omit<InspectorDiagnostic, 'severity'>[];
    tokens: DecorationSpan[];
    refs?: RefSpan[];
    outline?: OutlineSymbol[];
    spans?: SourceRange[];   // every node of the file's rules and sequences, but lone tokens
    symbols: {
        modules?: string[];
        tags: { name: string, values: { name: string, loc?: Location }[], unions: { name: string, loc?: Location }[] }[];
        layers: { name: string, type: string, loc?: Location }[];
        graphs?: { name: string, source: string, loc?: Location }[];
        params: { name: string, derived: boolean }[];
        rules: { name: string, loc?: Location }[];
        sequences: { name: string, loc?: Location }[];
        ops: string[];
        builtins: string[];
        graph_functions?: string[];   // built-ins of graph-pattern cells only
    };
}
