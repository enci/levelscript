// LevelScript website - highlighting, live generator widget, gallery, lab.
// Programs run in the browser through the same WASM build the VS Code extension uses.

(function () {
  'use strict';

  // ---------- version ----------
  // From src/version.hpp via tools/build_site_data.py. data-ls-version="language" shows
  // MAJOR.MINOR (the spec version); empty shows the full MAJOR.MINOR.PATCH.
  if (window.LVS_VERSION) {
    document.querySelectorAll('[data-ls-version]').forEach(el => {
      el.textContent = el.dataset.lsVersion === 'language'
        ? window.LVS_VERSION.split('.').slice(0, 2).join('.') : window.LVS_VERSION;
    });
  }

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

  // pattern-cell colors for both themes, as CSS variables (see cellVarsCss)
  if (window.LVS_EDITOR) {
    const st = document.createElement('style');
    st.textContent = model.cellVarsCss(window.LVS_EDITOR);
    document.head.appendChild(st);
  }

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function highlight(src) {
    return model.highlightHtml(src, window.LVS_EDITOR);
  }

  document.querySelectorAll('pre code.lvs, pre code.language-ls, pre code.language-lvs').forEach(el => {
    el.innerHTML = highlight(el.textContent);
  });

  // ---------- syntax highlighting for the C++ embedding snippets ----------
  // Just enough C++ for short examples, painted with the same tk-* classes as .lvs.
  const CPP_KW = new Set(('auto bool break case char class const constexpr continue default delete do double else enum ' +
    'false float for if int long namespace new nullptr return short signed sizeof static std struct switch ' +
    'template this true typename unsigned using void while').split(' '));
  const CPP_TOKEN = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')|(\b\d[\w.]*)|([A-Za-z_]\w*)(?=\s*::)|([A-Za-z_]\w*)(?=\s*\()|([A-Za-z_]\w*)|([-+*\/%=<>!&|^~?:]+)/g;
  function highlightCpp(src) {
    let out = '', last = 0, m;
    const span = (cls, s) => '<span class="' + cls + '">' + esc(s) + '</span>';
    CPP_TOKEN.lastIndex = 0;
    while ((m = CPP_TOKEN.exec(src))) {
      out += esc(src.slice(last, m.index));
      last = m.index + m[0].length;
      if (m[1]) out += span('tk-cm', m[0]);
      else if (m[2]) out += span('tk-str', m[0]);
      else if (m[3]) out += span('tk-num', m[0]);
      else if (m[4]) out += span('tk-ty', m[0]);                 // namespace: ls::
      else if (m[5]) out += span(CPP_KW.has(m[0]) ? 'tk-kw' : 'tk-fn', m[0]);   // call (or `if (`)
      else if (m[6]) out += CPP_KW.has(m[0]) ? span('tk-kw', m[0]) : esc(m[0]);
      else out += span('tk-op', m[0]);
    }
    return out + esc(src.slice(last));
  }
  document.querySelectorAll('pre code.language-cpp').forEach(el => {
    el.innerHTML = highlightCpp(el.textContent);
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

  // `use "path"` inside a sample resolves against the bundled sample files (samples/<group>/...).
  function resolver(from) {
    const files = window.LVS_FILES || {};
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

  // Light theme: the dark terrain (walls, rock, empty cells) is lifted so the map does not sit
  // as a dark slab on a light page. Everything else keeps its colour.
  const NAMED_LIGHT = { wall: '#8aa298', rock: '#97a39e' };
  const EMPTY = { dark: '#15231f', light: '#dfe7e2' };
  const lightTheme = () => document.documentElement.dataset.theme === 'light';
  const named = n => (lightTheme() && NAMED_LIGHT[n]) || NAMED[n];

  function strHash(s) {
    let h = 0;
    for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return h;
  }
  // The colours of tag values without one in NAMED, and of numbers. Picked to stay apart from
  // each other and from the greens of the named terrain; only one is green.
  const PALETTE = ['#4e9fff', '#ff8c42', '#c77dff', '#ffd23f', '#ff5c8a', '#3fd0d0',
                   '#a3e048', '#b5835a', '#f25c54', '#8c9eff', '#ff9ff3', '#e8e8e8'];
  const paletteAt = i => PALETTE[((i % PALETTE.length) + PALETTE.length) % PALETTE.length];
  // A value whose tagset is unknown (e.g. the legend before the first inspection)
  function hashColor(s) { return paletteAt(strHash(s)); }

  // Tag-value colours are shared by the map and the editor, and the map leads: the editor tints
  // each value with its map colour (editorTint below).
  // A map colour as an editor background. Hue and saturation are kept; lightness is
  // pulled into a band per theme, so near-black walls still show on the dark editor, white
  // pieces still show on the light one, and the text on top stays readable.
  const TINT_BAND = { dark: [26, 36], light: [76, 86] };
  function toHsl(color) {
    let m = /^hsl\((\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)%\s+(\d+(?:\.\d+)?)%\)$/.exec(color);
    if (m) return [+m[1], +m[2], +m[3]];
    m = /^rgb\((\d+),\s*(\d+),\s*(\d+)\)$/.exec(color) || /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color);
    if (!m) return null;
    const [r, g, b] = (color[0] === '#' ? m.slice(1).map(x => parseInt(x, 16)) : m.slice(1).map(Number)).map(x => x / 255);
    const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2, d = max - min;
    if (!d) return [0, 0, l * 100];
    const sat = d / (1 - Math.abs(2 * l - 1));
    const h = max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
    return [(h * 60 + 360) % 360, sat * 100, l * 100];
  }
  function editorTint(color) {
    const hsl = toHsl(color);
    if (!hsl) return color;
    const [lo, hi] = TINT_BAND[lightTheme() ? 'light' : 'dark'];
    const l = lo + (hi - lo) * (hsl[2] / 100);          // keep the order of light and dark values
    const sat = Math.min(hsl[1], lightTheme() ? 75 : 45); // vivid colours are toned down behind text
    return 'hsl(' + hsl[0].toFixed(0) + ' ' + sat.toFixed(0) + '% ' + l.toFixed(1) + '%)';
  }
  // A colour per number, as a tag value has one: consecutive ids never share a colour.
  function numberColor(n) { return paletteAt(n); }
  // @view=ramp: viridis, from the lowest value to the highest
  const RAMP = [[68, 1, 84], [59, 82, 139], [33, 145, 140], [94, 201, 98], [253, 231, 37]];
  function rampColor(t) {
    const f = Math.max(0, Math.min(1, t)) * (RAMP.length - 1);
    const i = Math.min(RAMP.length - 2, Math.floor(f)), k = f - i;
    return 'rgb(' + RAMP[i].map((v, j) => Math.round(v + (RAMP[i + 1][j] - v) * k)).join(',') + ')';
  }

  function isEmpty(layer, cell) {
    return layer.isNumber ? cell === null : cell.length === 0;
  }

  // The views of spec B.8 a layer can be switched to here. `tiles` is drawn as fill: the site
  // has no tilesets yet.
  const VIEWS = ['fill', 'shape', 'corner', 'text', 'border', 'ramp', 'hidden'];
  const TEXT_MIN_CELL = 12;   // px: below this a text view is left out (spec B.8)
  const SHAPES = ['circle', 'square', 'diamond', 'triangle', 'ring', 'cross'];

  // A marker of radius r at (cx, cy), filled with the current fillStyle.
  function drawShape(g, shape, cx, cy, r) {
    g.beginPath();
    if (shape === 'square') g.rect(cx - r * 0.85, cy - r * 0.85, r * 1.7, r * 1.7);
    else if (shape === 'diamond') { g.moveTo(cx, cy - r); g.lineTo(cx + r, cy); g.lineTo(cx, cy + r); g.lineTo(cx - r, cy); }
    else if (shape === 'triangle') { g.moveTo(cx, cy - r); g.lineTo(cx + r, cy + r * 0.8); g.lineTo(cx - r, cy + r * 0.8); }
    else if (shape === 'cross') {
      const t = r * 0.38;
      [[-t, -r], [t, -r], [t, -t], [r, -t], [r, t], [t, t], [t, r], [-t, r], [-t, t], [-r, t], [-r, -t], [-t, -t]]
        .forEach(([dx, dy], i) => (i ? g.lineTo : g.moveTo).call(g, cx + dx, cy + dy));
    } else g.arc(cx, cy, shape === 'ring' ? r * 0.75 : r, 0, Math.PI * 2);
    g.closePath();
    if (shape === 'ring') {
      const fill = g.fillStyle;
      g.lineWidth = Math.max(1.5, r * 0.45);
      g.strokeStyle = 'rgba(0,0,0,0.55)';
      g.stroke();
      g.lineWidth = Math.max(1, r * 0.3);
      g.strokeStyle = fill;
      g.stroke();
      return;
    }
    g.fill();
    g.lineWidth = 1;
    g.strokeStyle = 'rgba(0,0,0,0.55)';
    g.stroke();
  }

  // state: parsed run_* JSON. look: { view(layer, i), range(layer), shape(layer, cell) }.
  // Returns where the cells were drawn.
  function drawState(canvas, state, look) {
    const lvl = state.level;
    const w = lvl.width, h = lvl.height;
    const g = canvas.getContext('2d');
    g.clearRect(0, 0, canvas.width, canvas.height);
    if (!w || !h) return null;
    const cs = Math.max(2, Math.floor(Math.min(canvas.width / w, canvas.height / h)));
    const ox = Math.floor((canvas.width - w * cs) / 2);
    const oy = Math.floor((canvas.height - h * cs) / 2);
    g.fillStyle = lightTheme() ? EMPTY.light : EMPTY.dark;
    g.fillRect(ox, oy, w * cs, h * cs);
    // squares first, then the marks, then the border lines (spec B.8); each in declaration order
    const RANK = { fill: 0, ramp: 0, corner: 1, shape: 1, text: 1, border: 2 };
    const order = lvl.layers.map((layer, li) => ({ layer, view: look.view(layer, li), li }))
      .filter(e => e.view !== 'hidden')
      .sort((a, b) => RANK[a.view] - RANK[b.view] || a.li - b.li);
    let corners = 0;
    order.forEach(({ layer, view }) => {
      if (view === 'border') {
        const key = c => layer.isNumber ? String(c) : c.join('|');
        g.strokeStyle = lightTheme() ? 'rgba(10,30,22,0.85)' : 'rgba(245,250,240,0.9)';
        g.lineWidth = Math.max(1, Math.round(cs / 6));
        g.beginPath();
        for (let y = 0; y < h; y++) {
          for (let x = 0; x < w; x++) {
            const k = key(layer.cells[y * w + x]);
            if (x + 1 < w && key(layer.cells[y * w + x + 1]) !== k) {
              g.moveTo(ox + (x + 1) * cs, oy + y * cs); g.lineTo(ox + (x + 1) * cs, oy + (y + 1) * cs);
            }
            if (y + 1 < h && key(layer.cells[(y + 1) * w + x]) !== k) {
              g.moveTo(ox + x * cs, oy + (y + 1) * cs); g.lineTo(ox + (x + 1) * cs, oy + (y + 1) * cs);
            }
          }
        }
        g.stroke();
        return;
      }
      if (view === 'text') {
        if (cs < TEXT_MIN_CELL) return;
        // as large as fits: up to 3/4 of the cell, smaller for longer text down to MIN_PX, and
        // cut to what fits at MIN_PX. A monospace character is about 0.6 of the font size wide.
        const MIN_PX = 9, room = cs * 0.92;
        const maxChars = Math.max(1, Math.floor(room / (MIN_PX * 0.6)));
        g.textAlign = 'center';
        g.textBaseline = 'middle';
        g.lineJoin = 'round';
        for (let y = 0; y < h; y++) {
          for (let x = 0; x < w; x++) {
            const cell = layer.cells[y * w + x];
            if (isEmpty(layer, cell)) continue;
            const t = (layer.isNumber ? String(cell) : cell[0]).slice(0, maxChars);
            const px = Math.max(MIN_PX, Math.floor(Math.min(cs * 0.75, room / (t.length * 0.6))));
            const cx = ox + x * cs + cs / 2, cy = oy + y * cs + cs / 2 + 0.5;
            // the value's colour, outlined in black, or in white for the dark colours
            const color = look.color(layer, cell), hsl = toHsl(color);
            g.font = '700 ' + px + 'px ui-monospace, monospace';
            g.lineWidth = Math.max(2, px / 5);
            g.strokeStyle = hsl && hsl[2] < 40 ? 'rgba(255,255,255,0.85)' : 'rgba(0,0,0,0.8)';
            g.strokeText(t, cx, cy);
            g.fillStyle = color;
            g.fillText(t, cx, cy);
          }
        }
        return;
      }
      const range = view === 'ramp' ? look.range(layer) : null;
      const slot = view === 'corner' ? corners++ % 4 : 0;   // TL, TR, BR, BL
      const r = Math.max(1.5, cs * 0.36), leg = cs * 0.5;
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const cell = layer.cells[y * w + x];
          if (isEmpty(layer, cell)) continue;
          const x0 = ox + x * cs, y0 = oy + y * cs;
          if (view === 'ramp') {
            g.fillStyle = rampColor(range.hi > range.lo ? (cell - range.lo) / (range.hi - range.lo) : 0.5);
            g.fillRect(x0, y0, cs, cs);
            continue;
          }
          g.fillStyle = look.color(layer, cell);
          if (view === 'shape') drawShape(g, look.shape(layer, cell), x0 + cs / 2, y0 + cs / 2, r);
          else if (view === 'corner') {
            const cx = slot === 1 || slot === 2 ? x0 + cs : x0, cy = slot >= 2 ? y0 + cs : y0;
            g.beginPath();
            g.moveTo(cx, cy);
            g.lineTo(cx + (cx === x0 ? leg : -leg), cy);
            g.lineTo(cx, cy + (cy === y0 ? leg : -leg));
            g.closePath();
            g.fill();
          } else g.fillRect(x0, y0, cs, cs);   // fill, and tiles until the site has tilesets
        }
      }
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
    // in colours no tag value uses, over a dark outline so they show on any cell
    (state.done ? [] : state.highlights || []).forEach(hl => {
      const layer = lvl.layers[hl.layer];
      if (!layer || look.view(layer, hl.layer) === 'hidden') return;
      const x0 = ox + hl.x * cs + 1, y0 = oy + hl.y * cs + 1;
      g.lineWidth = 4;
      g.strokeStyle = 'rgba(0,0,0,0.7)';
      g.strokeRect(x0, y0, cs - 2, cs - 2);
      g.lineWidth = 2;
      g.strokeStyle = hl.what === 'write' ? '#ff3df2' : '#ffffff';
      g.strokeRect(x0, y0, cs - 2, cs - 2);
    });
    return { cs, ox, oy, w, h };
  }

  // every value on a drawn tag layer, hand-picked and palette colours alike: [name, colour]
  function legendFor(lvl, look) {
    const seen = new Map();
    lvl.layers.forEach((l, i) => {
      const view = look.view(l, i);
      if (l.isNumber || view === 'hidden' || view === 'border') return;
      l.cells.forEach(c => c.forEach(n => { if (!seen.has(n)) seen.set(n, look.color(l, [n])); }));
    });
    return [...seen];
  }

  // ---------- the widget ----------
  // A runnable generator: editable source, seed + params, Run, and stepping through
  // rule applications. `opts.editor` false hides the source (used by the hero).
  function createWidget(host, sample, opts) {
    opts = opts || {};
    const init = opts.initial || {};
    const fileName = opts.fileName || sample.file || sample.name + '.lvs';
    const params = {};
    let seed = Number.isInteger(init.seed) ? init.seed : sample.seed;
    let worker = null, runToken = 0, runTimer = null, pending = false;
    let sessionId = -1;
    let mod = null;
    // Layer views (spec B.8): the @view directives, and what the user switched this session.
    // A user's choice lasts until the layer's directive changes.
    let dirViews = {}, tilesets = {}, userViews = {};
    let rampRange = {};    // number layer -> { lo, hi } seen so far in this run
    let valueIndex = {};   // layer -> { value: position in its tagset }, for the marker shapes
    let layerTag = {};     // layer -> its tagset
    let slots = {};        // tagset -> { value: palette slot }, for values without a NAMED colour
    let colorKey = {};     // tagset -> { value: its @name, if it has one }
    let chipsKey = '';
    let playTimer = null;
    let debounce = null;
    let last = null;

    host.classList.add('lvs-widget');
    const editorHtml = '<div class="editor"></div>';   // CodeMirror (js/cm-editor.js, built from web/)
    const diagHtml = '<div class="diag-list" hidden></div>';
    // diagUnderEditor: the diagnostics list goes below the editor instead of below the output
    host.innerHTML =
      (opts.editor === false ? '' :
        opts.diagUnderEditor ? '<div class="edit-col">' + editorHtml + diagHtml + '</div>' : editorHtml) +
      '<div class="output">' +
        '<div class="canvas-wrap"><canvas width="960" height="' + (opts.height || 600) + '"></canvas>' +
        '<div class="status"></div><div class="cell-tip" hidden></div></div>' +
        '<div class="controls">' +
          '<button class="run"><i class="ph ph-play"></i> Run</button>' +
          '<button class="step" title="Apply one rule application"><i class="ph ph-skip-forward"></i> Step</button>' +
          '<button class="stmt" title="Run to the end of the current statement"><i class="ph ph-caret-double-right"></i> Statement</button>' +
          '<button class="play" title="Animate the generation"><i class="ph ph-film-strip"></i> Play</button>' +
          '<span class="spacer"></span>' +
          '<span class="seed-group"><label>seed <input class="seed" type="number" min="0" step="1"></label>' +
          '<button class="dice" title="Random seed"><i class="ph ph-dice-five"></i></button></span>' +
          '<span class="params"></span>' +
        '</div>' +
        '<div class="layer-chips"></div>' +
        '<div class="legend"></div>' +
        (opts.diagUnderEditor ? '' : diagHtml) +
        '<pre class="error" hidden></pre>' +
      '</div>';

    const $ = s => host.querySelector(s);
    const canvas = $('canvas'), status = $('.status'), errBox = $('.error');
    // the chip on the canvas; errors (compile error, stopped, ...) are red like the error messages
    function setStatus(text, isError) {
      status.textContent = text;
      status.classList.toggle('bad', !!isError);
    }
    // a run's time, like the native debugger's status bar. Browsers coarsen the clock (Safari and
    // Firefox to 1 ms), so whole milliseconds; a run under the clock's grain reads "< 1 ms".
    function fmtMs(ms) { return ms < 1 ? '< 1 ms' : Math.round(ms) + ' ms'; }
    const diagBox = $('.diag-list');
    if (opts.editor !== false && !window.LVS_CM) {
      // a local checkout without the bundle: say how to build it instead of failing silently
      $('.editor').innerHTML = '<pre class="error">The editor bundle is missing (docs/js/cm-editor.js).\n' +
        'Build it with:  npm --prefix web ci && npm --prefix web run build</pre>';
      opts.editor = false;
    }
    const ed = opts.editor === false ? null : window.LVS_CM.create({
      parent: $('.editor'),
      doc: typeof init.source === 'string' ? init.source : sample.source,
      fileName,
      onChange: () => {
        sync(true);
        if (opts.dynamicParams) buildParams(parseParams(ed.value()));
        changed();
        scheduleRun(SETTLE_MS);
      },
      onCursor: () => markActive(),
    });
    const seedInput = $('.seed');
    seedInput.value = seed;

    // params inputs; the lab rebuilds them from the source as it is edited, keeping the
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

    function source() { return ed ? ed.value() : sample.source; }
    function showError(msg) { errBox.hidden = !msg; errBox.textContent = msg || ''; }
    function stopPlay() { if (playTimer) { cancelAnimationFrame(playTimer); playTimer = null; } $('.play').innerHTML = '<i class="ph ph-film-strip"></i> Play'; }
    function endSession() { if (sessionId >= 0 && mod) { mod.run_end(sessionId); } sessionId = -1; }

    document.addEventListener('ls-themechange', () => {
      if (ed) sync();   // the editor tints follow the theme
      if (last) render(last);
    });

    // A layer's view: the user's switch, else its @view, else fill for the first layer and
    // shape for the rest. Known before the run starts, so it never changes while playing.
    function baseView(layer, i) {
      let v = dirViews[layer.name] || (i === 0 ? 'fill' : 'shape');
      if (v === 'ramp' && !layer.isNumber) v = 'fill';
      return v === 'tiles' ? 'fill' : v;
    }
    // A tag value's colour comes from its @name, else its own name: the NAMED colour of that
    // name, else the palette slot its hash picks, moved on to the next free one if an earlier
    // value of the tagset has it. So a name looks the same in every file, and two values of
    // one tagset never share a colour.
    function tagColor(tagset, name) {
      const key = (colorKey[tagset] || {})[name] || name;
      const slot = (slots[tagset] || {})[name];
      return named(key) || (slot !== undefined ? paletteAt(slot) : hashColor(key));
    }
    const look = {
      view: (layer, i) => userViews[layer.name] || baseView(layer, i),
      color: (layer, cell) => layer.isNumber ? numberColor(cell) : tagColor(layerTag[layer.name], cell[0]),
      range: layer => rampRange[layer.name] || { lo: 0, hi: 0 },
      shape(layer, cell) {
        const at = layer.isNumber ? cell : (valueIndex[layer.name] || {})[cell[0]] ?? strHash(cell[0]);
        return SHAPES[((at % SHAPES.length) + SHAPES.length) % SHAPES.length];
      }
    };
    // number layers: the ramp range only widens during a run (spec B.8)
    function widenRanges(lvl) {
      lvl.layers.forEach(l => {
        if (!l.isNumber) return;
        const r = rampRange[l.name] || (rampRange[l.name] = { lo: Infinity, hi: -Infinity });
        l.cells.forEach(c => { if (c !== null) { if (c < r.lo) r.lo = c; if (c > r.hi) r.hi = c; } });
      });
    }
    // one chip per layer: its name shows or hides it, its menu switches the view
    function buildChips(lvl) {
      const chips = $('.layer-chips');
      chips.innerHTML = '';
      lvl.layers.forEach((layer, i) => {
        const view = look.view(layer, i);
        const c = document.createElement('span');
        c.className = 'chip' + (view === 'hidden' ? '' : ' on');
        if (view === 'text' && geom && geom.cs < TEXT_MIN_CELL)
          c.title = 'The cells are too small for text here; hover a cell to read its values';
        else if (dirViews[layer.name] === 'tiles')
          c.title = 'tileset ' + (tilesets[layer.name] || '?') + ': the site has no tilesets yet, so the layer is drawn as fill';
        const name = document.createElement('span');
        name.textContent = layer.name;
        name.title = 'Show or hide the layer';
        name.onclick = () => {
          const base = baseView(layer, i);
          userViews[layer.name] = view !== 'hidden' ? 'hidden' : base !== 'hidden' ? base : i === 0 ? 'fill' : 'shape';
          render(last);
        };
        const menu = document.createElement('select');
        menu.className = 'chip-view';
        menu.title = 'How the layer is drawn';
        menu.setAttribute('aria-label', 'View of ' + layer.name);
        VIEWS.filter(v => v !== 'ramp' || layer.isNumber).forEach(v => menu.add(new Option(v, v)));
        menu.value = view;
        menu.onchange = () => { userViews[layer.name] = menu.value; render(last); };
        c.append(name, menu);
        chips.appendChild(c);
      });
    }

    // ms: how long a whole run took (Run, or an edit), shown once it is done
    function render(state, ms) {
      last = state;
      const lvl = state.level;
      widenRanges(lvl);
      const key = lvl.layers.map((l, i) => l.name + ':' + l.isNumber + ':' + look.view(l, i)).join(',') +
        ':' + (geom && geom.cs < TEXT_MIN_CELL);
      if (key !== chipsKey) { chipsKey = key; buildChips(lvl); }
      geom = drawState(canvas, state, look);
      showTip();
      $('.legend').innerHTML = legendFor(lvl, look)
        .map(([n, color]) => '<span><i style="background:' + color + '"></i>' + esc(n) +
          (displayNames[n] ? ' <em>' + esc(displayNames[n]) + '</em>' : '') + '</span>').join('');
      setStatus((state.done ? 'done' : 'statement ' + Math.min(state.statementIndex + 1, state.statementCount) +
        ' / ' + state.statementCount) + ' · ' + lvl.width + '×' + lvl.height +
        (state.done && ms !== undefined ? ' · ' + fmtMs(ms) : ''));
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
        '<div' + (look.view(l, lvl.layers.indexOf(l)) === 'hidden' ? ' class="off"' : '') + '><span>' + esc(l.name) + '</span>' +
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
    // The playback clock that @hold counts in (spec B.6): ticks per second.
    const PLAY_FPS = 30;
    // From the tool directives (spec Appendix B): value -> its @name, and
    // "sequence#index" -> the statement's @play and @hold.
    let displayNames = {}, plays = {}, holds = {};
    // the editor's tag-value backgrounds come from the map
    function editorColor(info) {
      if (info.number !== undefined) return editorTint(numberColor(info.number));
      return editorTint(tagColor(info.tag, info.name));
    }
    function inspectNow() {
      insp = null;
      if (!mod) return;
      try { insp = JSON.parse(mod.inspect_json(source(), fileName, resolver(fileName))); } catch (e) { insp = null; }
      displayNames = {};
      plays = {};
      holds = {};
      const prevViews = dirViews;
      colorKey = {};
      dirViews = {};
      tilesets = {};
      ((insp && insp.directives) || []).forEach(d => {
        if (d.key === 'name') {
          displayNames[d.target.name] = d.value;
          (colorKey[d.target.owner] || (colorKey[d.target.owner] = {}))[d.target.name] = d.value;
        }
        if (d.key === 'play') plays[d.target.owner + '#' + d.target.index] = +d.value;
        if (d.key === 'hold') holds[d.target.owner + '#' + d.target.index] = +d.value;
        if (d.key === 'view') dirViews[d.target.name] = d.value;
        if (d.key === 'tileset') tilesets[d.target.name] = d.value;
      });
      Object.keys(userViews).forEach(n => { if (dirViews[n] !== prevViews[n]) delete userViews[n]; });
      valueIndex = {};
      layerTag = {};
      slots = {};
      if (insp && insp.symbols) {
        const tags = {};
        (insp.symbols.tags || []).forEach(t => {
          tags[t.name] = t.values.map(v => v.name);
          const taken = new Set();
          slots[t.name] = {};
          t.values.forEach(v => {
            const key = (colorKey[t.name] || {})[v.name] || v.name;
            if (NAMED[key]) return;
            let slot = strHash(key) % PALETTE.length;
            for (let i = 0; i < PALETTE.length && taken.has(slot); i++) slot = (slot + 1) % PALETTE.length;
            taken.add(slot);
            slots[t.name][v.name] = slot;
          });
        });
        (insp.symbols.layers || []).forEach(l => {
          if (!tags[l.type]) return;
          layerTag[l.name] = l.type;
          valueIndex[l.name] = {};
          tags[l.type].forEach((v, i) => { valueIndex[l.name][v] = i; });
        });
      }
    }
    // The @play pace at a run state: the innermost statement on its stack with one (a sequence
    // application's covers its body). undefined: none; negative: -N, one step every N ticks.
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
      setStatus('compile error', true);
      return true;
    }
    function sync(typing) {
      const src = ed.value();
      inspectNow();
      const bgs = insp ? model.cellRanges(src, insp, window.LVS_EDITOR, fileName, editorColor) : [];
      diag = insp ? model.diagnosticRanges(src, insp, fileName) : null;
      view = { bgs };
      applyFileDirectives();
      paint(typing);
    }
    function paint(typing) {
      clearTimeout(settleTimer);
      shown = typing && diag && shown ? model.keepDiagnostics(diag, shown) : diag;
      if (typing && shown !== diag) settleTimer = setTimeout(paint, SETTLE_MS);
      // underlines and hover messages: the diagnostics in this file
      const lint = (shown ? shown.rows : []).filter(r => r.at !== null && r.from !== null)
        .map(r => ({ from: r.from, to: r.to, severity: r.severity === 'error' ? 'error' : 'warning', message: r.message }));
      ed.paint(insp, view.bgs, lint);
      renderDiagList();
    }
    function renderDiagList() {
      const rows = shown ? shown.rows : [];
      diagBox.hidden = !rows.length;
      diagBox.innerHTML = '';
      rows.forEach(r => {
        const el = document.createElement(r.at === null ? 'div' : 'button');
        el.className = 'diag-row ' + r.severity;
        el.textContent = (r.module ? r.module + ': ' : '') + r.line + ':' + r.col + '  ' + r.message;
        if (r.at !== null) { el.type = 'button'; el.onclick = () => { ed.goTo(r.at); markActive(); }; }
        diagBox.appendChild(el);
      });
      markActive();
    }
    // the list row of the diagnostic under the caret is marked
    function markActive() {
      if (!shown || !ed) return;
      const p = ed.cursor();
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
      rampRange = {};
      const id = mod.run_begin(source(), fileName, seed >>> 0, 'main', resolver(fileName), params);
      if (id < 0) { showError(mod.run_last_error()); setStatus('compile error', true); return false; }
      sessionId = id;
      return true;
    }

    function runAll() {
      if (opts.worker && window.Worker) { runInWorker(); return; }
      if (!begin()) return;
      const t0 = performance.now();
      const out = mod.run_finish(sessionId);
      render(JSON.parse(out), performance.now() - t0);
    }

    // The lab runs programs in a worker. A program that never reaches its fixpoint
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
      setStatus('running…');
      worker.onmessage = e => {
        if (e.data.token !== token) return;
        clearTimeout(runTimer);
        pending = false;
        if (e.data.error) { showError(e.data.error); setStatus('compile error', true); return; }
        rampRange = {};
        render(JSON.parse(e.data.state), e.data.ms);
      };
      worker.onerror = () => { stopWorker(); showError('The run worker failed to start.'); setStatus('runtime unavailable', true); };
      runTimer = setTimeout(() => {
        stopWorker();
        showError('Stopped: the run took longer than ' + RUN_LIMIT_MS / 1000 + ' s. A grow or settle without a count ' +
          'may never reach its fixpoint.');
        setStatus('stopped', true);
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
    function startPlay() {
      clearTimeout(debounce);
      if (!begin()) return;
      render(JSON.parse(mod.run_state(sessionId)));
      $('.play').innerHTML = '<i class="ph ph-pause"></i> Pause';
      play();
    }
    $('.play').onclick = () => { if (playTimer) stopPlay(); else startPlay(); };
    $('.dice').onclick = () => { seed = Math.floor(Math.random() * 1e6); seedInput.value = seed; changed(); runAll(); };
    seedInput.addEventListener('input', () => {
      const v = parseInt(seedInput.value, 10);
      if (!Number.isNaN(v) && v >= 0) { seed = v; changed(); scheduleRun(); }
    });

    // Animate: steps per frame accelerate so long generations still finish in a few seconds.
    // A finished statement's @hold keeps its result for that many ticks of the playback clock
    // first (spec B.6), PLAY_FPS ticks a second; the hold does not count toward the speed-up.
    // @play=-N waits N ticks before each step (spec B.5); neither does that wait.
    function play() {
      let t0 = performance.now(), holdUntil = 0;
      const frame = () => {
        if (performance.now() < holdUntil) { playTimer = requestAnimationFrame(frame); return; }
        let hold = 0;
        // the statement's @play steps per frame (0: all of it; -N: one), else a pace that speeds up
        const pace = last ? paceOf(last) : undefined;
        const perFrame = pace !== undefined ? (pace < 0 ? 1 : pace || Infinity)
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
        // the wait after this step: its statement's hold, then a slow @play's ticks (a begin
        // shows nothing new, so it does not wait)
        const slow = state.atStatementBegin ? undefined : paceOf(state);
        const ticks = hold + (slow < 0 ? -slow : 0);
        if (ticks > 0) { const ms = ticks * 1000 / PLAY_FPS; holdUntil = performance.now() + ms; t0 += ms; }
        playTimer = requestAnimationFrame(frame);
      };
      playTimer = requestAnimationFrame(frame);
    }

    if (ed) sync();

    const api = {
      host, replay: () => $('.play').click(), restart: runAll, source,
      // replace the whole program (lab): source, and optionally seed and param values
      // fresh: another document (a different script), so undo cannot reach back into the old one
      setSource(src, newSeed, savedParams, fresh) {
        fileDirs = null;
        if (Number.isInteger(newSeed)) { seed = newSeed; seedInput.value = seed; }
        buildParams(parseParams(src), savedParams);
        if (!fresh) { ed.setValue(src); return; }   // one undoable edit; onChange inspects and reruns
        ed.reset(src);
        shown = null;
        sync();
        if (opts.autoplay && mod) startPlay(); else scheduleRun(0);
      },
      // put text at the caret, without replacing a selection
      insert(text) { ed.insert(text); }
    };
    opts.onApi && opts.onApi(api);
    if (opts.dynamicParams) {
      host.querySelectorAll('.controls button').forEach(b => { b.disabled = true; });
      setStatus('loading…');
    }

    return loadRuntime().then(m => {
      mod = m;
      host.querySelectorAll('.controls button').forEach(b => { b.disabled = false; });
      if (ed) sync();
      if (opts.autoplay) startPlay(); else runAll();
      return api;
    }).catch(err => {
      showError(String(err.message || err));
      setStatus('runtime unavailable', true);
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
    if (s) lazy(el, () => createWidget(el, s, { height: 400, autoplay: true }));
  });

  // ---------- hero: the dungeon generating itself, step by step ----------
  const HERO = 'roguelike';
  const hero = document.getElementById('hero-widget');
  if (hero && sampleByName(HERO)) {
    let w;
    const loop = () => setTimeout(() => w && w.replay(), 2500);
    createWidget(hero, sampleByName(HERO), { editor: false, autoplay: true, height: 600, onFinish: loop })
      .then(x => { w = x; });
  }

  // ---------- samples gallery ----------
  // one section per samples/ folder, in the order build_site_data.py emits them
  const GROUP_TITLES = { tutorial: 'Tutorial', techniques: 'Techniques', games: 'Games' };
  const gallery = document.getElementById('sample-grid');
  if (gallery) {
    let group = null;
    (window.LVS_SAMPLES || []).forEach(s => {
      if (s.group && s.group !== group) {
        group = s.group;
        const h = document.createElement('h2');
        h.className = 'sample-group';
        h.id = group;
        h.textContent = GROUP_TITLES[group] || group;
        gallery.appendChild(h);
      }
      const card = document.createElement('article');
      card.className = 'sample-card';
      card.id = s.name;
      card.innerHTML = '<header><h3>' + esc(s.name) + '.lvs</h3>' +
        '<a class="open-lab lab-badge" title="Open this sample as a new script in the lab" href="lab.html#sample=' + encodeURIComponent(s.name) + '">' +
        '<i class="ph ph-flask"></i> open in lab</a></header>' +
        (s.about ? '<p class="about">' + esc(s.about) + '</p>' : '') + '<div class="host"></div>';
      gallery.appendChild(card);
      lazy(card, () => createWidget(card.querySelector('.host'), s, { height: 560, autoplay: true }));
    });
    if (location.hash) {
      const t = document.getElementById(decodeURIComponent(location.hash.slice(1)));
      if (t) t.scrollIntoView();
    }
  }


  // ---------- lab: one editor, a draft kept in localStorage, a searchable reference ----------
  // lab.html (under the site nav) and standalone_lab.html (for the paper) hold only their own
  // branding in <header class="lab-head"> and an empty <main id="lab">. Everything else is built
  // here, once, so the two pages cannot drift apart. In the header: the badge, then the saved
  // flash (its margin-right: auto pushes the toolbar to the right), then the toolbar.
  const labRoot = document.getElementById('lab');
  const labHead = document.querySelector('.lab-head');
  if (labRoot && labHead) {
    labHead.insertAdjacentHTML('beforeend',
      '<span class="lab-badge" title="The LevelScript lab: write, run and keep scripts in your browser"><i class="ph ph-flask"></i> lab</span>' +
      '<span id="lab-saved" class="lab-saved" role="status" title="Saved in this browser"><i class="ph ph-floppy-disk" aria-hidden="true"></i><span class="sr-only"></span></span>' +
      '<div class="lab-bar">' +
        '<div class="lab-group" role="group" aria-label="Your script">' +
          '<span class="lab-label"><i class="ph ph-file-code"></i> script</span>' +
          '<button type="button" id="lab-new" title="Start a new script"><i class="ph ph-plus"></i> New</button>' +
          '<button type="button" id="lab-rename" title="Rename this script"><i class="ph ph-pencil-simple"></i> Rename</button>' +
          '<button type="button" id="lab-download" title="Save this script as a .lvs file (Ctrl/Cmd+Shift+S)"><i class="ph ph-download-simple"></i> Download</button>' +
          '<button type="button" id="lab-delete" class="danger" title="Delete this script"><i class="ph ph-trash"></i> Delete</button>' +
        '</div>' +
        '<div class="lab-group" role="group" aria-label="Open as a new script">' +
          '<span class="lab-label">open as new script</span>' +
          '<select id="lab-sample" aria-label="Open a sample as a new script"><option value="">sample&hellip;</option></select>' +
          '<label class="file-btn" title="Open a .lvs file from your computer as a new script"><i class="ph ph-folder-open"></i> File&hellip;<input type="file" id="lab-open" accept=".lvs,text/plain" hidden></label>' +
        '</div>' +
        '<span id="lab-note" role="status"></span>' +
      '</div>');
    labRoot.innerHTML =
      '<div class="lab-host"></div>' +
      '<section id="reference">' +
        '<h2>Reference</h2>' +
        '<input type="search" id="ref-q" placeholder="search: random, path, grow, ..." aria-label="Search the reference" autocomplete="off">' +
        '<div class="layer-chips" id="ref-kinds"></div>' +
        '<div id="ref-results"></div>' +
      '</section>';
    const labEl = labRoot.querySelector('.lab-host');
    // Your scripts, kept in localStorage under one key:
    //   { current: id, scripts: [{ id, name, source, seed, params, base, updated }] }
    // base: the sample a script was opened from (so an untouched copy is not opened twice).
    // Every change re-reads the stored library and edits only its own entry, so a second tab
    // does not wipe scripts made in the first.
    const LIB = 'lvs.scripts.v1';
    const DEFAULT_SAMPLE = 'roguelike';
    const STARTER = [
      'tag terrain { wall, floor }',
      'layers { level: grid of terrain }',
      '',
      '// every empty cell becomes wall or floor',
      'rule fill {',
      '    level[.]',
      '    =>',
      '    { any',
      '      (weight=45) level[wall]',
      '      (weight=55) level[floor]',
      '    }',
      '}',
      '',
      'sequence main {',
      '    resize(40, 25)',
      '    everywhere fill',
      '}',
      ''].join('\n');
    const note = document.getElementById('lab-note');
    const store = (() => { try { const st = window.localStorage; st.getItem(LIB); return st; } catch (e) { return null; } })();
    function say(msg) { note.textContent = msg || ''; }
    if (!store) say('Your scripts will not be kept: this browser is blocking local storage.');

    function readJson(key) {
      if (!store) return null;
      try { return JSON.parse(store.getItem(key)); } catch (e) { return null; }
    }
    function isScript(d) { return d && typeof d.source === 'string' && typeof d.id === 'string' && typeof d.name === 'string'; }
    let lib = { current: null, scripts: [] };   // in memory; the stored copy wins on every change
    function load() {
      const d = readJson(LIB);
      return d && Array.isArray(d.scripts) ? { current: d.current, scripts: d.scripts.filter(isScript) } : null;
    }
    function save() {
      if (!store) return;
      try { store.setItem(LIB, JSON.stringify(lib)); }
      catch (e) { say('Could not save (storage is full).'); }
    }
    // read-modify-write against what is stored now
    function change(fn) {
      lib = load() || lib;
      fn(lib);
      save();
      fillPicker();
    }
    const newId = () => Date.now().toString(36) + Math.random().toString(36).slice(2, 7);
    function byId(id) { return lib.scripts.find(x => x.id === id) || null; }
    function uniqueName(name) {
      name = (name || '').trim() || 'untitled';
      const taken = new Set(lib.scripts.map(x => x.name));
      if (!taken.has(name)) return name;
      let i = 2;
      while (taken.has(name + ' ' + i)) i++;
      return name + ' ' + i;
    }
    function make(name, d) {
      return { id: newId(), name: uniqueName(name), source: d.source, seed: Number.isInteger(d.seed) ? d.seed : 1,
        params: d.params || {}, base: d.base || null, updated: Date.now() };
    }
    function fromSample(s) { return { source: s.source, seed: s.seed, params: {}, base: s.name }; }
    // untouched: still exactly the sample it came from
    function untouched(x) {
      const base = x.base && sampleByName(x.base);
      return !!base && base.source === x.source;
    }

    lib = load();
    if (!lib || !lib.scripts.length) {   // first visit: start with the default sample
      lib = { current: null, scripts: [] };
      const s = sampleByName(DEFAULT_SAMPLE) || (window.LVS_SAMPLES || [])[0];
      lib.scripts.push(s ? make(s.name, fromSample(s)) : make('untitled', { source: STARTER, seed: 1 }));
      lib.current = lib.scripts[0].id;
      save();
    }
    if (!byId(lib.current)) lib.current = lib.scripts[0].id;

    // edits: debounced into the current script
    let dirty = null, timer = null, widget = null;
    function flush() {
      clearTimeout(timer);
      if (!dirty) return;
      const d = dirty;
      dirty = null;
      change(l => {
        const x = l.scripts.find(y => y.id === d.id);
        if (x) Object.assign(x, { source: d.source, seed: d.seed, params: d.params, updated: Date.now() });
      });
    }
    function onChange(d) {
      dirty = Object.assign({ id: lib.current }, d);
      clearTimeout(timer);
      timer = setTimeout(flush, 400);
    }
    window.addEventListener('pagehide', flush);
    document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'hidden') flush(); });

    // switch the editor to a script (saving the one being left first)
    function open(id) {
      flush();
      change(l => { l.current = id; });
      const x = byId(id);
      if (x && widget) widget.setSource(x.source, x.seed, x.params, true);
      say('');
    }
    function add(name, d) {
      flush();
      const x = make(name, d);
      change(l => { l.scripts.push(x); });
      open(x.id);
    }

    // the starting point: a #sample= link (as a new script, or its untouched copy), else the last script
    const hashed = /^#sample=([\w.-]+)$/.exec(location.hash);
    const linked = hashed && sampleByName(decodeURIComponent(hashed[1]));
    if (hashed) history.replaceState(null, '', location.pathname + location.search);
    if (linked) {
      const copy = lib.scripts.find(x => x.base === linked.name && untouched(x));
      if (copy) lib.current = copy.id;
      else { const x = make(linked.name, fromSample(linked)); lib.scripts.push(x); lib.current = x.id; }
      save();
    }
    const start = byId(lib.current);

    createWidget(labEl, {
      name: 'lab', source: start.source, seed: start.seed, params: parseParams(start.source)
    }, {
      height: 640, worker: true, dynamicParams: true, fileName: 'lab.lvs',
      autoplay: true,
      diagUnderEditor: true,
      initial: { source: start.source, seed: start.seed, params: start.params },
      onChange, onApi: api => { widget = api; window.LVS_lab = api; }
    });

    // tabs over the editor: one per script, in the order they were made (so they do not jump
    // around while you type). Click to open, double-click to rename, x to delete, + for a new one.
    const picker = document.createElement('div');
    picker.className = 'lab-tabs';
    picker.setAttribute('role', 'tablist');
    picker.setAttribute('aria-label', 'Your scripts');
    (labEl.querySelector('.edit-col') || labEl).prepend(picker);
    function fillPicker() {
      picker.innerHTML = '';
      lib.scripts.forEach(x => {
        const on = x.id === lib.current;
        const t = document.createElement('div');
        t.className = 'lab-tab' + (on ? ' on' : '');
        t.setAttribute('role', 'tab');
        t.setAttribute('aria-selected', on ? 'true' : 'false');
        t.tabIndex = on ? 0 : -1;
        t.dataset.id = x.id;
        t.title = x.name + ' (double-click to rename)';
        t.innerHTML = '<span class="lab-tab-name"></span>' +
          '<button type="button" class="lab-tab-close" tabindex="-1" aria-label="Delete script" title="Delete this script">&times;</button>';
        t.firstChild.textContent = x.name;
        picker.appendChild(t);
      });
      const plus = document.createElement('button');
      plus.type = 'button';
      plus.className = 'lab-tab-new';
      plus.title = 'Start a new script';
      plus.setAttribute('aria-label', 'New script');
      plus.innerHTML = '<i class="ph ph-plus"></i>';
      picker.appendChild(plus);
      // keep the open tab in view when it changes (not on every save, so the page stays put)
      const cur = picker.querySelector('.lab-tab.on');
      if (cur && shownTab !== lib.current) {
        shownTab = lib.current;
        picker.scrollLeft = Math.max(0, Math.min(picker.scrollLeft, cur.offsetLeft - 8),
          cur.offsetLeft + cur.offsetWidth + 40 - picker.clientWidth);
      }
    }
    let shownTab = null;
    fillPicker();
    picker.addEventListener('click', e => {
      if (e.target.closest('.lab-tab-new')) { document.getElementById('lab-new').click(); return; }
      const t = e.target.closest('.lab-tab');
      if (!t) return;
      if (e.target.closest('.lab-tab-close')) { removeScript(t.dataset.id); return; }
      if (t.dataset.id !== lib.current) open(t.dataset.id);
    });
    picker.addEventListener('dblclick', e => {
      const t = e.target.closest('.lab-tab');
      if (t && !e.target.closest('.lab-tab-close')) document.getElementById('lab-rename').click();
    });
    // a mouse wheel scrolls the tabs sideways (they have no scrollbar)
    picker.addEventListener('wheel', e => {
      if (Math.abs(e.deltaY) <= Math.abs(e.deltaX) || picker.scrollWidth <= picker.clientWidth) return;
      e.preventDefault();
      picker.scrollLeft += e.deltaY;
    }, { passive: false });
    // middle click closes, like browser tabs
    picker.addEventListener('auxclick', e => {
      const t = e.button === 1 && e.target.closest('.lab-tab');
      if (t) { e.preventDefault(); removeScript(t.dataset.id); }
    });
    picker.addEventListener('keydown', e => {
      const tabs = [...picker.querySelectorAll('.lab-tab')];
      const i = tabs.findIndex(t => t.dataset.id === lib.current);
      const j = e.key === 'ArrowRight' ? i + 1 : e.key === 'ArrowLeft' ? i - 1 : e.key === 'Home' ? 0 : e.key === 'End' ? tabs.length - 1 : -2;
      if (j === -2 || !tabs[j]) return;
      e.preventDefault();
      open(tabs[j].dataset.id);
      const t = picker.querySelector('.lab-tab.on');
      if (t) t.focus();
    });
    // another tab changed the library: refresh the list (the open script stays as it is here)
    window.addEventListener('storage', e => {
      if (e.key !== LIB) return;
      const cur = lib.current;
      lib = load() || lib;
      if (byId(cur)) lib.current = cur;
      fillPicker();
    });

    document.getElementById('lab-new').addEventListener('click', () => {
      const name = window.prompt('Name of the new script:', uniqueName('untitled'));
      if (name === null) return;
      add(name, { source: STARTER, seed: 1 });
    });
    document.getElementById('lab-rename').addEventListener('click', () => {
      const x = byId(lib.current);
      if (!x) return;
      const name = window.prompt('Rename "' + x.name + '" to:', x.name);
      if (name === null || !name.trim() || name.trim() === x.name) return;
      flush();
      const next = uniqueName(name);
      change(l => { const y = l.scripts.find(z => z.id === x.id); if (y) y.name = next; });
    });
    // delete a script; if it was the open one, its neighbour tab opens
    function removeScript(id) {
      const x = byId(id);
      if (!x || !window.confirm('Delete "' + x.name + '"? This cannot be undone.')) return;
      const wasOpen = id === lib.current;
      if (wasOpen) { dirty = null; clearTimeout(timer); } else flush();
      const at = lib.scripts.findIndex(y => y.id === id);
      change(l => { l.scripts = l.scripts.filter(y => y.id !== id); });
      if (!lib.scripts.length) { add('untitled', { source: STARTER, seed: 1 }); return; }
      if (wasOpen) open(lib.scripts[Math.min(at, lib.scripts.length - 1)].id);
    }
    document.getElementById('lab-delete').addEventListener('click', () => removeScript(lib.current));

    // a sample or a file always opens as a new script: nothing is overwritten
    const pick = document.getElementById('lab-sample');
    let optgroup = pick;
    (window.LVS_SAMPLES || []).forEach(s => {
      if (s.group && s.group !== optgroup.dataset.group) {
        optgroup = document.createElement('optgroup');
        optgroup.label = GROUP_TITLES[s.group] || s.group;
        optgroup.dataset.group = s.group;
        pick.appendChild(optgroup);
      }
      const o = document.createElement('option');
      o.value = s.name;
      o.textContent = s.name + '.lvs';
      optgroup.appendChild(o);
    });
    pick.addEventListener('change', () => {
      const s = sampleByName(pick.value);
      pick.value = '';
      if (s) add(s.name, fromSample(s));
    });
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
    document.getElementById('lab-open').addEventListener('change', e => {
      const f = e.target.files[0];
      e.target.value = '';
      if (!f) return;
      f.text().then(text => add(f.name.replace(/\.lvs$/i, ''), { source: text, seed: fileSeed(text) ?? 1 }));
    });
    document.getElementById('lab-download').addEventListener('click', () => {
      flush();
      const x = byId(lib.current);
      const src = widget ? widget.source() : (x && x.source) || '';
      const url = URL.createObjectURL(new Blob([src], { type: 'text/plain' }));
      const a = document.createElement('a');
      a.href = url;
      a.download = ((x && x.name) || 'lab').replace(/[\\/:*?"<>|]+/g, '_') + '.lvs';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    });

    // Ctrl/Cmd+S out of habit: every edit is kept already, so it saves at once and says so,
    // instead of the browser's "save page". With Shift it downloads the .lvs file.
    const savedIcon = document.getElementById('lab-saved');
    document.addEventListener('keydown', e => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey || e.key.toLowerCase() !== 's') return;
      e.preventDefault();
      if (e.shiftKey) { document.getElementById('lab-download').click(); return; }
      flush();
      if (!store || !savedIcon) return;   // the note already says the scripts are not kept
      savedIcon.classList.remove('flash');
      void savedIcon.offsetWidth;   // restart the blink on a quick second save
      savedIcon.classList.add('flash');
      savedIcon.lastChild.textContent = 'Saved';
    }, true);

    // reference
    const refRoot = document.getElementById('reference');
    if (refRoot) {
      const entries = window.LVS_API || [];
      const q = document.getElementById('ref-q');
      const chips = document.getElementById('ref-kinds');
      const out = document.getElementById('ref-results');
      let kind = 'all';
      ['all', 'function', 'operation', 'mode', 'keyword', 'attribute'].forEach(k => {
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
      // The inline markdown of docs/reference.md: `code`, **bold**, *italic* (escaped first).
      function inline(s) {
        return esc(s).split(/(`[^`]*`)/).map(part => part.startsWith('`') && part.length > 1
          ? '<code>' + part.slice(1, -1) + '</code>'
          : part.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>').replace(/\*([^*\s][^*]*)\*/g, '<em>$1</em>')
        ).join('');
      }
      // The detail of an entry: its blocks, then the related entries as links into the search.
      function detail(e) {
        let html = (e.doc || []).map(b =>
          b.p ? '<p>' + inline(b.p) + '</p>' :
          b.ul ? '<ul>' + b.ul.map(li => '<li>' + inline(li) + '</li>').join('') + '</ul>' :
          '<pre><code>' + highlight(b.code) + '</code></pre>').join('');
        if (e.related && e.related.length) {
          html += '<p class="ref-related">See also: ' + e.related.map(r =>
            '<a href="#" data-ref="' + esc(r) + '">' + esc(r) + '</a>').join(', ') + '</p>';
        }
        return html;
      }
      const open = new Set();   // names of the expanded entries, kept across searches
      function row(e) {
        const el = document.createElement('div');
        el.className = 'ref-row' + (open.has(e.name) ? ' open' : '');
        el.innerHTML = '<div class="ref-head" role="button" tabindex="0" aria-expanded="' + open.has(e.name) + '">' +
            '<code>' + esc(e.signature) + '</code><span class="ref-sum">' + inline(e.summary) + '</span>' +
            '<button type="button" title="Insert at the cursor">insert</button>' +
          '</div>' +
          '<div class="ref-detail">' + detail(e) + '</div>';
        const head = el.querySelector('.ref-head');
        const toggle = () => {
          const on = el.classList.toggle('open');
          head.setAttribute('aria-expanded', on);
          if (on) open.add(e.name); else open.delete(e.name);
        };
        head.onclick = ev => { if (!ev.target.closest('button')) toggle(); };
        head.onkeydown = ev => {
          if ((ev.key === 'Enter' || ev.key === ' ') && ev.target === head) { ev.preventDefault(); toggle(); }
        };
        el.querySelector('button').onclick = () => widget && widget.insert(e.insert);
        el.querySelectorAll('[data-ref]').forEach(a => a.onclick = ev => {
          ev.preventDefault();
          const name = a.dataset.ref;
          q.value = name;
          open.add(name);
          kind = 'all';   // the related entry may be of another kind than the filter
          chips.querySelectorAll('.chip').forEach(c => c.classList.toggle('on', c.textContent === 'all'));
          show();
          const hit = out.querySelector('.ref-row.open');
          if (hit) hit.scrollIntoView({ block: 'nearest' });
        });
        return el;
      }
      function show() {
        out.innerHTML = '';
        const hits = window.LVS_searchApi(entries, q.value, kind);
        if (!hits.length) {
          out.innerHTML = '<p class="ref-none">No results for <em>' + esc(q.value) +
            '</em>.</p>';
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
})();
