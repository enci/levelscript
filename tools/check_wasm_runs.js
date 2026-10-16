#!/usr/bin/env node
// Runs every sample through the WASM build (the one the website uses) and compares
// the result with its golden .expected, the same text `levelscript` prints.
// Guards the "live" demos against drifting from the native implementation.
//
//   node tools/check_wasm_runs.js [path/to/ls_wasm.js]
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const wasmJs = path.resolve(process.argv[2] || path.join(ROOT, 'extension', 'ls_wasm.js'));
const createLsModule = require(wasmJs);

function format(level) {
  let out = '';
  for (const layer of level.layers) {
    out += `=== ${layer.name} ===\n`;
    for (let y = 0; y < level.height; y++) {
      const row = [];
      for (let x = 0; x < level.width; x++) {
        const c = layer.cells[y * level.width + x];
        row.push(layer.isNumber ? (c === null ? '.' : String(c)) : (c.length ? c[0][0] : '.'));
      }
      out += row.join(' ') + '\n';
    }
    out += '\n';
  }
  return out;
}

(async () => {
  const m = await createLsModule();
  const dir = path.join(ROOT, 'samples');
  let failed = 0, checked = 0;
  for (const f of fs.readdirSync(dir).filter(n => n.endsWith('.lvs')).sort()) {
    const exp = path.join(dir, f.replace(/\.lvs$/, '.expected'));
    if (!fs.existsSync(exp)) continue;
    const file = path.join(dir, f);
    const src = fs.readFileSync(file, 'utf8');
    const seed = +((src.match(/\/\/\s*@seed\s+(\d+)/) || [])[1] ?? 42);
    const resolve = (p, from) => {
      const full = path.resolve(path.dirname(from), p);
      try { return { name: full, source: fs.readFileSync(full, 'utf8') }; } catch { return null; }
    };
    const id = m.run_begin(src, file, seed, 'main', resolve, undefined);
    checked++;
    if (id < 0) { console.log(`FAIL ${f}: ${m.run_last_error()}`); failed++; continue; }
    const got = format(JSON.parse(m.run_finish(id)).level);
    m.run_end(id);
    if (got !== fs.readFileSync(exp, 'utf8')) { console.log(`FAIL ${f}: output differs from ${path.basename(exp)}`); failed++; }
  }
  console.log(`${checked - failed}/${checked} samples match their goldens`);
  process.exit(failed ? 1 : 0);
})();
