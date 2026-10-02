#!/usr/bin/env node
// Checks the completion-context classifier (src/completion.ts, compiled to
// out/src/completion.js): each case is a source with the cursor at '▮' and
// the context kind (plus fields) it must produce. Also checks the extension's
// operation table against the compiler's (inspect `ops`).
//
//   node scripts/check-completion.js

const path = require('path');
const { completionContext, OPS } = require('../out/src/completion.js');
const createLsModule = require('../ls_wasm.js');

const decls = `use "lib.ls"
tag t { F, W, D = F | W }
layers {
    g: grid of t
    n: grid of number
}
params { k: number = 2 }
rule r { g[F] => g[W] }
`;

const cases = [
    // top level: the reported bug - blank lines between declarations
    ['▮', { kind: 'top' }],
    [decls + '\n\n▮', { kind: 'top' }],
    [decls + '\nru▮', { kind: 'top' }],
    ['tag ▮', { kind: 'none' }],                         // declaring a name
    ['rule ▮', { kind: 'none' }],
    ['rule r(symmetry=all) ▮', { kind: 'none' }],
    ['// a comment ▮', { kind: 'none' }],
    ['rule r { g[F] // note ▮', { kind: 'none' }],

    // tag / layers / params blocks
    ['tag t { F, W }\ntag u { A, B = A | ▮', { kind: 'unionMember', tag: 'u' }],
    ['tag t { F, ▮', { kind: 'none' }],                  // declaring a value
    ['layers { g: ▮', { kind: 'gridOf' }],
    ['layers { g: grid ▮', { kind: 'of' }],
    ['layers { g: grid of ▮', { kind: 'gridType' }],
    ['layers { g: grid of t\n▮', { kind: 'none' }],      // next entry's name
    ['params { p: ▮', { kind: 'paramType' }],
    ['params { p: number = ▮', { kind: 'expr', grids: false, pos: false }],
    ['params { p: number = 2\n q = p * ▮', { kind: 'expr', grids: false, pos: false }],
    ['params { p: number = 2\n▮', { kind: 'none' }],

    // rules
    ['rule r(▮', { kind: 'ruleAttr' }],
    ['rule r(symmetry=all, ▮', { kind: 'ruleAttr' }],
    ['rule r(symmetry=▮', { kind: 'attrValue', attr: 'symmetry' }],
    ['rule r(rotation=▮', { kind: 'attrValue', attr: 'rotation' }],
    ['rule r(rotation={▮', { kind: 'none' }],
    ['rule r {\n    ▮', { kind: 'ruleBody', start: true }],
    ['rule r { g[F] => ▮', { kind: 'ruleBody', start: false }],
    ['rule r { any\n    g[F] => g[W]\n    ▮', { kind: 'ruleBody', start: false }],
    ['rule r { g▮', { kind: 'ruleBody', start: true }],   // typing the grid name
    ['rule r { g ▮', { kind: 'none' }],                   // '[' expected
    ['rule r { {▮', { kind: 'combinator' }],
    ['rule r { { all ▮', { kind: 'ruleBody', start: false }],
    ['rule r { g[F] => { any (▮', { kind: 'weight' }],
    ['rule r { g[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F ▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F|▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[\n        F W\n        ▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F] => g[ (▮', { kind: 'expr', grids: true, pos: true }],
    ['rule r { g[F] => g[ (min(n, ▮', { kind: 'expr', grids: true, pos: true }],
    ['rule r { g[F] => g[ (k ▮', { kind: 'none' }],
    ['rule r { { all g[.] where[ (x == ▮', { kind: 'expr', grids: true, pos: true }],
    ['rule r { { all g[.] where[ ▮', { kind: 'none' }],

    // modules (section 2.6)
    ['use "schema.ls"\n▮', { kind: 'top' }],
    ['use "sche▮', { kind: 'none' }],                     // typing a path
    ['use ▮', { kind: 'none' }],

    // sequences (section 6.10)
    ['sequence ▮', { kind: 'none' }],
    ['sequence s(▮', { kind: 'none' }],
    ['sequence s {\n    ▮', { kind: 'statement', guard: false }],
    ['sequence s {\n    all r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence s {\n    some(max=2) ▮', { kind: 'ruleName' }],
    [decls + 'sequence s { all r }\n\n▮', { kind: 'top' }],

    // statements (the body of a sequence)
    ['sequence main {\n    ▮', { kind: 'statement', guard: false }],
    ['sequence main {\n    resize(4, 4)\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    one r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    some(max=3) r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    mirror(horizontal) when (k > 1)\n    ▮', { kind: 'statement', guard: false }],
    ['sequence main {\n    one ▮', { kind: 'ruleName' }],
    ['sequence main {\n    all(policy=incremental) ▮', { kind: 'ruleName' }],
    ['sequence main {\n    some(max=2) ▮', { kind: 'ruleName' }],
    ['sequence main {\n    some(▮', { kind: 'strategyArg', strategy: 'some' }],
    ['sequence main {\n    one(policy=▮', { kind: 'policyValue' }],
    ['sequence main {\n    mirror(▮', { kind: 'opArg', op: 'mirror', index: 0, used: [] }],
    ['sequence main {\n    path(from=a, ▮', { kind: 'opArg', op: 'path', index: 1, used: ['from'] }],
    ['sequence main {\n    path(into=▮', { kind: 'opValue', op: 'path', param: 'into' }],
    ['sequence main {\n    path(cost=(▮', { kind: 'expr', grids: true, pos: true }],
    ['sequence main {\n    resize(4, 4) when (▮', { kind: 'expr', grids: false, pos: false }],
    ['sequence main {\n    resize(4, 4) when ▮', { kind: 'none' }],
    ['sequence main {\n    resize ▮', { kind: 'none' }],
];

let fail = 0;
for (const [marked, want] of cases) {
    const offset = marked.indexOf('▮');
    const src = marked.replace('▮', '');
    const got = completionContext(src, offset);
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        fail++;
        console.log(`FAIL ${JSON.stringify(marked)}\n     want ${JSON.stringify(want)}\n     got  ${JSON.stringify(got)}`);
    }
}

(async () => {
    const wasm = await createLsModule();
    const ops = JSON.parse(wasm.inspect_json('sequence main { }', 'x.ls')).symbols.ops;
    const mine = OPS.map(o => o.name);
    if (JSON.stringify(ops) !== JSON.stringify(mine)) {
        fail++;
        console.log(`FAIL operation table out of sync with the compiler:\n     compiler ${ops}\n     extension ${mine}`);
    }
    console.log(`${cases.length} completion contexts + op table: ${fail ? fail + ' failed' : 'all pass'}`);
    process.exit(fail ? 1 : 0);
})();
