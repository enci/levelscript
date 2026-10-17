import * as vscode from 'vscode';
import * as path from 'path';

// "LevelScript Tileset": a read-only viewer for .png tilesets - unsmoothed
// zoom, the tile grid, and the index of the tile under the cursor (row-major
// from 0, as the debugger numbers them). Offered for every .png through
// "Reopen Editor With…"; the built-in image preview stays the default.
//
// The tile size comes from the first `<script>.lvs.json` whose `tilesets`
// entry points at this image, else the debugger's default (16).
const VIEW_TYPE = 'levelscript.tileset';
const DEFAULT_TILE = 16;

interface TileSize { w: number; h: number; source?: string }

async function tileSizeFor(image: vscode.Uri): Promise<TileSize> {
    const configs = await vscode.workspace.findFiles('**/*.lvs.json', '**/node_modules/**');
    for (const cfg of configs) {
        try {
            const json = JSON.parse(Buffer.from(await vscode.workspace.fs.readFile(cfg)).toString('utf8'));
            for (const ts of json.tilesets ?? []) {
                if (typeof ts.path !== 'string') continue;
                const resolved = path.resolve(path.dirname(cfg.fsPath), ts.path);
                if (path.normalize(resolved) !== path.normalize(image.fsPath)) continue;
                return {
                    w: ts.tile_w ?? DEFAULT_TILE,
                    h: ts.tile_h ?? DEFAULT_TILE,
                    source: vscode.workspace.asRelativePath(cfg),
                };
            }
        } catch { /* not ours to complain about; the debugger reports bad configs */ }
    }
    return { w: DEFAULT_TILE, h: DEFAULT_TILE };
}

const provider: vscode.CustomReadonlyEditorProvider = {
    openCustomDocument(uri) {
        return { uri, dispose() { } };
    },
    async resolveCustomEditor(doc, panel) {
        panel.webview.options = {
            enableScripts: true,
            localResourceRoots: [vscode.Uri.joinPath(doc.uri, '..')],
        };
        const size = await tileSizeFor(doc.uri);
        panel.webview.html = html(panel.webview, panel.webview.asWebviewUri(doc.uri), size);
    },
};

export function registerTilesetView(ctx: vscode.ExtensionContext) {
    ctx.subscriptions.push(
        vscode.window.registerCustomEditorProvider(VIEW_TYPE, provider, {
            webviewOptions: { retainContextWhenHidden: true },
        }),
    );
}

function html(webview: vscode.Webview, src: vscode.Uri, size: TileSize): string {
    const nonce = Array.from({ length: 16 }, () => Math.floor(Math.random() * 36).toString(36)).join('');
    const from = size.source ? `from ${size.source}` : 'default';
    return /* html */ `<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy"
      content="default-src 'none'; img-src ${webview.cspSource}; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
<style>
  body { margin: 0; padding: 0; color: var(--vscode-foreground); font: var(--vscode-font-size) var(--vscode-font-family); }
  .bar { position: sticky; top: 0; display: flex; gap: 12px; align-items: center; flex-wrap: wrap;
         padding: 6px 10px; background: var(--vscode-editor-background);
         border-bottom: 1px solid var(--vscode-panel-border); z-index: 1; }
  .bar input[type=number] { width: 4em; background: var(--vscode-input-background);
         color: var(--vscode-input-foreground); border: 1px solid var(--vscode-input-border, transparent); }
  .bar button { background: var(--vscode-button-secondaryBackground); color: var(--vscode-button-secondaryForeground);
         border: none; padding: 2px 8px; cursor: pointer; }
  .dim { opacity: 0.7; }
  #info { margin-left: auto; font-family: var(--vscode-editor-font-family); }
  .stage { padding: 16px; }
  canvas { display: block; image-rendering: pixelated;
           background: repeating-conic-gradient(#8884 0% 25%, transparent 0% 50%) 0 0 / 16px 16px; }
</style>
</head>
<body>
<div class="bar">
  <span>Tile <input id="tw" type="number" min="1" value="${size.w}"> × <input id="th" type="number" min="1" value="${size.h}">
    <span class="dim">(${from})</span></span>
  <label><input id="grid" type="checkbox" checked> Grid</label>
  <label><input id="nums" type="checkbox"> Indices</label>
  <span><button id="out">−</button> <span id="zoom"></span> <button id="in">+</button></span>
  <span id="info" class="dim"></span>
</div>
<div class="stage"><canvas id="c"></canvas></div>
<script nonce="${nonce}">
  const img = new Image();
  const c = document.getElementById('c'), g = c.getContext('2d');
  const $ = id => document.getElementById(id);
  let zoom = 1, hover = -1;

  function tile() { return { w: Math.max(1, +$('tw').value | 0), h: Math.max(1, +$('th').value | 0) }; }
  function cols() { return Math.max(1, Math.floor(img.width / tile().w)); }
  function rows() { return Math.max(1, Math.floor(img.height / tile().h)); }

  function draw() {
    const { w, h } = tile();
    c.width = img.width * zoom; c.height = img.height * zoom;
    g.imageSmoothingEnabled = false;
    g.drawImage(img, 0, 0, c.width, c.height);
    if ($('grid').checked) {
      g.strokeStyle = 'rgba(255,255,255,0.35)'; g.lineWidth = 1;
      g.beginPath();
      for (let x = 0; x <= cols(); x++) { g.moveTo(x * w * zoom + 0.5, 0); g.lineTo(x * w * zoom + 0.5, rows() * h * zoom); }
      for (let y = 0; y <= rows(); y++) { g.moveTo(0, y * h * zoom + 0.5); g.lineTo(cols() * w * zoom, y * h * zoom + 0.5); }
      g.stroke();
    }
    if ($('nums').checked && w * zoom >= 16) {
      g.font = Math.min(12, h * zoom / 2) + 'px monospace'; g.textBaseline = 'top';
      for (let i = 0; i < cols() * rows(); i++) {
        const x = (i % cols()) * w * zoom + 2, y = Math.floor(i / cols()) * h * zoom + 2;
        g.fillStyle = 'rgba(0,0,0,0.6)'; g.fillRect(x - 1, y - 1, g.measureText(i).width + 2, 12);
        g.fillStyle = '#fff'; g.fillText(i, x, y);
      }
    }
    if (hover >= 0) {
      g.strokeStyle = '#ffcc00'; g.lineWidth = 2;
      g.strokeRect((hover % cols()) * w * zoom + 1, Math.floor(hover / cols()) * h * zoom + 1, w * zoom - 2, h * zoom - 2);
    }
    $('zoom').textContent = zoom + '×';
  }

  function setZoom(z) { zoom = Math.min(64, Math.max(1, z)); draw(); }

  img.onload = () => {
    // fit roughly 512px wide to start; pixel art is usually tiny
    setZoom(Math.max(1, Math.floor(512 / Math.max(img.width, img.height))));
    $('info').textContent = img.width + '×' + img.height + ' px';
  };
  img.src = ${JSON.stringify(src.toString())};

  c.addEventListener('mousemove', e => {
    const { w, h } = tile();
    const px = Math.floor(e.offsetX / zoom), py = Math.floor(e.offsetY / zoom);
    const col = Math.floor(px / w), row = Math.floor(py / h);
    const inside = col < cols() && row < rows();
    hover = inside ? row * cols() + col : -1;
    $('info').textContent = inside
      ? 'tile ' + hover + '  (col ' + col + ', row ' + row + ')  px ' + px + ',' + py
      : img.width + '×' + img.height + ' px';
    draw();
  });
  c.addEventListener('mouseleave', () => { hover = -1; $('info').textContent = img.width + '×' + img.height + ' px'; draw(); });
  window.addEventListener('wheel', e => {
    if (!e.ctrlKey && !e.metaKey) return;
    e.preventDefault(); setZoom(zoom + (e.deltaY < 0 ? 1 : -1));
  }, { passive: false });
  $('in').onclick = () => setZoom(zoom * 2);
  $('out').onclick = () => setZoom(Math.floor(zoom / 2));
  for (const id of ['tw', 'th', 'grid', 'nums']) $(id).addEventListener('input', draw);
</script>
</body>
</html>`;
}
