// Completion context: where in the grammar (spec.md) the cursor sits, decided
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
    | { kind: 'unionMember'; tag: string }              // tag t { ..., D = F | _ }
    | { kind: 'gridOf' }                                // layers { g: _ }
    | { kind: 'of' }                                    // layers { g: grid _ }
    | { kind: 'gridType' }                              // layers { g: grid of _ }
    | { kind: 'paramType' }                             // params { p: _ }
    | { kind: 'expr'; grids: boolean; pos: boolean }    // expressions (section 5.8)
    | { kind: 'ruleAttr' }                              // rule r(_)
    | { kind: 'attrValue'; attr: string }               // rule r(symmetry=_)
    | { kind: 'ruleBody'; start: boolean }              // a pattern may start here
    | { kind: 'combinator' }                            // => { _   (all / any)
    | { kind: 'weight' }                                // { any (_ ) g[...] }
    | { kind: 'cell'; grid: string }                    // g[ _ ]
    | { kind: 'statement'; guard: boolean }             // sequence s { _ }
    | { kind: 'ruleName' }                              // sequence s { once _ } - rules and sequences
    | { kind: 'opArg'; op: string; index: number; used: string[] }  // path(_)
    | { kind: 'opValue'; op: string; param: string };   // path(into=_)

// Operation parameter schemas (spec section 6.0). The op names are checked
// against the compiler's inspect `ops` list by scripts/check-completion.js.
export type OpParamKind = 'int' | 'grid' | 'pred' | 'value' | 'expr' | 'enum';
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
               { name: 'over', kind: 'grid', positional: false, required: false },
               { name: 'passable', kind: 'pred', positional: false, required: false },
               { name: 'connectivity', kind: 'enum', positional: false, required: false, values: ['4', '8'] },
               { name: 'cost', kind: 'expr', positional: false, required: false }] },
];

interface Tok { t: string; owner?: string }   // t: text; owner set on synthetic closers
interface Frame {
    open: '{' | '[' | '(' | '';
    owner: string;       // what opened it: 'top' 'tag:t' 'layers' 'params' 'rule' 'statements' 'combinator' 'group' 'set' 'grid:g' 'where' 'attrs' 'count:grow' 'op:path' 'when' 'weight' 'expr'
    scope: 'full' | 'restricted' | '';   // expression scope inherited by '(' frames
    toks: Tok[];
}

const STRING = '<string>';   // token text for any string literal
const OPERATORS = new Set(['+', '-', '*', '/', '==', '!=', '<', '<=', '>', '>=', '&&', '||', '|', '!']);
// Modes (section 6). scatter always takes a count; grow and settle may.
const MODES = new Set(['once', 'scatter', 'everywhere', 'grow', 'settle']);
const COUNTED = new Set(['scatter', 'grow', 'settle']);

function isIdentStart(c: string) { return /[A-Za-z_]/.test(c); }
function isIdent(c: string) { return /[A-Za-z0-9_]/.test(c); }

export function completionContext(text: string, offset: number): CompletionContext {
    // the word being typed is the filter text, not context
    let wordStart = offset;
    while (wordStart > 0 && isIdent(text[wordStart - 1])) wordStart--;

    const stack: Frame[] = [{ open: '', owner: 'top', scope: '', toks: [] }];
    const top = () => stack[stack.length - 1];

    let i = 0;
    while (i < wordStart) {
        const c = text[i];
        if (c === '/' && text[i + 1] === '/') {
            const eol = text.indexOf('\n', i);
            if (eol < 0 || eol >= offset) return { kind: 'none' };   // typing in a comment
            i = eol;
            continue;
        }
        if (/\s/.test(c)) { i++; continue; }
        if (c === '"') {   // a `use` path (section 2.6): one token, no escapes
            let j = i + 1;
            while (j < text.length && text[j] !== '"' && text[j] !== '\n') j++;
            if (j >= offset || text[j] !== '"') return { kind: 'none' };   // typing a path
            top().toks.push({ t: STRING });
            i = j + 1;
            continue;
        }
        if (isIdentStart(c) || /[0-9]/.test(c)) {
            let j = i + 1;
            while (j < wordStart && isIdent(text[j])) j++;
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
            stack.push(openFrame(c, stack));
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
    return classify(stack);
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
    const frame = (owner: string, scope: Frame['scope'] = ''): Frame =>
        ({ open: c, owner, scope, toks: [] });

    if (c === '{') {
        if (parent.owner === 'top') {
            const decl = currentDecl(toks);
            const kw = decl[0]?.t;
            if (kw === 'tag') return frame('tag:' + (decl[1]?.t ?? ''));
            if (kw === 'layers' || kw === 'params') return frame(kw);
            if (kw === 'sequence') return frame('statements');   // a statement_list (section 6)
            if (kw === 'rule') return frame('rule');
            return frame('other');
        }
        // an inline rule's body: after a mode, a count, or its attributes (section 6)
        if (parent.owner === 'statements' && last && (endsMode(last) || (last.t === ')' && last.owner === 'attrs')))
            return frame('rule');
        if (parent.owner === 'attrs' && prev === '=' && prev2 === 'rotation') return frame('set');
        // after '=>' (and inside a write block) a combinator block; otherwise
        // braces only group a match side (section 5.2)
        if (parent.owner === 'combinator' || (parent.owner === 'rule' && prev === '=>'))
            return frame('combinator');
        if (parent.owner === 'rule') return frame('group');
        return frame('other');
    }
    if (c === '[') {
        if (prev === 'where') return frame('where');
        if (prev && isIdentStart(prev[0])) return frame('grid:' + prev);
        return frame('other');
    }
    // '('
    if (parent.owner === 'top' && currentDecl(toks)[0]?.t === 'rule') return frame('attrs');
    if (parent.owner === 'statements') {
        if (prev && COUNTED.has(prev)) return frame('count:' + prev, 'restricted');   // params only
        if (last && endsMode(last)) return frame('attrs');   // an inline rule's attributes
        if (prev === 'when') return frame('when', 'restricted');
        if (prev && isIdentStart(prev[0])) return frame('op:' + prev, 'full');
        return frame('other');
    }
    if (parent.owner === 'rule' || parent.owner === 'combinator') return frame('weight');
    if (parent.owner === 'params') return frame('expr', 'restricted');
    if (parent.owner.startsWith('grid:') || parent.owner === 'where') return frame('expr', 'full');
    if (parent.scope) return frame('expr', parent.scope);
    return frame('other');
}

// Does this token end a mode, so a rule or sequence name follows? A mode
// word that takes no count yet, or the ')' closing a count.
function endsMode(t: Tok) {
    return (MODES.has(t.t) && t.t !== 'scatter') || (t.t === ')' && !!t.owner?.startsWith('count:'));
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
        if (prev === 'grid') return { kind: 'of' };
        if (prev === 'of') return { kind: 'gridType' };
        return none;
    case 'params':
        if (prev === ':') return { kind: 'paramType' };
        // `p: number = _` or `p = _`, and operands after operators - not a new entry's name
        if (prev === '=' || (prev !== undefined && OPERATORS.has(prev)))
            return { kind: 'expr', grids: false, pos: false };
        return none;
    case 'attrs':
        if (prev === undefined || prev === ',') return { kind: 'ruleAttr' };
        if (prev === '=' && prev2) return { kind: 'attrValue', attr: prev2 };
        return none;
    case 'group':
        return prev === undefined || prev === ']' ? { kind: 'ruleBody', start: false } : none;
    case 'rule':
    case 'combinator': {
        if (prev === undefined) return f.owner === 'rule' ? { kind: 'ruleBody', start: true } : { kind: 'combinator' };
        if (prev === '=>' || prev === ']' || prev === '}' || prev === ')' || prev === ',' ||
            prev === 'any' || prev === 'all' || prev === 'ordered')
            return { kind: 'ruleBody', start: false };
        return none;
    }
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
        return prev === '=' || prev === '|' ? { kind: 'unionMember', tag: f.owner.slice(4) } : none;
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
    if (f.owner === 'when' || f.owner === 'expr' || f.owner.startsWith('count:'))
        return exprMayStart(prev)
            ? { kind: 'expr', grids: f.scope === 'full', pos: f.scope === 'full' }
            : none;
    return none;
}
