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

const decls = `use "lib.lvs"
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
    ['tag t { F, W }\ntag u { A, B = A | ▮', { kind: 'maskMember', tag: 'u' }],
    ['tag u { A, B = !▮', { kind: 'maskMember', tag: 'u' }],
    ['tag u { A, B = !(A | ▮', { kind: 'maskMember', tag: 'u' }],
    ['tag u { A, B = !(▮', { kind: 'maskMember', tag: 'u' }],
    ['tag t { F, ▮', { kind: 'none' }],                  // declaring a value
    ['layers { g: ▮', { kind: 'gridOf' }],
    ['layers { g: grid ▮', { kind: 'of' }],
    ['layers { g: grid of ▮', { kind: 'gridType' }],
    ['layers { g: grid of t\n▮', { kind: 'none' }],      // next entry's name
    ['layers { m: graph ▮', { kind: 'of' }],
    ['layers { m: graph of ▮', { kind: 'graphSource' }],
    ['layers { m: graph of g(▮', { kind: 'graphArg' }],
    ['layers { m: graph of g(nodes=F, ▮', { kind: 'graphArg' }],
    ['layers { m: graph of g(nodes=▮', { kind: 'none' }],
    ['layers { m: graph of g(nodes=F)\n▮', { kind: 'none' }],
    ['params { p: ▮', { kind: 'paramType' }],
    ['params { p: number = ▮', { kind: 'expr', grids: false, pos: false, nodes: false }],
    ['params { p: number = 2\n q = p * ▮', { kind: 'expr', grids: false, pos: false, nodes: false }],
    ['params { p: number = 2\n▮', { kind: 'none' }],

    // rules
    ['rule r(▮', { kind: 'ruleAttr' }],
    ['rule r(symmetry=all, ▮', { kind: 'ruleAttr' }],
    ['rule r(symmetry=▮', { kind: 'attrValue', attr: 'symmetry' }],
    ['rule r(rotation=▮', { kind: 'attrValue', attr: 'rotation' }],
    ['rule r(rotation={▮', { kind: 'none' }],
    ['rule r {\n    ▮', { kind: 'ruleBody', start: true, guard: false }],
    ['rule r { g[F] => ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['rule r { any\n    g[F] => g[W]\n    ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['rule r { g▮', { kind: 'ruleBody', start: true, guard: false }],   // typing the grid name
    ['rule r { g ▮', { kind: 'none' }],                   // '[' expected
    ['rule r { g[F] => {▮', { kind: 'combinator' }],
    ['rule r { g[F] => { all ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['rule r { g[F] => { all g[W] {▮', { kind: 'combinator' }],   // a nested write block
    ['rule r { {▮', { kind: 'ruleBody', start: false, guard: false }],           // a match-side group
    ['rule r { { g[F] ▮', { kind: 'ruleBody', start: false, guard: true }],
    ['rule r { all\n    { g[F] } => g[W]\n    {▮', { kind: 'ruleBody', start: false, guard: false }],
    ['rule r { g[F] => { any (▮', { kind: 'weight' }],
    ['rule r { g[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F ▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F|▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[\n        F W\n        ▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F] => g[ (▮', { kind: 'expr', grids: true, pos: true, nodes: true, head: 'g' }],
    ['rule r { g[F] => g[ (min(n, ▮', { kind: 'expr', grids: true, pos: true, nodes: true, head: 'g' }],
    ['rule r { m[?n] when (distance(?n, g[F]) > ▮', { kind: 'expr', grids: false, pos: false, nodes: true }],
    ['rule r { m[ (distance(g[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g[F] => g[ (k ▮', { kind: 'none' }],
    ['rule r { g[.] where[ (x == ▮', { kind: 'expr', grids: true, pos: true, nodes: true }],
    ['rule r { g[.] where[ ▮', { kind: 'none' }],
    // sized patterns `g(W, H)[c]`: the '(' is a size, not a weight
    ['rule r { g(▮', { kind: 'none' }],
    ['rule r { g(8, ▮', { kind: 'none' }],
    ['rule r { g(8, 6) ▮', { kind: 'none' }],             // '[' expected
    ['rule r { g(8, 6)[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g(8, 6)[ (▮', { kind: 'expr', grids: true, pos: true, nodes: true, head: 'g' }],
    ['rule r { g(8, 6)[.] where(8, 6)[ (x == ▮', { kind: 'expr', grids: true, pos: true, nodes: true }],
    ['rule r { g(8, 6)[.] ▮', { kind: 'ruleBody', start: false, guard: true }],
    // rule guards: after a match-side pattern, before '=>'
    ['rule r { g[F] when (▮', { kind: 'expr', grids: false, pos: false, nodes: true }],
    ['rule r { g[F] when (k > 1) ▮', { kind: 'ruleBody', start: false, guard: true }],
    ['rule r { { g[F] when (k > 1) ▮', { kind: 'ruleBody', start: false, guard: true }],
    ['rule r { g[F] => g[W] ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['rule r { all\n    g[F] => g[W]\n    g[W] ▮', { kind: 'ruleBody', start: false, guard: true }],
    ['rule r { all\n    g[F] => { any g[W] g[D] }\n    ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['sequence main {\n    everywhere { g[F] ▮', { kind: 'ruleBody', start: false, guard: true }],
    ['rule r { g(8, 6)[.] => g(8, 6)[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g(2, 1)[.] => { any g(2, 1)[▮', { kind: 'cell', grid: 'g' }],
    ['rule r { g(2, 1)[.] => { any (▮', { kind: 'weight' }],
    ['rule r { { g(8, 6)[▮', { kind: 'cell', grid: 'g' }],

    // modules
    ['use "schema.lvs"\n▮', { kind: 'top' }],
    ['use "sche▮', { kind: 'none' }],                     // typing a path
    ['use ▮', { kind: 'none' }],

    // sequences
    ['sequence ▮', { kind: 'none' }],
    ['sequence s(▮', { kind: 'none' }],
    ['sequence s {\n    ▮', { kind: 'statement', guard: false }],
    ['sequence s {\n    everywhere r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence s {\n    settle(2) ▮', { kind: 'ruleName' }],
    [decls + 'sequence s { everywhere r }\n\n▮', { kind: 'top' }],

    // statements (the body of a sequence)
    ['sequence main {\n    ▮', { kind: 'statement', guard: false }],
    ['sequence main {\n    resize(4, 4)\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    once r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    scatter(3) r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    grow r\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    mirror(horizontal) when (k > 1)\n    ▮', { kind: 'statement', guard: false }],
    ['sequence main {\n    once ▮', { kind: 'ruleName' }],
    ['sequence main {\n    everywhere ▮', { kind: 'ruleName' }],
    ['sequence main {\n    grow ▮', { kind: 'ruleName' }],
    ['sequence main {\n    settle ▮', { kind: 'ruleName' }],
    ['sequence main {\n    scatter ▮', { kind: 'none' }],          // '(' expected
    ['sequence main {\n    scatter(2) ▮', { kind: 'ruleName' }],
    ['sequence main {\n    grow(k * 2) ▮', { kind: 'ruleName' }],
    ['sequence main {\n    scatter(50%) ▮', { kind: 'ruleName' }],
    ['sequence main {\n    scatter(▮', { kind: 'expr', grids: false, pos: false, nodes: false }],   // params only
    // inline rules
    ['sequence main {\n    everywhere {▮', { kind: 'ruleBody', start: true, guard: false }],
    ['sequence main {\n    everywhere { g[F] => ▮', { kind: 'ruleBody', start: false, guard: false }],
    ['sequence main {\n    once (▮', { kind: 'ruleAttr' }],
    ['sequence main {\n    once (symmetry=▮', { kind: 'attrValue', attr: 'symmetry' }],
    ['sequence main {\n    scatter(3) (rotation=all) {▮', { kind: 'ruleBody', start: true, guard: false }],
    ['sequence main {\n    settle { g[F] => g[W] }\n    ▮', { kind: 'statement', guard: true }],
    ['sequence main {\n    settle(k - ▮', { kind: 'expr', grids: false, pos: false, nodes: false }],
    ['sequence main {\n    mirror(▮', { kind: 'opArg', op: 'mirror', index: 0, used: [] }],
    ['sequence main {\n    path(from=a, ▮', { kind: 'opArg', op: 'path', index: 1, used: ['from'] }],
    ['sequence main {\n    path(into=▮', { kind: 'opValue', op: 'path', param: 'into' }],
    ['sequence main {\n    path(cost=(▮', { kind: 'expr', grids: true, pos: true, nodes: false }],
    ['sequence main {\n    resize(4, 4) when (▮', { kind: 'expr', grids: false, pos: false, nodes: false }],
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
    const ops = JSON.parse(wasm.inspect_json('sequence main { }', 'x.lvs')).symbols.ops;
    const mine = OPS.map(o => o.name);
    if (JSON.stringify(ops) !== JSON.stringify(mine)) {
        fail++;
        console.log(`FAIL operation table out of sync with the compiler:\n     compiler ${ops}\n     extension ${mine}`);
    }
    console.log(`${cases.length} completion contexts + op table: ${fail ? fail + ' failed' : 'all pass'}`);
    process.exit(fail ? 1 : 0);
})();
