// Completion context: where in the grammar (docs/spec.md) the cursor sits, decided
// from the text before it. Pure (no vscode import) so scripts/ can test it.
//
// A tiny scanner tokenizes up to the word being typed, skipping comments, and
// keeps a stack of open '{' '[' '(' frames. Each frame knows what opened it
// (a `tag` block, a rule's attributes, `scatter(`, `path(`, a pattern of grid g,
// ...) and the tokens seen inside it so far; the innermost frame plus its last
// token or two decide the context. Anything unrecognized is 'none' - no list
// at all beats a list of everything.

export type CompletionContext =
    | { kind: 'none' }
    | { kind: 'top' }                                   // use / tag / layers / params / rule / sequence
    | { kind: 'maskMember'; tag: string }               // tag t { ..., D = !(F | _ }
    | { kind: 'gridOf' }                                // layers { g: _ }
    | { kind: 'of' }                                    // layers { g: grid _ } / { m: graph _ }
    | { kind: 'gridType' }                              // layers { g: grid of _ }
    | { kind: 'graphSource' }                           // layers { m: graph of _ }
    | { kind: 'graphArg' }                              // layers { m: graph of g(_ }
    | { kind: 'paramType' }                             // params { p: _ }
    | { kind: 'expr'; grids: boolean; pos: boolean; nodes: boolean; globals: boolean;
        head?: string; vars?: string[] }
                                                        // expressions; nodes: in a rule, where
                                                        // node variables and graph functions are;
                                                        // globals: count(s), nodes(g), edges(g)
                                                        // (all but params); head: the pattern
                                                        // whose cell this is; vars: the rule's
                                                        // pattern variables, by bare name
    | { kind: 'member'; head: string; write: boolean; vars?: string[] }
                                                        // n._ / map._ - an attribute, a region
                                                        // read, or (write) a graph write's grid
    | { kind: 'ruleAttr' }                              // rule r(_)
    | { kind: 'attrValue'; attr: string }               // rule r(symmetry=_)
    | { kind: 'ruleBody'; start: boolean; guard: boolean; block?: boolean }
                                                        // a pattern may start here; guard: so
                                                        // may a rule guard `when (...)`; block:
                                                        // a graph block just ended, so only one
    | { kind: 'combinator' }                            // => { _   (all / any)
    | { kind: 'weight' }                                // { any (_ ) g[...] }
    | { kind: 'cell'; grid: string }                    // g[ _ ]
    | { kind: 'statement'; guard: boolean }             // sequence s { _ }
    | { kind: 'ruleName' }                              // sequence s { once _ } - rules and sequences
    | { kind: 'opArg'; op: string; index: number; used: string[] }  // path(_)
    | { kind: 'opValue'; op: string; param: string };   // path(into=_)

// Operation parameter schemas. The op names are checked against the compiler's
// inspect `ops` list by scripts/check-completion.js.
export type OpParamKind = 'int' | 'grid' | 'graph' | 'pred' | 'value' | 'expr' | 'enum';
// `values`: the suggested words (an enum's whole set; an int's meaningful values)
export interface OpParam { name: string; kind: OpParamKind; positional: boolean; required: boolean; values?: string[] }
export const OPS: { name: string; params: OpParam[]; snippet: string }[] = [
    { name: 'resize',  snippet: 'resize(${1:w}, ${2:h})',
      params: [{ name: 'w', kind: 'int', positional: true, required: true },
               { name: 'h', kind: 'int', positional: true, required: true }] },
    { name: 'upscale', snippet: 'upscale(${1:n}, ${2:m})',
      params: [{ name: 'n', kind: 'int', positional: true, required: true },
               { name: 'm', kind: 'int', positional: true, required: true }] },
    { name: 'trim',    snippet: 'trim()', params: [] },
    { name: 'mirror',  snippet: 'mirror(${1|horizontal,vertical|})',
      params: [{ name: 'axis', kind: 'enum', positional: true, required: true, values: ['horizontal', 'vertical'] }] },
    { name: 'pad',     snippet: 'pad(${1:n})',
      params: [{ name: 'n', kind: 'int', positional: true, required: true }] },
    { name: 'path',    snippet: 'path(from=${1}, to=${2}, into=${3}, write=${4})',
      params: [{ name: 'from', kind: 'pred', positional: false, required: true },
               { name: 'to', kind: 'pred', positional: false, required: true },
               { name: 'into', kind: 'grid', positional: false, required: true },
               { name: 'write', kind: 'value', positional: false, required: true },
               { name: 'over', kind: 'graph', positional: false, required: false },
               { name: 'passable', kind: 'pred', positional: false, required: false },
               { name: 'connectivity', kind: 'int', positional: false, required: false, values: ['4', '8'] },
               { name: 'cost', kind: 'expr', positional: false, required: false }] },
    { name: 'bsp',     snippet: 'bsp(into=${1}, min=${2})',
      params: [{ name: 'into', kind: 'grid', positional: false, required: true },
               { name: 'min', kind: 'int', positional: false, required: true },
               { name: 'max', kind: 'int', positional: false, required: false },
               { name: 'split', kind: 'int', positional: false, required: false }] },
    { name: 'voronoi', snippet: 'voronoi(sites=${1}, into=${2})',
      params: [{ name: 'sites', kind: 'pred', positional: false, required: true },
               { name: 'into', kind: 'grid', positional: false, required: true },
               { name: 'within', kind: 'pred', positional: false, required: false },
               { name: 'metric', kind: 'enum', positional: false, required: false, values: ['euclidean', 'manhattan', 'chebyshev'] },
               { name: 'cost', kind: 'expr', positional: false, required: false }] },
    { name: 'noise',   snippet: 'noise(into=${1}, lo=${2:0}, hi=${3:99}, scale=${4:16})',
      params: [{ name: 'into', kind: 'grid', positional: false, required: true },
               { name: 'lo', kind: 'int', positional: false, required: true },
               { name: 'hi', kind: 'int', positional: false, required: true },
               { name: 'scale', kind: 'int', positional: false, required: true },
               { name: 'octaves', kind: 'int', positional: false, required: false },
               { name: 'persistence', kind: 'int', positional: false, required: false },
               { name: 'normalize', kind: 'enum', positional: false, required: false, values: ['rank', 'range'] }] },
];

interface Tok { t: string; owner?: string }   // t: text; owner set on synthetic closers
interface Frame {
    open: '{' | '[' | '(' | '';
    owner: string;       // what opened it: 'top' 'tag:t' 'layers' 'params' 'rule' 'statements' 'combinator' 'group' 'block' 'set' 'grid:g' 'where' 'attrs' 'count:grow' 'op:path' 'when' 'weight' 'expr'
    scope: 'full' | 'restricted' | '';   // expression scope inherited by '(' frames
    head?: string;       // the pattern head of a cell's '(' frames
    toks: Tok[];
    start?: number;      // the offset of its opening bracket
}

const STRING = '<string>';   // token text for any string literal
const OPERATORS = new Set(['+', '-', '*', '/', '==', '!=', '<', '<=', '>', '>=', '&&', '||', '|', '!']);
// Modes. scatter always takes a count; grow and settle may.
const MODES = new Set(['once', 'scatter', 'everywhere', 'grow', 'settle']);
const COUNTED = new Set(['scatter', 'grow', 'settle']);
// Words that open a rule body's sub-rules or blocks, never a graph block's name.
const BODY_WORDS = new Set(['any', 'all', 'ordered', 'when', 'where']);
// The node attributes (section 5.12) and the global queries (valid in every
// expression scope but params).
export const NODE_ATTRIBUTES = ['degree', 'size', 'value'];
export const GLOBAL_QUERIES = ['count', 'nodes', 'edges'];

function isIdentStart(c: string) { return /[A-Za-z_]/.test(c); }
function isIdent(c: string) { return /[A-Za-z0-9_]/.test(c); }

export function completionContext(text: string, offset: number): CompletionContext {
    // the word being typed is the filter text, not context
    let wordStart = offset;
    while (wordStart > 0 && isIdent(text[wordStart - 1])) wordStart--;
    // `head.word`: an attribute access; the context is where `head` stands
    let member: string | undefined;
    let scanEnd = wordStart;
    if (wordStart >= 2 && text[wordStart - 1] === '.' && isIdent(text[wordStart - 2])) {
        let h = wordStart - 1;
        while (h > 0 && isIdent(text[h - 1])) h--;
        if (isIdentStart(text[h])) { member = text.slice(h, wordStart - 1); scanEnd = h; }
    }

    const stack: Frame[] = [{ open: '', owner: 'top', scope: '', toks: [], start: 0 }];
    const top = () => stack[stack.length - 1];

    let i = 0;
    while (i < scanEnd) {
        const c = text[i];
        if (c === '/' && text[i + 1] === '/') {
            const eol = text.indexOf('\n', i);
            if (eol < 0 || eol >= offset) return { kind: 'none' };   // typing in a comment
            i = eol;
            continue;
        }
        if (/\s/.test(c)) { i++; continue; }
        if (c === '"') {   // a `use` path: one token, no escapes
            let j = i + 1;
            while (j < text.length && text[j] !== '"' && text[j] !== '\n') j++;
            if (j >= offset || text[j] !== '"') return { kind: 'none' };   // typing a path
            top().toks.push({ t: STRING });
            i = j + 1;
            continue;
        }
        if (isIdentStart(c) || /[0-9]/.test(c)) {
            let j = i + 1;
            while (j < scanEnd && isIdent(text[j])) j++;
            top().toks.push({ t: text.slice(i, j) });
            i = j;
            continue;
        }
        const two = text.slice(i, i + 2);
        if (['=>', '==', '!=', '<=', '>=', '&&', '||'].includes(two)) {
            top().toks.push({ t: two });
            i += 2;
            continue;
        }
        if (c === '{' || c === '[' || c === '(') {
            const f = openFrame(c, stack);
            f.start = i;
            stack.push(f);
        } else if (c === '}' || c === ']' || c === ')') {
            if (stack.length > 1) {
                const f = stack.pop()!;
                top().toks.push({ t: c, owner: f.owner });
            }
        } else {
            top().toks.push({ t: c });
        }
        i++;
    }
    const ctx = classify(stack);
    // the pattern variables of the rule around the cursor, by bare name
    const rule = [...stack].reverse().find(f => f.owner === 'rule');
    const vars: string[] = [];
    if (rule) for (const m of text.slice(rule.start ?? 0, scanEnd).matchAll(/\?([A-Za-z_]\w*)/g))
        if (!vars.includes(m[1])) vars.push(m[1]);
    if (member !== undefined) {
        // an attribute or region read in an expression; on a write side, a graph write
        if (ctx.kind === 'expr') return vars.length ? { kind: 'member', head: member, write: false, vars }
                                                    : { kind: 'member', head: member, write: false };
        if (ctx.kind === 'ruleBody' && !ctx.start)
            return vars.length ? { kind: 'member', head: member, write: true, vars }
                               : { kind: 'member', head: member, write: true };
        return { kind: 'none' };
    }
    if (ctx.kind === 'expr' && ctx.nodes && vars.length) return { ...ctx, vars };
    return ctx;
}

// The top frame's tokens since the last completed declaration.
function currentDecl(toks: Tok[]): Tok[] {
    let k = toks.length;
    while (k > 0 && !(toks[k - 1].t === '}' && toks[k - 1].owner !== undefined)) k--;
    return toks.slice(k);
}

function openFrame(c: '{' | '[' | '(', stack: Frame[]): Frame {
    const parent = stack[stack.length - 1];
    const toks = parent.toks;
    const last = toks[toks.length - 1];
    const prev = last?.t;
    const prev2 = toks[toks.length - 2]?.t;
    const frame = (owner: string, scope: Frame['scope'] = '', head?: string): Frame =>
        head === undefined ? { open: c, owner, scope, toks: [] } : { open: c, owner, scope, head, toks: [] };

    if (c === '{') {
        if (parent.owner === 'top') {
            const decl = currentDecl(toks);
            const kw = decl[0]?.t;
            if (kw === 'tag') return frame('tag:' + (decl[1]?.t ?? ''));
            if (kw === 'layers' || kw === 'params') return frame(kw);
            if (kw === 'sequence') return frame('statements');   // a statement_list
            if (kw === 'rule') return frame('rule');
            return frame('other');
        }
        // an inline rule's body: after a mode, a count, or its attributes
        if (parent.owner === 'statements' && last && (endsMode(last) || (last.t === ')' && last.owner === 'attrs')))
            return frame('rule');
        if (parent.owner === 'attrs' && prev === '=' && prev2 === 'rotation') return frame('set');
        // after '=>' (and inside a write block) a combinator block; otherwise
        // braces only group a match side
        if (parent.owner === 'combinator' || (parent.owner === 'rule' && prev === '=>'))
            return frame('combinator');
        // a name before '{' in a rule body is a graph block: `plan { ?n }`
        if ((parent.owner === 'rule' || parent.owner === 'group') && prev && isIdentStart(prev[0]) &&
            !BODY_WORDS.has(prev) && !(prev2 === '=' || prev2 === '.'))
            return frame('block');
        if (parent.owner === 'rule') return frame('group');
        return frame('other');
    }
    if (c === '[') {
        const head = prev;
        if (head === 'where') return frame('where');
        if (head && isIdentStart(head[0])) return frame('grid:' + head);
        return frame('other');
    }
    // '('
    if (parent.owner === 'top' && currentDecl(toks)[0]?.t === 'rule') return frame('attrs');
    if (parent.owner.startsWith('tag:')) return frame(parent.owner);   // a named mask's group
    if (parent.owner === 'statements') {
        if (prev && COUNTED.has(prev)) return frame('count:' + prev, 'restricted');   // params only
        if (last && endsMode(last)) return frame('attrs');   // an inline rule's attributes
        if (prev === 'when') return frame('when', 'restricted');
        if (prev && isIdentStart(prev[0])) return frame('op:' + prev, 'full');
        return frame('other');
    }
    // a rule guard's expression; after a graph write's '=', its value; a
    // bare '(' a weight
    const inBody = parent.owner === 'rule' || parent.owner === 'combinator' || parent.owner === 'group';
    if (inBody && prev === 'when') return frame('when', 'restricted');   // candidate scope
    if (inBody && prev === '=' && graphWriteHead(toks)) return frame('expr', 'full', graphWriteHead(toks));
    // a name then '(' was a sized pattern or a node extent, both removed in 0.17
    if (inBody && prev && isIdentStart(prev[0]) && !BODY_WORDS.has(prev)) return frame('other');
    if (parent.owner === 'rule' || parent.owner === 'combinator') return frame('weight');
    if (parent.owner === 'params') return frame('expr', 'restricted');
    if (parent.owner === 'layers') return frame('graphArgs');
    if (parent.owner.startsWith('grid:')) return frame('expr', 'full', parent.owner.slice(5));
    if (parent.owner === 'where') return frame('expr', 'full');
    if (parent.scope) return frame('expr', parent.scope, parent.head);
    return frame('other');
}

// `n . g =` at the end of a frame's tokens: the grid g of a graph write.
function graphWriteHead(toks: Tok[]): string | undefined {
    const k = toks.length;
    if (k < 4 || toks[k - 1].t !== '=' || toks[k - 3].t !== '.') return undefined;
    const n = toks[k - 4].t, g = toks[k - 2].t;
    return isIdentStart(n[0]) && isIdentStart(g[0]) ? g : undefined;
}

// Does a graph write `n . g = value` end the frame's tokens? Its value is
// one token, or a parenthesized expression.
function graphWriteDone(toks: Tok[]): boolean {
    const k = toks.length;
    const valueOk = (t: Tok) => isIdentStart(t.t[0]) || /^[0-9]/.test(t.t) || t.t === '.' ||
                                (t.t === ')' && t.owner === 'expr');
    return k >= 5 && valueOk(toks[k - 1]) && graphWriteHead(toks.slice(0, k - 1)) !== undefined;
}

// Does this token end a mode, so a rule or sequence name follows? A mode
// word that takes no count yet, or the ')' closing a count.
function endsMode(t: Tok) {
    return (MODES.has(t.t) && t.t !== 'scatter') || (t.t === ')' && !!t.owner?.startsWith('count:'));
}

// Is the cursor on a match side, after one of its patterns, where a rule
// guard may follow? In a 'rule' frame a match side is everything before the
// first '=>', or, after the last '=>', the items past its one write term.
function guardMayFollow(f: Frame): boolean {
    const last = f.toks[f.toks.length - 1];
    if (!last || !(last.t === ']' || (last.t === ')' && last.owner === 'when') ||
                   (last.t === '}' && (last.owner === 'group' || last.owner === 'block'))))
        return false;
    if (f.owner === 'group') return true;
    if (f.owner !== 'rule') return false;
    let arrow = -1;
    f.toks.forEach((t, k) => { if (t.t === '=>') arrow = k; });
    if (arrow < 0) return true;
    let items = 0;
    for (const t of f.toks.slice(arrow + 1))
        if (t.t === ']' || (t.t === '}' && t.owner !== undefined)) items++;
    return items >= 2;
}

// Do a frame's tokens since its last '=>' end in a graph block and its guards?
function afterBlock(f: Frame): boolean {
    for (let k = f.toks.length - 1; k >= 0; k--) {
        const t = f.toks[k];
        if (t.t === ')' && t.owner === 'when') { k--; if (f.toks[k]?.t !== 'when') return false; continue; }
        return t.t === '}' && t.owner === 'block';
    }
    return false;
}

// Tokens after which an expression operand may start.
function exprMayStart(prev: string | undefined) {
    return prev === undefined || prev === '(' || prev === ',' || prev === '=' || OPERATORS.has(prev);
}

function classify(stack: Frame[]): CompletionContext {
    const f = stack[stack.length - 1];
    const toks = f.toks;
    const last = toks[toks.length - 1];
    const prev = last?.t;
    const prev2 = toks[toks.length - 2]?.t;
    const none: CompletionContext = { kind: 'none' };

    switch (f.owner) {
    case 'top':
        // between declarations only - after `tag`, `rule` etc. a name is being
        // declared; a finished `use "path"` is a complete declaration too
        return prev === undefined || (prev === '}' && last.owner !== undefined) ||
               (prev === STRING && prev2 === 'use')
            ? { kind: 'top' } : none;
    case 'layers':
        if (prev === ':') return { kind: 'gridOf' };
        if (prev === 'grid' || prev === 'graph') return { kind: 'of' };
        if (prev === 'of') return prev2 === 'graph' ? { kind: 'graphSource' } : { kind: 'gridType' };
        return none;
    case 'graphArgs':
        return prev === undefined || prev === ',' ? { kind: 'graphArg' } : none;
    case 'params':
        if (prev === ':') return { kind: 'paramType' };
        // `p: number = _` or `p = _`, and operands after operators - not a new entry's name
        if (prev === '=' || (prev !== undefined && OPERATORS.has(prev)))
            return { kind: 'expr', grids: false, pos: false, nodes: false, globals: false };
        return none;
    case 'attrs':
        if (prev === undefined || prev === ',') return { kind: 'ruleAttr' };
        if (prev === '=' && prev2) return { kind: 'attrValue', attr: prev2 };
        return none;
    case 'group':
        return prev === undefined || prev === ']' || (prev === ')' && last.owner === 'when')
            ? { kind: 'ruleBody', start: false, guard: guardMayFollow(f) } : none;
    case 'rule':
    case 'combinator': {
        if (prev === undefined)
            return f.owner === 'rule' ? { kind: 'ruleBody', start: true, guard: false } : { kind: 'combinator' };
        // after a graph block, its guards, then '=>'
        if (prev === '}' && last.owner === 'block')
            return { kind: 'ruleBody', start: false, guard: true, block: true };
        if (prev === ')' && last.owner === 'when' && afterBlock(f))
            return { kind: 'ruleBody', start: false, guard: true, block: true };
        // a graph write's value: a cell of its grid
        if (prev === '=' && graphWriteHead(toks)) return { kind: 'cell', grid: graphWriteHead(toks)! };
        if (graphWriteDone(toks)) return { kind: 'ruleBody', start: false, guard: false };
        if (prev === '=>' || prev === ']' || prev === '}' || prev === ')' || prev === ',' ||
            prev === 'any' || prev === 'all' || prev === 'ordered')
            return { kind: 'ruleBody', start: false, guard: guardMayFollow(f) };
        return none;
    }
    case 'block':
        return none;   // a graph block declares its variables: `?n`, `?a -- ?b`
    case 'weight':
        return prev === undefined ? { kind: 'weight' } : none;
    case 'statements': {
        if (prev === undefined) return { kind: 'statement', guard: false };
        if (endsMode(last)) return { kind: 'ruleName' };
        if (prev === ')' && last.owner?.startsWith('op:')) return { kind: 'statement', guard: true };
        if (prev === ')' && last.owner === 'when') return { kind: 'statement', guard: false };
        if (prev === '}' && last.owner === 'rule') return { kind: 'statement', guard: true };   // an inline rule ended
        // `once r _` / `scatter(3) r _`: an application just ended
        const before = toks[toks.length - 2];
        if (isIdentStart(prev[0]) && before && endsMode(before))
            return { kind: 'statement', guard: true };
        return none;
    }
    case 'set':
        return none;
    case 'where':
        return none;   // where cells are '(' expressions
    }
    if (f.owner.startsWith('tag:'))
        // in a named mask: after '=', '|', '!', or a group's '('
        return prev === '=' || prev === '|' || prev === '!' || (f.open === '(' && prev === undefined)
            ? { kind: 'maskMember', tag: f.owner.slice(4) } : none;
    if (f.owner.startsWith('grid:')) {
        // a cell may start anywhere except right after an atom-joining '|' / '!' ... which also want a value
        return { kind: 'cell', grid: f.owner.slice(5) };
    }
    if (f.owner.startsWith('op:')) {
        const op = f.owner.slice(3);
        if (prev === undefined || prev === ',') {
            const used: string[] = [];
            let index = 0;
            for (let k = 0; k < toks.length; k++) {
                if (toks[k].t === ',') index++;
                if (toks[k].t === '=' && k > 0) used.push(toks[k - 1].t);
            }
            return { kind: 'opArg', op, index, used };
        }
        if (prev === '=' && prev2) return { kind: 'opValue', op, param: prev2 };
        return none;
    }
    if (f.owner === 'when' || f.owner === 'expr' || f.owner.startsWith('count:')) {
        if (!exprMayStart(prev)) return none;
        const full = f.scope === 'full';
        const nodes = stack.some(s => s.owner === 'rule' || s.owner === 'group' || s.owner === 'combinator');
        return f.head === undefined ? { kind: 'expr', grids: full, pos: full, nodes, globals: true }
                                    : { kind: 'expr', grids: full, pos: full, nodes, globals: true, head: f.head };
    }
    return none;
}

// ── suggestions: what to offer in a context, shared by the VS Code extension
//    and the website's editor, which each map them onto their own items ─────

// The inspect symbols completion reads (types.ts has the full shape).
export interface CompletionSymbols {
    tags: { name: string, values: { name: string }[], masks: { name: string }[] }[];
    layers: { name: string, type: string }[];
    graphs?: { name: string, source: string }[];
    params: { name: string, derived: boolean }[];
    rules: { name: string }[];
    sequences?: { name: string }[];
    builtins: string[];
    graph_functions?: string[];
    node_attributes?: string[];
}
export type SuggestionKind = 'keyword' | 'function' | 'enum' | 'enumMember' | 'class' | 'interface' |
    'variable' | 'constant' | 'property' | 'method' | 'module' | 'type';
// `snippet`: a VS Code snippet body (`${1:x}`, `${1|a,b|}`, `$0`) to insert instead of the label
export interface Suggestion { label: string; kind: SuggestionKind; detail?: string; snippet?: string }

export const ATTRIBUTE_DETAIL: Record<string, string> = {
    degree: 'node attribute: the number of adjacent nodes',
    size: 'node attribute: the number of cells in its region',
    value: 'node attribute: the value every cell of its region holds',
};

// The node attribute at `offset` of `text` - `size` in `n.size` - or undefined.
export function attributeAt(text: string, offset: number): string | undefined {
    let a = offset, b = offset;
    while (a > 0 && isIdent(text[a - 1])) a--;
    while (b < text.length && isIdent(text[b])) b++;
    const word = text.slice(a, b);
    return NODE_ATTRIBUTES.includes(word) && text[a - 1] === '.' && a >= 2 && isIdent(text[a - 2])
        ? word : undefined;
}

export function suggestions(ctx: CompletionContext, sym: CompletionSymbols | undefined): Suggestion[] {
    const items: Suggestion[] = [];
    const word = (label: string, kind: SuggestionKind, detail?: string) => { items.push({ label, kind, detail }); };
    const snippet = (label: string, body: string, kind: SuggestionKind = 'keyword', detail?: string) => {
        items.push({ label, kind, detail, snippet: body });
    };
    const tagOf = (grid: string) => {
        const layer = sym?.layers.find(l => l.name === grid);
        return layer ? sym?.tags.find(t => t.name === layer.type) : undefined;
    };
    const tagValues = (tag: CompletionSymbols['tags'][number] | undefined) => {
        tag?.values.forEach(v => word(v.name, 'enumMember', tag.name));
        tag?.masks.forEach(u => word(u.name, 'enum', `named mask in ${tag.name}`));
    };
    const allTagValues = () => sym?.tags.forEach(t => tagValues(t));
    const grids = (detail?: (type: string) => string) =>
        sym?.layers.forEach(l => word(l.name, 'class', detail ? detail(l.type) : `grid of ${l.type}`));
    const graphs = () => sym?.graphs?.forEach(g => word(g.name, 'interface', `graph of ${g.source}`));
    const attributes = (detail: string) =>
        (sym?.node_attributes ?? NODE_ATTRIBUTES).forEach(a => word(a, 'property', ATTRIBUTE_DETAIL[a] ?? detail));
    const vars = (names: string[] | undefined) => names?.forEach(v => word(v, 'variable', 'pattern variable'));

    switch (ctx.kind) {
    case 'none':
        break;
    case 'top':
        snippet('use', 'use "${1:module.lvs}"', 'keyword', 'use another module (head of the file)');
        snippet('tag', 'tag ${1:name} { ${0} }');
        snippet('layers', 'layers {\n\t$0\n}');
        snippet('params', 'params {\n\t$0\n}');
        snippet('rule', 'rule ${1:name} {\n\t$0\n}');
        snippet('sequence', 'sequence ${1:name} {\n\t$0\n}');
        break;
    case 'maskMember': {   // a value or a named mask of the same tagset
        const tag = sym?.tags.find(t => t.name === ctx.tag);
        tag?.values.forEach(v => word(v.name, 'enumMember'));
        tag?.masks.forEach(m => word(m.name, 'enum', `named mask in ${ctx.tag}`));
        break;
    }
    case 'gridOf':
        snippet('grid of', 'grid of ${1}');
        snippet('graph of', 'graph of ${1}', 'keyword', 'a read-only view of a grid');
        break;
    case 'graphSource':
        grids();
        break;
    case 'graphArg':
        snippet('nodes', 'nodes=${1}', 'property', 'tag mask of the node cells');
        snippet('edges', 'edges=${1}', 'property', 'tag mask of the connector cells');
        break;
    case 'of':
        word('of', 'keyword');
        break;
    case 'gridType':
        sym?.tags.forEach(t => word(t.name, 'type', 'tag'));
        word('number', 'keyword');
        break;
    case 'paramType':
        snippet('number', 'number = ${1:0}');
        break;
    case 'expr': {
        sym?.builtins.forEach(b => snippet(b, `${b}(\${1})`, 'function', 'built-in'));
        // the graph functions take a node variable; the global queries read
        // the whole level, anywhere but in a param
        if (ctx.nodes)
            sym?.graph_functions?.filter(b => !GLOBAL_QUERIES.includes(b))
                .forEach(b => snippet(b, `${b}(\${1})`, 'function', 'graph function'));
        if (ctx.globals) {
            snippet('count', 'count(${1})', 'function', ctx.nodes ? 'count(s): cells a selector matches; count(n, s): of a region'
                                                                  : 'count(s): cells a selector matches');
            snippet('nodes', 'nodes(${1})', 'function', 'global query: the nodes of a graph');
            snippet('edges', 'edges(${1})', 'function', 'global query: the edges of a graph');
        }
        vars(ctx.vars);
        sym?.params.forEach(p => word(p.name, 'variable', p.derived ? 'derived param' : 'param'));
        if (ctx.grids) { grids(); graphs(); }   // a graph heads an attribute read at a cell
        if (ctx.pos) ['x', 'y', 'width', 'height'].forEach(n => word(n, 'constant', 'position'));
        allTagValues();
        break;
    }
    case 'member': {
        const graph = sym?.graphs?.some(g => g.name === ctx.head);
        if (graph) {   // `map.size`: the node at the cell
            attributes('of the node at the cell');
            break;
        }
        if (ctx.write) {   // a graph write `n.g = c`: any grid
            grids(t => `graph write: a grid of ${t}`);
            break;
        }
        attributes('node attribute');
        grids(t => `region read: every cell, a grid of ${t}`);
        break;
    }
    case 'ruleAttr':
        snippet('symmetry', 'symmetry=${1|horizontal,vertical,all,none|}', 'property');
        snippet('rotation', 'rotation=${1|all,90,180,270,none|}', 'property');
        break;
    case 'attrValue':
        if (ctx.attr === 'symmetry') ['horizontal', 'vertical', 'all', 'none'].forEach(v => word(v, 'enumMember'));
        if (ctx.attr === 'rotation') ['all', '90', '180', '270', 'none'].forEach(v => word(v, 'enumMember'));
        break;
    case 'ruleBody':
        if (ctx.block) {   // after a graph block: its guards, then '=>'
            snippet('when', 'when (${1})', 'keyword', 'rule guard: once per assignment');
            break;
        }
        if (ctx.start) {
            word('any', 'keyword', 'pick one sub-rule per match');
            word('all', 'keyword', 'apply every sub-rule');
            word('ordered', 'keyword', 'sub-rules in priority order');
        }
        grids();
        graphs();
        word('where', 'keyword', 'position pseudo-layer');
        if (ctx.guard) snippet('when', 'when (${1})', 'keyword', 'rule guard: once per candidate');
        break;
    case 'combinator':
        word('all', 'keyword');
        word('any', 'keyword');
        break;
    case 'weight':
        snippet('weight', 'weight=${1:1}', 'property');
        break;
    case 'cell':
        tagValues(tagOf(ctx.grid));
        break;
    case 'statement':
        snippet('once', 'once ${1:rule}', 'keyword', 'one application, or one iteration');
        snippet('scatter', 'scatter(${1:N}) ${2:rule}', 'keyword', 'up to N (or P%) of one batch');
        snippet('everywhere', 'everywhere ${1:rule}', 'keyword', 'every match of one batch');
        snippet('grow', 'grow ${1:rule}', 'keyword', 'steps, each seeing the last; grow(N) for N');
        snippet('settle', 'settle ${1:rule}', 'keyword', 'repeat until nothing changes; settle(N) for at most N');
        OPS.forEach(op => snippet(op.name, op.snippet, 'function', 'operation'));
        if (ctx.guard) snippet('when', 'when (${1})', 'keyword', 'guard');
        break;
    case 'ruleName':
        sym?.rules.forEach(r => word(r.name, 'method', 'rule'));
        sym?.sequences?.forEach(s => word(s.name, 'module', 'sequence'));
        break;
    case 'opArg': {
        const op = OPS.find(o => o.name === ctx.op);
        if (!op) break;
        const positional = op.params.filter(p => p.positional);
        const next = ctx.used.length === 0 ? positional[ctx.index] : undefined;
        next?.values?.forEach(v => word(v, 'enumMember'));
        for (const p of op.params)
            if (!p.positional && !ctx.used.includes(p.name))
                snippet(p.name, `${p.name}=\${1}`, 'property', p.required ? p.kind : `${p.kind}, optional`);
        break;
    }
    case 'opValue': {
        const p = OPS.find(o => o.name === ctx.op)?.params.find(q => q.name === ctx.param);
        if (p?.kind === 'grid') grids();
        else if (p?.kind === 'graph') graphs();
        else if (p?.kind === 'pred' || p?.kind === 'value') allTagValues();
        else p?.values?.forEach(v => word(v, 'enumMember'));
        break;
    }
    }
    return items;
}
