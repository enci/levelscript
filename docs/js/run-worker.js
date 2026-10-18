// Runs one program off the main thread, so the sandbox can give up on a program that never
// reaches its fixpoint (the page terminates this worker) instead of freezing the tab.

importScripts('../wasm/ls_wasm.js');

const ready = createLsModule({ locateFile: f => new URL('../wasm/' + f, self.location.href).href });

// Same contract as the page's resolver: `use "path"` resolves against the bundled modules.
function resolver(files, from) {
  return (path, importer) => {
    const parts = (importer || from).split('/').slice(0, -1).concat(path.split('/'));
    const out = [];
    parts.forEach(p => { if (p === '..') out.pop(); else if (p && p !== '.') out.push(p); });
    const name = out.join('/');
    return name in files ? { name, source: files[name] } : null;
  };
}

self.onmessage = async e => {
  const { token, source, name, seed, params, files } = e.data;
  try {
    const mod = await ready;
    const id = mod.run_begin(source, name, seed >>> 0, 'main', resolver(files, name), params);
    if (id < 0) { self.postMessage({ token, error: mod.run_last_error() }); return; }
    const state = mod.run_finish(id);
    mod.run_end(id);
    self.postMessage({ token, state });
  } catch (err) {
    self.postMessage({ token, error: String(err && err.message || err) });
  }
};
