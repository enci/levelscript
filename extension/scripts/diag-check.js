// Ad-hoc harness: run the compiled parser+sema over a source string and print diagnostics.
// Usage: node scripts/diag-check.js
const { parse } = require('../out/src/language/parser');
const { check } = require('../out/src/language/sema');

const src = `
tag algo { S, W }
layers { level: grid of algo }

rule bad {
  level[
    * * *
    * S W
    * * *
  ]
  =>
  level[
    S W W
    W W S
  ]
}

program {
  resize(10, 10)
  one bad
}
`;

const { ast, diagnostics: parseDiags } = parse(src);
const sema = check(ast, true);
const all = [...parseDiags, ...sema.diagnostics];
console.log('parse diags:', parseDiags.length, 'sema diags:', sema.diagnostics.length);
for (const d of all) {
  console.log(`  [${d.severity}] ${d.span.line}:${d.span.col} ${d.message}`);
}
