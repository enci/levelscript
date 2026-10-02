#!/usr/bin/env node
// Runs the extension's parser + sema (the diagnostics engine) over every .ls
// file under ../../examples and checks the results against each file's
// `// @expect ...` annotations.
//
//   @expect error                  → analysis must report >= 1 error
//   @expect stderr-contains <text> → some error message must contain <text>
//                                    (with run-ok it names a runtime warning;
//                                    SKIPPED, static-only)
//   @expect run-ok                 → analysis must report 0 errors
//   @expect grid <g> count(...)    → runtime assertion; SKIPPED (static-only)
//   (no @expect directives)        → treated as run-ok (must be clean)
//
// Exit code is non-zero if any file fails, so this doubles as a CI gate.
//
//   node scripts/check-examples.js            # check all
//   node scripts/check-examples.js --verbose  # also list every diagnostic

const fs   = require('fs');
const path = require('path');
const createLsModule = require('../ls_wasm.js');

const VERBOSE = process.argv.includes('--verbose');
const EXAMPLES = path.resolve(__dirname, '..', '..', 'examples');

let wasmModule = null;

function walk(dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(p));
    else if (e.name.endsWith('.ls')) out.push(p);
  }
  return out;
}

function parseExpectations(src) {
  const exp = { error: false, contains: [], runOk: false, runtimeOnly: false };
  for (const line of src.split(/\r?\n/)) {
    const m = line.match(/^\s*\/\/\s*@expect\s+(.*)$/);
    if (!m) continue;
    const rest = m[1].trim();
    if (rest === 'error') exp.error = true;
    else if (rest === 'run-ok') exp.runOk = true;
    else if (rest.startsWith('stderr-contains ')) exp.contains.push(rest.slice('stderr-contains '.length).trim());
    else if (rest.startsWith('grid ')) exp.runtimeOnly = true; // runtime assertion, not static
  }
  return exp;
}

// `use` paths resolve relative to the using file (section 2.6), like lsc.
function resolve(usePath, from) {
  const full = path.resolve(path.dirname(from), usePath);
  try { return { name: full, source: fs.readFileSync(full, 'utf8') }; }
  catch { return null; }
}

function analyse(src, filename) {
  let all;
  try {
    const res = JSON.parse(wasmModule.inspect_json(src, filename, resolve));
    all = res.diagnostics || [];
  } catch (e) {
    return { errors: [{ message: 'analysis threw: ' + e.message, line: 0, col: 0 }], all: [] };
  }
  return { errors: all.filter(d => d.severity === 'error'), all };
}

async function run() {
  wasmModule = await createLsModule();
  
  const files = walk(EXAMPLES).sort();
let pass = 0, fail = 0, skipped = 0, stubs = 0;
const failures = [];

for (const file of files) {
  const rel = path.relative(EXAMPLES, file).replace(/\\/g, '/');
  const src = fs.readFileSync(file, 'utf8');
  const exp = parseExpectations(src);

  // Skip content-less stubs: no @expect directives and no actual code (empty or
  // comments/whitespace only) — such files assert nothing.
  const hasDirectives = exp.error || exp.contains.length > 0 || exp.runOk || exp.runtimeOnly;
  const code = src.replace(/\/\/[^\n]*/g, '').replace(/\s+/g, '');
  if (!hasDirectives && code === '') { stubs++; if (VERBOSE) console.log(`  skip [STUB ] ${rel}`); continue; }

  const { errors, all } = analyse(src, file);
  const msgs = errors.map(e => e.message);

  // Decide expectation: error-expecting wins; else run-ok/clean.
  const expectsError = exp.error || (exp.contains.length > 0 && !exp.runOk);
  const expectsClean = !expectsError; // run-ok, or no directive, or runtime-only

  const problems = [];
  if (expectsError) {
    if (errors.length === 0) problems.push('expected an error, got none');
    for (const needle of exp.contains)
      if (!msgs.some(m => m.includes(needle)))
        problems.push(`expected a diagnostic containing "${needle}"`);
  } else if (expectsClean) {
    if (errors.length > 0)
      problems.push(`expected clean, got ${errors.length} error(s): ${msgs.join(' | ')}`);
  }

  if (problems.length === 0) {
    pass++;
    if (VERBOSE) {
      const tag = expectsError ? 'ERR-OK' : 'CLEAN ';
      console.log(`  ok   [${tag}] ${rel}`);
      if (VERBOSE) for (const d of all) console.log(`         ${d.severity} ${d.line}:${d.col} ${d.message}`);
    }
  } else {
    fail++;
    failures.push({ rel, problems });
  }
  if ((exp.runtimeOnly || exp.contains.length > 0) && !expectsError) skipped++; // runtime assertions not verified here
}

console.log('');
for (const f of failures) {
  console.log(`  FAIL ${f.rel}`);
  for (const p of f.problems) console.log(`         - ${p}`);
}
console.log('');
console.log(`${files.length} files: ${pass} passed, ${fail} failed, ${stubs} empty stub(s) skipped`);
if (skipped) console.log(`(${skipped} files also carry runtime grid-count assertions, not verified by the static checker)`);

process.exit(fail === 0 ? 0 : 1);
}

run().catch(console.error);
