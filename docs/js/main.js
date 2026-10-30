// LevelScript website - highlighting, live generator widget, gallery, spec viewer.
// Programs run in the browser through the same WASM build the VS Code extension uses.

(function () {
  'use strict';

  // ---------- nav ----------
  const here = location.pathname.split('/').pop() || 'index.html';
  document.querySelectorAll('nav .nav-links a').forEach(a => {
    if (a.getAttribute('href') === here) a.classList.add('active');
  });
  document.querySelectorAll('nav .nav-toggle').forEach(toggle => {
    toggle.addEventListener('click', () => {
      const open = toggle.getAttribute('aria-expanded') !== 'true';
      toggle.setAttribute('aria-expanded', open);
      toggle.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation');
      toggle.closest('nav').classList.toggle('nav-open', open);
    });
  });

  // ---------- syntax highlighting for .lvs ----------
  // The vocabulary comes from the grammar (window.LVS_EDITOR, built by tools/build_site_data.py);
  // the lexer and renderers live in editor-model.js.
  const model = window.LVS_model;

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function highlight(src) {
    return model.highlightHtml(src, window.LVS_EDITOR);
  }

  document.querySelectorAll('pre code.lvs, pre code.language-ls, pre code.language-lvs').forEach(el => {
    el.innerHTML = highlight(el.textContent);
  });

  // ---------- WASM runtime ----------
  let runtime = null;
  function loadRuntime() {
    if (runtime) return runtime;
    runtime = new Promise((resolve, reject) => {
      if (typeof createLsModule !== 'function') {
        reject(new Error('The WebAssembly build (wasm/ls_wasm.js) is not on this server. ' +
          'See tools/build_site_data.py for how to build it.'));
        return;
      }
      createLsModule().then(resolve, reject);
    });
    return runtime;
  }

  // `use "path"` inside a sample resolves against the bundled sample modules.
  function resolver(from) {
    const files = window.LVS_FILES || {};
    return (path, importer) => {
      const parts = (importer || from).split('/').slice(0, -1).concat(path.split('/'));
      const out = [];
      parts.forEach(p => { if (p === '..') out.pop(); else if (p && p !== '.') out.push(p); });
      const name = out.join('/');
      return name in files ? { name, source: files[name] } : null;
    };
  }

  // ---------- drawing ----------
  const NAMED = {
    wall: '#26413a', rock: '#3b4a45', floor: '#cfdca4', sentinel: '#f5dd7e', seed: '#ffb86b',
    ocean: '#1d4f7c', coast: '#e8d9a0', land: '#58b368', highland: '#8a7a56', peak: '#f2f4f5',
    water: '#2f7fc2', grass: '#58b368', lava: '#f0622d', open: '#cfdca4',
    player: '#ffffff', chest: '#f5b83d', heart: '#ef476f', potion: '#7b5cff', sword: '#c9d1d6',
    shield: '#4aa3df', skeleton_knife: '#e8e4d0', skeleton_axe: '#d8cfa8', skull: '#e0e0e0',
    necromancer: '#b44bd1', door: '#c98a3c', exit: '#3fe0a5', road: '#b8a37a', start: '#3fe0a5',
    dead_end: '#ef476f', guard: '#f5b83d', hub: '#4aa3df', room: '#7fb8a6', hall: '#cfdca4',
    head: '#ffd166', trail: '#3fc46e', gold: '#f5c542', plain: '#9db88c',
    goblin: '#6ab04c', troll: '#8e6e53', dragon: '#e55039'
  };

  function hashColor(s) {
    let h = 0;
    for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return 'hsl(' + (h % 360) + ' 55% 62%)';
  }

  // A cell is an array of tag value names ([] = empty) or, in number layers, a number / null.
  function cellColor(layer, cell) {
    if (layer.isNumber) {
      const v = Math.min(255, 40 + (cell * 7) % 200);
      return 'rgb(' + v + ',' + v + ',' + v + ')';
    }
    const name = cell[0];
    return NAMED[name] || hashColor(name);
  }

  function isEmpty(layer, cell) {
    return layer.isNumber ? cell === null : cell.length === 0;
  }

  const HIDDEN_BY_DEFAULT = new Set(['a', 'algo']);

  // state: parsed run_* JSON. visible: Set of layer names. Returns where the cells were drawn.
  function drawState(canvas, state, visible) {
    const lvl = state.level;
    const w = lvl.width, h = lvl.height;
    const g = canvas.getContext('2d');
    g.clearRect(0, 0, canvas.width, canvas.height);
    if (!w || !h) return null;
    const cs = Math.max(2, Math.floor(Math.min(canvas.width / w, canvas.height / h)));
    const ox = Math.floor((canvas.width - w * cs) / 2);
    const oy = Math.floor((canvas.height - h * cs) / 2);
    let first = true;
    lvl.layers.forEach(layer => {
      if (!visible.has(layer.name)) return;
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const cell = layer.cells[y * w + x];
          if (isEmpty(layer, cell)) {
            if (first) { g.fillStyle = '#15231f'; g.fillRect(ox + x * cs, oy + y * cs, cs, cs); }
            continue;
          }
          g.fillStyle = cellColor(layer, cell);
          if (first) g.fillRect(ox + x * cs, oy + y * cs, cs, cs);
          else {
            const r = Math.max(1.5, cs * 0.36);
            g.beginPath();
            g.arc(ox + x * cs + cs / 2, oy + y * cs + cs / 2, r, 0, Math.PI * 2);
            g.fill();
            g.lineWidth = 1;
            g.strokeStyle = 'rgba(0,0,0,0.55)';
            g.stroke();
          }
        }
      }
      first = false;
    });
    if (cs >= 8) {
      g.strokeStyle = 'rgba(0,0,0,0.12)';
      g.lineWidth = 1;
      g.beginPath();
      for (let x = 0; x <= w; x++) { g.moveTo(ox + x * cs + 0.5, oy); g.lineTo(ox + x * cs + 0.5, oy + h * cs); }
      for (let y = 0; y <= h; y++) { g.moveTo(ox, oy + y * cs + 0.5); g.lineTo(ox + w * cs, oy + y * cs + 0.5); }
      g.stroke();
    }
    // the cells the last rule application matched / wrote; a finished run shows just the level
    (state.done ? [] : state.highlights || []).forEach(hl => {
      const layer = lvl.layers[hl.layer];
      if (!layer || !visible.has(layer.name)) return;
      g.lineWidth = 2;
      g.strokeStyle = hl.what === 'write' ? '#3fe0a5' : '#f5dd7e';
      g.strokeRect(ox + hl.x * cs + 1, oy + hl.y * cs + 1, cs - 2, cs - 2);
    });
    return { cs, ox, oy, w, h };
  }

  function defaultVisible(lvl) {
    const vis = new Set(lvl.layers.filter(l => !HIDDEN_BY_DEFAULT.has(l.name)).map(l => l.name));
    return vis.size ? vis : new Set(lvl.layers.map(l => l.name));
  }

  function legendFor(lvl, visible) {
    const seen = new Set();
    lvl.layers.forEach(l => {
      if (l.isNumber || !visible.has(l.name)) return;
      l.cells.forEach(c => c.forEach(n => seen.add(n)));
    });
    return [...seen].filter(n => NAMED[n]);
  }

  // ---------- the widget ----------
  // A runnable generator: editable source, seed + params, Run, and stepping through
  // rule applications. `opts.editor` false hides the source (used by the hero).
  function createWidget(host, sample, opts) {
    opts = opts || {};
    const init = opts.initial || {};
    const fileName = opts.fileName || sample.name + '.lvs';
    const params = {};
    let seed = Number.isInteger(init.seed) ? init.seed : sample.seed;
    let worker = null, runToken = 0, runTimer = null, pending = false;
    let sessionId = -1;
    let mod = null;
    let visible = new Set();
    let layersKey = '';
    let playTimer = null;
    let debounce = null;
    let last = null;

    host.classList.add('lvs-widget');
    const editorHtml = '<div class="editor"><pre class="gutter" aria-hidden="true"></pre>' +
      '<pre class="hl" aria-hidden="true"><code></code></pre>' +
      '<textarea spellcheck="false" aria-label="LevelScript source"></textarea></div>';
    const diagHtml = '<div class="diag-list" hidden></div>';
    // diagUnderEditor: the diagnostics list goes below the editor instead of below the output
    host.innerHTML =
      (opts.editor === false ? '' :
        opts.diagUnderEditor ? '<div class="edit-col">' + editorHtml + diagHtml + '</div>' : editorHtml) +
      '<div class="output">' +
        '<div class="canvas-wrap"><canvas width="960" height="' + (opts.height || 600) + '"></canvas>' +
        '<div class="status"></div><div class="cell-tip" hidden></div></div>' +
        '<div class="controls">' +
          '<label>seed <input class="seed" type="number" min="0" step="1"></label>' +
          '<button class="dice" title="Random seed"><i class="fas fa-dice"></i></button>' +
          '<span class="params"></span>' +
          '<span class="spacer"></span>' +
          '<button class="run"><i class="fas fa-play"></i> Run</button>' +
          '<button class="step" title="Apply one rule application"><i class="fas fa-forward-step"></i> Step</button>' +
          '<button class="stmt" title="Run to the end of the current statement"><i class="fas fa-angles-right"></i> Statement</button>' +
          '<button class="play" title="Animate the generation"><i class="fas fa-film"></i> Play</button>' +
        '</div>' +
        '<div class="layer-chips"></div>' +
        '<div class="legend"></div>' +
        (opts.diagUnderEditor ? '' : diagHtml) +
        '<pre class="error" hidden></pre>' +
      '</div>';

    const $ = s => host.querySelector(s);
    const canvas = $('canvas'), status = $('.status'), errBox = $('.error');
    const ta = $('textarea'), hl = $('.hl code'), diagBox = $('.diag-list'), gutter = $('.gutter');
    const seedInput = $('.seed');
    seedInput.value = seed;

    // params inputs; the sandbox rebuilds them from the source as it is edited, keeping the
    // values of names that survive and dropping the rest
    const paramBox = $('.params');
    const paramInputs = {};
    const unset = new Set();   // params neither saved nor typed into, so a file's @param applies
    let fileDirs = null;       // the root file's @seed and @param values, from the last inspection
    let paramKey = null;
    function changed() { opts.onChange && opts.onChange({ source: source(), seed, params: Object.assign({}, params) }); }
    function buildParams(list, saved) {
      const key = JSON.stringify(list);
      if (key === paramKey) return;
      paramKey = key;
      const keep = Object.assign({}, saved || params);
      Object.keys(params).forEach(k => delete params[k]);
      Object.keys(paramInputs).forEach(k => delete paramInputs[k]);
      paramBox.innerHTML = '';
      list.forEach(p => {
        const fileValue = fileDirs && fileDirs.params[p.name];
        if (saved) { if (Number.isInteger(saved[p.name])) unset.delete(p.name); else unset.add(p.name); }
        else if (!(p.name in keep)) unset.add(p.name);   // new while editing; survivors keep their state
        params[p.name] = Number.isInteger(keep[p.name]) ? keep[p.name] : fileValue ?? p.default;
        const lab = document.createElement('label');
        lab.innerHTML = esc(p.name) + ' <input type="number" step="1" value="' + params[p.name] + '">';
        paramInputs[p.name] = lab.querySelector('input');
        paramInputs[p.name].addEventListener('input', e => {
          const v = parseInt(e.target.value, 10);
          params[p.name] = Number.isNaN(v) ? p.default : v;
          unset.delete(p.name);
          changed();
          scheduleRun();
        });
        paramBox.appendChild(lab);
      });
    }
    buildParams(sample.params || [], init.params);

    function source() { return ta ? ta.value : sample.source; }
    function showError(msg) { errBox.hidden = !msg; errBox.textContent = msg || ''; }
    function stopPlay() { if (playTimer) { cancelAnimationFrame(playTimer); playTimer = null; } $('.play').innerHTML = '<i class="fas fa-film"></i> Play'; }
    function endSession() { if (sessionId >= 0 && mod) { mod.run_end(sessionId); } sessionId = -1; }

    function render(state) {
      last = state;
      const lvl = state.level;
      const key = lvl.layers.map(l => l.name).join(',');
      if (key !== layersKey) {
        layersKey = key;
        visible = defaultVisible(lvl);
        const chips = $('.layer-chips');
        chips.innerHTML = '';
        if (lvl.layers.length > 1) lvl.layers.forEach(layer => {
          const c = document.createElement('span');
          c.className = 'chip' + (visible.has(layer.name) ? ' on' : '');
          c.textContent = layer.name;
          c.title = 'Toggle layer';
          c.onclick = () => {
            if (visible.has(layer.name)) { if (visible.size > 1) visible.delete(layer.name); }
            else visible.add(layer.name);
            chips.querySelectorAll('.chip').forEach((el, i) => el.classList.toggle('on', visible.has(lvl.layers[i].name)));
            render(last);
          };
          chips.appendChild(c);
        });
      }
      geom = drawState(canvas, state, visible);
      showTip();
      $('.legend').innerHTML = legendFor(lvl, visible)
        .map(n => '<span><i style="background:' + NAMED[n] + '"></i>' + esc(n) +
          (displayNames[n] ? ' <em>' + esc(displayNames[n]) + '</em>' : '') + '</span>').join('');
      status.textContent = (state.done ? 'done' : 'statement ' + Math.min(state.statementIndex + 1, state.statementCount) +
        ' / ' + state.statementCount) + ' · ' + lvl.width + '×' + lvl.height;
    }

    // ---- cell inspector: every layer's value at the cell under the pointer, with @name names ----
    const tip = $('.cell-tip');
    let geom = null, hoverAt = null;
    function cellAt(e) {
      if (!geom) return null;
      const r = canvas.getBoundingClientRect();
      const k = Math.min(r.width / canvas.width, r.height / canvas.height);   // object-fit: contain
      const px = (e.clientX - r.left - (r.width - canvas.width * k) / 2) / k;
      const py = (e.clientY - r.top - (r.height - canvas.height * k) / 2) / k;
      const x = Math.floor((px - geom.ox) / geom.cs), y = Math.floor((py - geom.oy) / geom.cs);
      return x >= 0 && y >= 0 && x < geom.w && y < geom.h ? { x, y } : null;
    }
    function valueText(layer, cell) {
      if (layer.isNumber) return cell === null ? '·' : String(cell);
      if (!cell.length) return '·';
      return cell.map(n => displayNames[n] ? esc(displayNames[n]) + ' <i>' + esc(n) + '</i>' : esc(n)).join(' | ');
    }
    function showTip() {
      const at = hoverAt && last && last.level && geom && hoverAt.x < geom.w && hoverAt.y < geom.h ? hoverAt : null;
      tip.hidden = !at;
      if (!at) return;
      const lvl = last.level, i = at.y * lvl.width + at.x;
      tip.innerHTML = '<b>' + at.x + ', ' + at.y + '</b>' + lvl.layers.map(l =>
        '<div' + (visible.has(l.name) ? '' : ' class="off"') + '><span>' + esc(l.name) + '</span>' +
        valueText(l, l.cells[i]) + '</div>').join('');
    }
    canvas.addEventListener('mousemove', e => {
      hoverAt = cellAt(e);
      showTip();
      const w = canvas.parentNode.getBoundingClientRect(), x = e.clientX - w.left, y = e.clientY - w.top;
      tip.style.left = x < w.width / 2 ? x + 14 + 'px' : '';
      tip.style.right = x < w.width / 2 ? '' : w.width - x + 14 + 'px';
      tip.style.top = y < w.height / 2 ? y + 14 + 'px' : '';
      tip.style.bottom = y < w.height / 2 ? '' : w.height - y + 14 + 'px';
    });
    canvas.addEventListener('mouseleave', () => { hoverAt = null; tip.hidden = true; });

    // ---- inspection: decorations and diagnostics come from one inspect_json call per edit ----
    let insp = null, diag = null;
    // shown: the diagnostics on screen. While typing it only loses entries; new ones appear
    // once the edit has settled for SETTLE_MS, and the program reruns then too.
    let shown = null, view = null, settleTimer = null;
    const SETTLE_MS = 500;
    // From the tool directives (spec Appendix B): value -> its @name, and
    // "sequence#index" -> the statement's @play and @hold.
    let displayNames = {}, plays = {}, holds = {};
    function inspectNow() {
      insp = null;
      if (!mod || !ta) return;
      try { insp = JSON.parse(mod.inspect_json(ta.value, fileName, resolver(fileName))); } catch (e) { insp = null; }
      displayNames = {};
      plays = {};
      holds = {};
      ((insp && insp.directives) || []).forEach(d => {
        if (d.key === 'name') displayNames[d.target.name] = d.value;
        if (d.key === 'play') plays[d.target.owner + '#' + d.target.index] = +d.value;
        if (d.key === 'hold') holds[d.target.owner + '#' + d.target.index] = +d.value;
      });
    }
    // The @play pace at a run state: the innermost statement on its stack with one (a sequence
    // application's covers its body). undefined: none.
    function paceOf(state) {
      let pace;
      (state.stack || []).forEach(f => { const p = plays[f.sequence + '#' + f.index]; if (p !== undefined) pace = p; });
      return pace;
    }
    // The @hold at a statement's begin: the largest of the statements that finished since the last
    // begin. 0: none.
    function holdOf(state) {
      return (state.finished || []).reduce((h, f) => Math.max(h, holds[f.sequence + '#' + f.index] || 0), 0);
    }
    // The file directives @seed and @param (spec Appendix B), as the debugger uses them: on the
    // first inspection @param fills the params that have no saved value, and later an edited
    // @seed or @param applies its new value. A seed or param the user set stays until then.
    function applyFileDirectives() {
      if (!insp) return;
      const now = { seed: undefined, params: {} };
      (insp.directives || []).forEach(d => {
        if (d.target.kind !== 'file' || d.loc.module !== fileName) return;
        if (d.key === 'seed') now.seed = Number(d.value);
        if (d.key === 'param') now.params[d.target.name] = parseInt(d.value, 10);
      });
      const prev = fileDirs;
      fileDirs = now;
      let dirty = false;
      if (prev && now.seed !== undefined && now.seed !== prev.seed) {
        seed = now.seed;
        seedInput.value = seed;
        dirty = true;
      }
      Object.keys(now.params).forEach(n => {
        if (!(n in params) || (prev ? now.params[n] === prev.params[n] : !unset.has(n))) return;
        params[n] = now.params[n];
        paramInputs[n].value = params[n];
        dirty = true;
      });
      if (dirty) changed();
    }
    function hasErrors() { return !!insp && (insp.diagnostics || []).some(d => d.severity === 'error'); }
    // Compile errors are shown by the diagnostics list, so a program that does not compile is not run.
    function blocked() {
      inspectNow();
      if (!hasErrors()) return false;
      stopPlay();
      endSession();
      if (worker) stopWorker();
      showError('');
      status.textContent = 'compile error';
      return true;
    }
    function scrollSync() { hl.parentNode.scrollTop = gutter.scrollTop = ta.scrollTop; hl.parentNode.scrollLeft = ta.scrollLeft; }
    // line numbers: rebuilt when the line count changes; --gw is the digit count, which sizes the gutter
    let lineCount = 0, caretLine = -1;
    function numberLines(src) {
      let n = 1;
      for (let i = src.indexOf('\n'); i >= 0; i = src.indexOf('\n', i + 1)) n++;
      if (n === lineCount) return;
      lineCount = n;
      caretLine = -1;
      ta.parentNode.style.setProperty('--gw', Math.max(2, String(n).length));
      let html = '';
      for (let i = 1; i <= n; i++) html += '<span>' + i + '</span>\n';
      gutter.innerHTML = html;
    }
    function markLine() {
      let line = 0;
      for (let i = ta.value.lastIndexOf('\n', ta.selectionStart - 1); i >= 0; i = ta.value.lastIndexOf('\n', i - 1)) line++;
      if (line === caretLine) return;
      if (gutter.children[caretLine]) gutter.children[caretLine].classList.remove('on');
      caretLine = line;
      if (gutter.children[line]) gutter.children[line].classList.add('on');
    }
    function sync(typing) {
      const src = ta.value;
      inspectNow();
      const lex = model.lexRuns(src, window.LVS_EDITOR);
      const bgs = insp ? model.cellRanges(src, insp, window.LVS_EDITOR, fileName) : [];
      diag = insp ? model.diagnosticRanges(src, insp, fileName) : null;
      view = { src, lex, bgs };
      applyFileDirectives();
      numberLines(src);
      paint(typing);
    }
    function paint(typing) {
      clearTimeout(settleTimer);
      shown = typing && diag && shown ? model.keepDiagnostics(diag, shown) : diag;
      if (typing && shown !== diag) settleTimer = setTimeout(paint, SETTLE_MS);
      hl.innerHTML = model.renderHtml(view.src, view.lex, view.bgs, shown) + '\n';
      renderDiagList();
      scrollSync();
    }
    function renderDiagList() {
      const rows = shown ? shown.rows : [];
      diagBox.hidden = !rows.length;
      diagBox.innerHTML = '';
      rows.forEach(r => {
        const el = document.createElement(r.at === null ? 'div' : 'button');
        el.className = 'diag-row ' + r.severity;
        el.textContent = (r.module ? r.module + ': ' : '') + r.line + ':' + r.col + '  ' + r.message;
        if (r.at !== null) { el.type = 'button'; el.onclick = () => goTo(r.at); }
        diagBox.appendChild(el);
      });
      markActive();
    }
    function goTo(at) {
      ta.focus();
      ta.setSelectionRange(at, at);
      const line = ta.value.slice(0, at).split('\n').length - 1;
      const lh = parseFloat(getComputedStyle(ta).lineHeight) || 20;
      ta.scrollTop = Math.max(0, line * lh - ta.clientHeight / 3);
      scrollSync();
      markActive();
    }
    // there is no hover (the textarea covers the overlay), so the row under the caret is marked
    function markActive() {
      markLine();
      if (!shown) return;
      const p = ta.selectionStart;
      [...diagBox.children].forEach((el, i) => {
        const r = shown.rows[i];
        el.classList.toggle('active', r.from !== null && p >= r.from && p <= r.to);
      });
    }

    function begin() {
      stopPlay();
      endSession();
      showError('');
      if (blocked()) return false;
      const id = mod.run_begin(source(), fileName, seed >>> 0, 'main', resolver(fileName), params);
      if (id < 0) { showError(mod.run_last_error()); status.textContent = 'compile error'; return false; }
      sessionId = id;
      return true;
    }

    function runAll() {
      if (opts.worker && window.Worker) { runInWorker(); return; }
      if (!begin()) return;
      render(JSON.parse(mod.run_finish(sessionId)));
    }

    // The sandbox runs programs in a worker. A program that never reaches its fixpoint
    // (a `grow` or `settle` without a count) is cut off after RUN_LIMIT_MS rather than
    // freezing the page.
    const RUN_LIMIT_MS = 5000;
    function stopWorker() {
      if (worker) { worker.terminate(); worker = null; }
      clearTimeout(runTimer);
      pending = false;
    }
    function runInWorker() {
      stopPlay();
      endSession();
      showError('');
      if (pending) stopWorker();
      if (blocked()) return;
      if (!worker) {
        try { worker = new Worker('js/run-worker.js'); }
        catch (err) { opts.worker = false; runAll(); return; }
      }
      const token = ++runToken;
      pending = true;
      status.textContent = 'running…';
      worker.onmessage = e => {
        if (e.data.token !== token) return;
        clearTimeout(runTimer);
        pending = false;
        if (e.data.error) { showError(e.data.error); status.textContent = 'compile error'; return; }
        render(JSON.parse(e.data.state));
      };
      worker.onerror = () => { stopWorker(); showError('The run worker failed to start.'); status.textContent = 'runtime unavailable'; };
      runTimer = setTimeout(() => {
        stopWorker();
        showError('Stopped: the run took longer than ' + RUN_LIMIT_MS / 1000 + ' s. A grow or settle without a count ' +
          'may never reach its fixpoint.');
        status.textContent = 'stopped';
      }, RUN_LIMIT_MS);
      worker.postMessage({ token, source: source(), name: fileName, seed, params: Object.assign({}, params),
        files: window.LVS_FILES || {} });
    }

    function scheduleRun(ms) { clearTimeout(debounce); debounce = setTimeout(runAll, ms || 250); }

    function startStepping() {
      if (sessionId < 0 || (last && last.done)) { if (!begin()) return false; render(JSON.parse(mod.run_state(sessionId))); }
      return sessionId >= 0;
    }

    $('.run').onclick = runAll;
    $('.step').onclick = () => {
      if (!startStepping()) return;
      stopPlay();
      render(JSON.parse(mod.run_step(sessionId)));
    };
    $('.stmt').onclick = () => {
      if (!startStepping()) return;
      stopPlay();
      render(JSON.parse(mod.run_next_statement(sessionId)));
    };
    $('.play').onclick = () => {
      if (playTimer) { stopPlay(); return; }
      if (!begin()) return;
      render(JSON.parse(mod.run_state(sessionId)));
      $('.play').innerHTML = '<i class="fas fa-pause"></i> Pause';
      play();
    };
    $('.dice').onclick = () => { seed = Math.floor(Math.random() * 1e6); seedInput.value = seed; changed(); runAll(); };
    seedInput.addEventListener('input', () => {
      const v = parseInt(seedInput.value, 10);
      if (!Number.isNaN(v) && v >= 0) { seed = v; changed(); scheduleRun(); }
    });

    // Animate: steps per frame accelerate so long generations still finish in a few seconds.
    // A finished statement's @hold keeps its result for that many frames first.
    function play() {
      const t0 = performance.now();
      let hold = 0;
      const frame = () => {
        if (hold > 0) { hold--; playTimer = requestAnimationFrame(frame); return; }
        // the statement's @play steps per frame (0: all of it), else a pace that speeds up
        const pace = last ? paceOf(last) : undefined;
        const perFrame = pace !== undefined ? (pace || Infinity)
          : 1 + Math.floor(Math.pow(Math.max(0, performance.now() - t0) / 700, 2.2));
        const at = JSON.stringify(last && last.stack), f0 = performance.now();
        let state;
        for (let i = 0; i < perFrame; i++) {
          state = JSON.parse(mod.run_play_step(sessionId));
          if (state.atStatementBegin && (hold = holdOf(state)) > 0) break;
          if (state.done || performance.now() - f0 > 50) break;   // keep the page live
          // a new statement may set its own pace
          if (pace !== undefined && JSON.stringify(state.stack) !== at) break;
        }
        render(state);
        if (state.done) { playTimer = null; stopPlay(); opts.onFinish && opts.onFinish(); return; }
        playTimer = requestAnimationFrame(frame);
      };
      playTimer = requestAnimationFrame(frame);
    }

    if (ta) {
      ta.value = typeof init.source === 'string' ? init.source : sample.source;
      ta.addEventListener('input', () => {
        sync(true);
        if (opts.dynamicParams) buildParams(parseParams(ta.value));
        changed();
        scheduleRun(SETTLE_MS);
      });
      ta.addEventListener('scroll', scrollSync);
      ['keyup', 'click', 'select'].forEach(ev => ta.addEventListener(ev, markActive));
      ta.addEventListener('keydown', e => {
        if (e.key === 'Tab') {
          e.preventDefault();
          const s = ta.selectionStart;
          ta.setRangeText('    ', s, ta.selectionEnd, 'end');
          ta.dispatchEvent(new Event('input'));
        }
      });
      sync();
    }

    const api = {
      host, replay: () => $('.play').click(), restart: runAll,
      // replace the whole program (sandbox): source, and optionally seed and param values
      setSource(src, newSeed, savedParams) {
        ta.value = src;
        fileDirs = null;
        if (Number.isInteger(newSeed)) { seed = newSeed; seedInput.value = seed; }
        buildParams(parseParams(src), savedParams);
        ta.dispatchEvent(new Event('scroll'));
        ta.dispatchEvent(new Event('input'));
      },
      // put text at the caret, without replacing a selection
      insert(text) {
        const at = ta.selectionStart;
        ta.setRangeText(text, at, at, 'end');
        ta.focus();
        ta.dispatchEvent(new Event('input'));
      }
    };
    opts.onApi && opts.onApi(api);
    if (opts.dynamicParams) {
      host.querySelectorAll('.controls button').forEach(b => { b.disabled = true; });
      status.textContent = 'loading…';
    }

    return loadRuntime().then(m => {
      mod = m;
      host.querySelectorAll('.controls button').forEach(b => { b.disabled = false; });
      if (ta) sync();
      if (opts.autoplay) { $('.play').click(); } else { runAll(); }
      return api;
    }).catch(err => {
      showError(String(err.message || err));
      status.textContent = 'runtime unavailable';
    });
  }

  // `params { name: number = 5 }` -> [{name, default}], the same reading build_site_data.py does
  function parseParams(src) {
    const m = /\bparams\s*\{([\s\S]*?)\}/.exec(src);
    if (!m) return [];
    return [...m[1].matchAll(/(\w+)\s*:\s*number\s*=\s*(-?\d+)/g)]
      .map(p => ({ name: p[1], default: parseInt(p[2], 10) }));
  }

  function sampleByName(name) {
    return (window.LVS_SAMPLES || []).find(s => s.name === name);
  }

  // Create widgets only when scrolled into view, so the gallery starts fast.
  function lazy(el, fn) {
    if (!('IntersectionObserver' in window)) { fn(); return; }
    const io = new IntersectionObserver(entries => {
      if (entries.some(e => e.isIntersecting)) { io.disconnect(); fn(); }
    }, { rootMargin: '200px' });
    io.observe(el);
  }

  document.querySelectorAll('[data-demo]').forEach(el => {
    const s = sampleByName(el.dataset.demo);
    if (s) lazy(el, () => createWidget(el, s, { height: 400 }));
  });

  // ---------- hero: the dungeon generating itself, step by step ----------
  const hero = document.getElementById('hero-widget');
  if (hero && sampleByName('dungeon')) {
    let w;
    const loop = () => setTimeout(() => w && w.replay(), 2500);
    createWidget(hero, sampleByName('dungeon'), { editor: false, autoplay: true, height: 600, onFinish: loop })
      .then(x => { w = x; });
  }

  // ---------- samples gallery ----------
  const gallery = document.getElementById('sample-grid');
  if (gallery) {
    (window.LVS_SAMPLES || []).forEach(s => {
      const card = document.createElement('article');
      card.className = 'sample-card';
      card.id = s.name;
      card.innerHTML = '<header><h3>' + esc(s.name) + '.lvs</h3>' +
        '<a class="open-sandbox" href="sandbox.html#sample=' + encodeURIComponent(s.name) + '">' +
        '<i class="fas fa-up-right-from-square"></i> open in sandbox</a></header>' +
        (s.about ? '<p class="about">' + esc(s.about) + '</p>' : '') + '<div class="host"></div>';
      gallery.appendChild(card);
      lazy(card, () => createWidget(card.querySelector('.host'), s, { height: 420 }));
    });
    if (location.hash) {
      const t = document.getElementById(decodeURIComponent(location.hash.slice(1)));
      if (t) t.scrollIntoView();
    }
  }


  // ---------- sandbox: one editor, a draft kept in localStorage, a searchable reference ----------
  const sandboxEl = document.getElementById('sandbox');
  if (sandboxEl) {
    const KEY = 'lvs.sandbox.v1', PREV = KEY + '.prev';
    const DEFAULT_SAMPLE = 'dungeon';
    const note = document.getElementById('sb-note');
    const store = (() => { try { const st = window.localStorage; st.getItem(KEY); return st; } catch (e) { return null; } })();

    function read(key) {
      if (!store) return null;
      try {
        const d = JSON.parse(store.getItem(key));
        return d && typeof d.source === 'string' ? d : null;
      } catch (e) { return null; }
    }
    function write(key, d) {
      if (!store) return false;
      try { store.setItem(key, JSON.stringify(d)); return true; } catch (e) { return false; }
    }
    function say(msg) { note.textContent = msg || ''; }
    if (!store) say('Your draft will not be kept: this browser is blocking local storage.');

    let draft = read(KEY);       // what is stored (and what the editor holds, after a flush)
    let dirty = null;            // an edit not yet written
    let timer = null;
    let widget = null;

    function flush() {
      clearTimeout(timer);
      if (!dirty) return;
      const d = Object.assign({}, dirty, { savedAt: Date.now() });
      dirty = null;
      draft = d;
      if (store && !write(KEY, d)) say('Could not save the draft (storage is full).');
    }
    function onChange(d) {
      dirty = Object.assign({ base: draft && draft.base || null }, d);
      clearTimeout(timer);
      timer = setTimeout(flush, 400);
    }
    window.addEventListener('pagehide', flush);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') flush(); });

    function current() {
      flush();
      return draft;
    }
    // A draft is worth asking about unless it is empty or still exactly a bundled sample.
    function modified(d) {
      if (!d || !d.source.trim()) return false;
      const base = d.base && sampleByName(d.base);
      return !(base && base.source === d.source);
    }
    // The `// @seed=N` directive of a file (spec Appendix B): on its own line before the first
    // declaration. undefined: none.
    function fileSeed(text) {
      for (const line of text.split('\n')) {
        const t = line.trim();
        if (!t) continue;
        if (!t.startsWith('//')) return undefined;
        const m = /^\/\/[ \t]*@.*?(?:^|[ \t])@seed=(\d+)(?=[ \t]|$)/.exec(t) || /^\/\/[ \t]*@seed=(\d+)(?=[ \t]|$)/.exec(t);
        if (m) return +m[1];
      }
      return undefined;
    }
    // Replace the whole program. The old draft is kept one level deep for "restore previous".
    function replaceWith(next, label, confirmFirst) {
      const cur = current();
      if (confirmFirst && modified(cur) && !window.confirm('Replace your current draft with ' + label + '?')) return;
      if (cur && cur.source.trim()) write(PREV, cur);
      draft = { source: next.source, seed: next.seed, params: next.params || {}, base: next.base || null };
      dirty = null;
      write(KEY, Object.assign({ savedAt: Date.now() }, draft));
      widget.setSource(next.source, next.seed, next.params);
      updatePrev();
    }
    function fromSample(s) { return { source: s.source, seed: s.seed, params: {}, base: s.name }; }

    const prevBtn = document.getElementById('sb-prev');
    function updatePrev() { prevBtn.disabled = !read(PREV); }

    // the starting point: a #sample= link, else the stored draft, else the default sample
    const hashed = /^#sample=([\w.-]+)$/.exec(location.hash);
    const linked = hashed && sampleByName(decodeURIComponent(hashed[1]));
    if (hashed) history.replaceState(null, '', location.pathname + location.search);
    let start = draft;
    let asked = false;
    if (linked) {
      if (!modified(draft) || window.confirm('Replace your current draft with ' + linked.name + '.lvs?')) {
        if (draft && modified(draft)) write(PREV, draft);
        start = fromSample(linked);
      }
      asked = true;
    }
    if (!start || !start.source.trim()) start = fromSample(sampleByName(DEFAULT_SAMPLE) || (window.LVS_SAMPLES || [])[0]);
    draft = { source: start.source, seed: start.seed, params: start.params || {}, base: start.base || null };
    if (asked) write(KEY, Object.assign({ savedAt: Date.now() }, draft));

    createWidget(sandboxEl, {
      name: 'sandbox', source: start.source, seed: start.seed, params: parseParams(start.source)
    }, {
      height: Number(sandboxEl.dataset.height) || 420, worker: true, dynamicParams: true, fileName: 'sandbox.lvs',
      diagUnderEditor: 'diagUnderEditor' in sandboxEl.dataset,
      initial: { source: start.source, seed: start.seed, params: start.params },
      onChange, onApi: api => { widget = api; window.LVS_sandbox = api; }
    });

    // toolbar
    const pick = document.getElementById('sb-sample');
    (window.LVS_SAMPLES || []).forEach(s => {
      const o = document.createElement('option');
      o.value = s.name;
      o.textContent = s.name + '.lvs';
      pick.appendChild(o);
    });
    pick.addEventListener('change', () => {
      const s = sampleByName(pick.value);
      pick.value = '';
      if (s) replaceWith(fromSample(s), s.name + '.lvs', true);
    });
    document.getElementById('sb-reset').addEventListener('click', () => {
      const s = sampleByName(DEFAULT_SAMPLE) || (window.LVS_SAMPLES || [])[0];
      if (s) replaceWith(fromSample(s), s.name + '.lvs', false);
    });
    prevBtn.addEventListener('click', () => {
      const prev = read(PREV);
      if (!prev) return;
      const cur = current();
      if (cur && cur.source.trim()) write(PREV, cur); else if (store) store.removeItem(PREV);
      draft = { source: prev.source, seed: prev.seed, params: prev.params || {}, base: prev.base || null };
      dirty = null;
      write(KEY, Object.assign({ savedAt: Date.now() }, draft));
      widget.setSource(prev.source, prev.seed, prev.params);
      updatePrev();
    });
    document.getElementById('sb-download').addEventListener('click', () => {
      const src = (current() || draft).source;
      const url = URL.createObjectURL(new Blob([src], { type: 'text/plain' }));
      const a = document.createElement('a');
      a.href = url;
      a.download = 'sandbox.lvs';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    });
    document.getElementById('sb-open').addEventListener('change', e => {
      const f = e.target.files[0];
      e.target.value = '';
      if (!f) return;
      f.text().then(text => replaceWith({ source: text, seed: fileSeed(text) ?? draft.seed, params: {}, base: null },
        f.name, true));
    });
    updatePrev();

    // reference
    const refRoot = document.getElementById('reference');
    if (refRoot) {
      const entries = window.LVS_API || [];
      const q = document.getElementById('ref-q');
      const chips = document.getElementById('ref-kinds');
      const out = document.getElementById('ref-results');
      let kind = 'all';
      ['all', 'function', 'operation', 'mode', 'keyword'].forEach(k => {
        const c = document.createElement('span');
        c.className = 'chip' + (k === 'all' ? ' on' : '');
        c.textContent = k;
        c.onclick = () => {
          kind = k;
          chips.querySelectorAll('.chip').forEach(el => el.classList.toggle('on', el === c));
          show();
        };
        chips.appendChild(c);
      });
      function row(e) {
        const el = document.createElement('div');
        el.className = 'ref-row';
        el.innerHTML = '<code>' + esc(e.signature) + '</code><span class="ref-sum">' + esc(e.summary) + '</span>' +
          '<a href="spec.html#' + e.anchor + '">spec</a><button type="button">insert</button>';
        el.querySelector('button').onclick = () => widget && widget.insert(e.insert);
        return el;
      }
      function show() {
        out.innerHTML = '';
        const hits = window.LVS_searchApi(entries, q.value, kind);
        if (!hits.length) {
          out.innerHTML = '<p class="ref-none">No results for <em>' + esc(q.value) +
            '</em>. See the <a href="spec.html">full spec</a>.</p>';
          return;
        }
        let group = null;
        hits.forEach(e => {
          if (!q.value.trim() && kind === 'all' && e.kind !== group) {
            group = e.kind;
            const h = document.createElement('h3');
            h.textContent = e.kind + 's';
            out.appendChild(h);
          }
          out.appendChild(row(e));
        });
      }
      q.addEventListener('input', show);
      show();
    }
  }

  // ---------- spec viewer ----------
  const specEl = document.getElementById('spec');
  if (specEl) {
    const tocEl = document.getElementById('toc');
    fetch('spec.md').then(r => { if (!r.ok) throw new Error(r.status); return r.text(); }).then(md => {
      specEl.innerHTML = window.marked.parse(md);
      specEl.querySelectorAll('pre code').forEach(c => {
        if (/language-(ls|lvs)\b/.test(c.className)) c.innerHTML = highlight(c.textContent);
      });
      const used = {};
      specEl.querySelectorAll('h2, h3').forEach(h => {
        let id = h.textContent.toLowerCase().replace(/[^\w]+/g, '-').replace(/^-|-$/g, '') || 'section';
        if (used[id]) id += '-' + (++used[id]); else used[id] = 1;
        h.id = id;
        const a = document.createElement('a');
        a.href = '#' + id;
        a.textContent = h.textContent;
        if (h.tagName === 'H3') a.className = 'sub';
        tocEl.appendChild(a);
      });
      if (location.hash) {
        const t = document.getElementById(decodeURIComponent(location.hash.slice(1)));
        if (t) t.scrollIntoView();
      }
      const links = [...tocEl.querySelectorAll('a')];
      const heads = links.map(a => document.getElementById(a.hash.slice(1)));
      const onScroll = () => {
        let cur = 0;
        heads.forEach((h, i) => { if (h.getBoundingClientRect().top < 120) cur = i; });
        links.forEach((a, i) => a.classList.toggle('active', i === cur));
      };
      document.addEventListener('scroll', onScroll, { passive: true });
      onScroll();
    }).catch(() => {
      specEl.innerHTML = '<p class="note warn"><strong>Could not load the spec.</strong> ' +
        'Serve this folder over HTTP (for example <code>python3 -m http.server</code> in <code>docs/</code>) ' +
        'or read <a href="spec.md">spec.md</a> directly.</p>';
    });
  }
})();
