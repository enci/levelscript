import * as vscode from 'vscode';
import * as path from 'path';
import { InspectionResult, RunLayer, RunState } from './types';
import { getCached } from './cache';
import { inspectJson, runBegin, runEnd, runFinish, runLastError, runState, runStep } from './wasm';
import { anyColor, emptyColor, tagColor } from './palette';

interface RenderLayer {
    name: string;
    width: number;
    height: number;
    colors: string[];
    values: (string | number | null)[];
}

interface RenderPayload {
    statementIndex: number;
    statementCount: number;
    done: boolean;
    layers: RenderLayer[];
}

interface Session {
    panel: vscode.WebviewPanel;
    id: number;
}

function numberColor(t: number, dark: boolean): string {
    t = Math.max(0, Math.min(1, t));
    const lo = dark ? [42, 46, 58] : [232, 238, 250];
    const hi = dark ? [58, 130, 150] : [140, 205, 230];
    const c = lo.map((v, i) => Math.round(v + (hi[i] - v) * t));
    return `rgb(${c[0]},${c[1]},${c[2]})`;
}

function cellColor(
    cell: string[] | number | null,
    layer: RunLayer,
    symbols: InspectionResult['symbols'],
    isDark: boolean,
    range: { min: number; max: number } | null
): string {
    if (layer.isNumber) {
        if (cell === null) return emptyColor(isDark);
        const { min, max } = range!;
        const t = max === min ? 0.5 : (Number(cell) - min) / (max - min);
        return numberColor(t, isDark);
    }
    const names = cell as string[];
    if (!names || names.length === 0) return emptyColor(isDark);
    const layerSym = symbols.layers.find(l => l.name === layer.name);
    const tagIdx = layerSym ? symbols.tags.findIndex(t => t.name === layerSym.type) : -1;
    if (tagIdx < 0) return anyColor(isDark);
    const valueIdx = symbols.tags[tagIdx].values.findIndex(v => v.name === names[0]);
    if (valueIdx < 0) return anyColor(isDark);
    return tagColor(valueIdx + tagIdx * 12, isDark);
}

function toRender(state: RunState, symbols: InspectionResult['symbols'], isDark: boolean): RenderPayload {
    const { width, height } = state.level;
    const layers: RenderLayer[] = state.level.layers.map(layer => {
        let range: { min: number; max: number } | null = null;
        if (layer.isNumber) {
            let min = Infinity, max = -Infinity;
            for (const c of layer.cells) {
                if (typeof c === 'number') {
                    if (c < min) min = c;
                    if (c > max) max = c;
                }
            }
            range = Number.isFinite(min) ? { min, max } : { min: 0, max: 0 };
        }
        const colors: string[] = [];
        const values: (string | number | null)[] = [];
        for (const cell of layer.cells) {
            colors.push(cellColor(cell, layer, symbols, isDark, range));
            values.push(layer.isNumber
                ? (cell as number | null)
                : ((cell as string[]).length ? (cell as string[]).join('+') : null));
        }
        return { name: layer.name, width, height, colors, values };
    });
    return { statementIndex: state.statementIndex, statementCount: state.statementCount, done: state.done, layers };
}

function nonce(): string {
    let s = '';
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    for (let i = 0; i < 32; i++) s += chars.charAt(Math.floor(Math.random() * chars.length));
    return s;
}

function renderHtml(csp: string, n: string): string {
    return /* html */ `<!DOCTYPE html>
<html>
<head>
<meta http-equiv="Content-Security-Policy" content="${csp}">
<style>
  body { font-family: var(--vscode-font-family); color: var(--vscode-foreground); padding: 8px 12px; }
  #toolbar { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; flex-wrap: wrap; }
  button { background: var(--vscode-button-background); color: var(--vscode-button-foreground);
           border: none; padding: 4px 10px; cursor: pointer; border-radius: 2px; }
  button:hover { background: var(--vscode-button-hoverBackground); }
  button:disabled { opacity: 0.5; cursor: default; }
  #progress { opacity: 0.8; }
  .layer { margin-bottom: 16px; }
  .layer h4 { margin: 0 0 4px 0; font-weight: normal; opacity: 0.85; }
  canvas { image-rendering: pixelated; border: 1px solid var(--vscode-panel-border); }
  #info { min-height: 1.2em; opacity: 0.85; font-family: var(--vscode-editor-font-family); }
  #error { color: var(--vscode-errorForeground); white-space: pre-wrap; font-family: var(--vscode-editor-font-family); }
</style>
</head>
<body>
  <div id="toolbar">
    <button id="step">Step</button>
    <button id="finish">Run to End</button>
    <button id="restart">Restart</button>
    <span id="progress"></span>
  </div>
  <div id="info">&nbsp;</div>
  <div id="error"></div>
  <div id="layers"></div>
<script nonce="${n}">
(function () {
  const vscode = acquireVsCodeApi();
  const layersEl = document.getElementById('layers');
  const progressEl = document.getElementById('progress');
  const infoEl = document.getElementById('info');
  const errorEl = document.getElementById('error');
  const canvases = new Map();

  document.getElementById('step').addEventListener('click', () => vscode.postMessage({ type: 'step' }));
  document.getElementById('finish').addEventListener('click', () => vscode.postMessage({ type: 'finish' }));
  document.getElementById('restart').addEventListener('click', () => vscode.postMessage({ type: 'restart' }));

  function cellSize(w, h) {
    return Math.max(4, Math.min(24, Math.floor(520 / Math.max(w, h, 1))));
  }

  function ensureCanvas(layer) {
    let c = canvases.get(layer.name);
    if (c) return c;
    const wrap = document.createElement('div');
    wrap.className = 'layer';
    const h4 = document.createElement('h4');
    h4.textContent = layer.name;
    const canvas = document.createElement('canvas');
    wrap.appendChild(h4);
    wrap.appendChild(canvas);
    layersEl.appendChild(wrap);
    c = { canvas, ctx: canvas.getContext('2d') };
    canvas.addEventListener('mousemove', ev => {
      const rect = canvas.getBoundingClientRect();
      const size = canvas._cellSize || 1;
      const x = Math.floor((ev.clientX - rect.left) / size);
      const y = Math.floor((ev.clientY - rect.top) / size);
      const w = canvas._w || 0, h = canvas._h || 0;
      if (x < 0 || y < 0 || x >= w || y >= h) return;
      const v = canvas._values ? canvas._values[y * w + x] : null;
      infoEl.textContent = layer.name + ' (' + x + ', ' + y + '): ' + (v === null || v === undefined ? 'empty' : v);
    });
    canvas.addEventListener('mouseleave', () => { infoEl.innerHTML = '&nbsp;'; });
    canvases.set(layer.name, c);
    return c;
  }

  function draw(layer) {
    const { canvas, ctx } = ensureCanvas(layer);
    const size = cellSize(layer.width, layer.height);
    canvas._cellSize = size;
    canvas._w = layer.width;
    canvas._h = layer.height;
    canvas._values = layer.values;
    canvas.width = layer.width * size;
    canvas.height = layer.height * size;
    for (let y = 0; y < layer.height; y++) {
      for (let x = 0; x < layer.width; x++) {
        ctx.fillStyle = layer.colors[y * layer.width + x];
        ctx.fillRect(x * size, y * size, size, size);
      }
    }
  }

  window.addEventListener('message', ev => {
    const msg = ev.data;
    if (msg.type === 'error') {
      errorEl.textContent = msg.message;
      layersEl.innerHTML = '';
      canvases.clear();
      progressEl.textContent = '';
      document.getElementById('step').disabled = true;
      document.getElementById('finish').disabled = true;
      return;
    }
    if (msg.type !== 'state') return;
    errorEl.textContent = '';
    const r = msg.render;
    progressEl.textContent = 'Statement ' + Math.max(0, r.statementIndex + 1) + ' / ' + r.statementCount +
      (r.done ? '  (done)' : '');
    document.getElementById('step').disabled = r.done;
    document.getElementById('finish').disabled = r.done;
    for (const layer of r.layers) draw(layer);
  });

  vscode.postMessage({ type: 'ready' });
})();
</script>
</body>
</html>`;
}

export function registerRunner(ctx: vscode.ExtensionContext) {
    const sessions = new Map<string, Session>();

    function symbolsFor(doc: vscode.TextDocument): InspectionResult['symbols'] {
        const cached = getCached(doc.uri.toString());
        if (cached) return cached.symbols;
        return inspectJson(doc.getText(), doc.uri.fsPath).symbols;
    }

    function isDarkTheme(): boolean {
        return vscode.window.activeColorTheme.kind === vscode.ColorThemeKind.Dark
            || vscode.window.activeColorTheme.kind === vscode.ColorThemeKind.HighContrast;
    }

    function post(session: Session, doc: vscode.TextDocument, state: RunState) {
        const render = toRender(state, symbolsFor(doc), isDarkTheme());
        session.panel.webview.postMessage({ type: 'state', render });
    }

    function start(doc: vscode.TextDocument, seed: number) {
        const uri = doc.uri.toString();
        const existing = sessions.get(uri);
        if (existing) runEnd(existing.id);

        const id = runBegin(doc.getText(), doc.uri.fsPath, seed);
        if (id < 0) {
            const message = runLastError();
            if (existing) {
                // Keep the session entry (and its panel) around so a later
                // 'restart' reuses this panel instead of spawning another.
                existing.id = -1;
                existing.panel.webview.postMessage({ type: 'error', message });
            } else {
                vscode.window.showErrorMessage(`LevelScript run failed:\n${message}`);
            }
            return;
        }

        let panel = existing?.panel;
        if (!panel) {
            panel = vscode.window.createWebviewPanel(
                'levelscriptRun',
                `Run: ${path.basename(doc.uri.fsPath)}`,
                vscode.ViewColumn.Beside,
                { enableScripts: true, retainContextWhenHidden: true }
            );
            const n = nonce();
            const csp = `default-src 'none'; style-src ${panel.webview.cspSource} 'unsafe-inline'; script-src 'nonce-${n}';`;
            panel.webview.html = renderHtml(csp, n);

            panel.onDidDispose(() => {
                const s = sessions.get(uri);
                if (s) runEnd(s.id);
                sessions.delete(uri);
            });

            panel.webview.onDidReceiveMessage(msg => {
                const s = sessions.get(uri);
                if (!s) return;
                switch (msg.type) {
                    case 'ready':
                        post(s, doc, runState(s.id));
                        break;
                    case 'step':
                        post(s, doc, runStep(s.id));
                        break;
                    case 'finish':
                        post(s, doc, runFinish(s.id));
                        break;
                    case 'restart':
                        start(doc, (Math.random() * 0xffffffff) >>> 0);
                        break;
                }
            });
        } else {
            panel.reveal(vscode.ViewColumn.Beside, true);
        }

        sessions.set(uri, { panel, id });
        post({ panel, id }, doc, runState(id));
    }

    ctx.subscriptions.push(
        vscode.commands.registerCommand('levelscript.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'levelscript') {
                vscode.window.showWarningMessage('Open a .ls file to run it.');
                return;
            }
            start(editor.document, (Date.now() ^ Math.floor(Math.random() * 0xffffffff)) >>> 0);
        }),
        { dispose: () => { for (const s of sessions.values()) runEnd(s.id); } }
    );
}
