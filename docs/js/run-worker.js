// Runs one program off the main thread, so the lab can give up on a program that never
// reaches its fixpoint (the page terminates this worker) instead of freezing the tab.

importScripts('../wasm/ls_wasm.js');

const ready = createLsModule({ locateFile: f => new URL('../wasm/' + f, self.location.href).href });

// Same contract as the page's resolver: `use "path"` resolves against the bundled sample files.
function resolver(files, from) {
  return (path, importer) => {
    const parts = (importer || from).split('/').slice(0, -1).concat(path.split('/'));
    const out = [];
    parts.forEach(p => { if (p === '..') out.pop(); else if (p && p !== '.') out.push(p); });
    const name = out.join('/');
    if (name in files) return { name, source: files[name] };
    // a sample opened in the lab (lab.lvs) still finds the files next to it
    const near = Object.keys(files).filter(k => k.endsWith('/' + name));
    return near.length === 1 ? { name: near[0], source: files[near[0]] } : null;
  };
}

self.onmessage = async e => {
  const { token, source, name, seed, params, files } = e.data;
  try {
    const mod = await ready;
    const id = mod.run_begin(source, name, seed >>> 0, 'main', resolver(files, name), params);
    if (id < 0) { self.postMessage({ token, error: mod.run_last_error() }); return; }
    const t0 = performance.now();
    const state = mod.run_finish(id);
    const ms = performance.now() - t0;   // the run (and its state as JSON), for the status chip
    mod.run_end(id);
    self.postMessage({ token, state, ms });
  } catch (err) {
    self.postMessage({ token, error: String(err && err.message || err) });
  }
};
