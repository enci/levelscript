#!/usr/bin/env python3
"""LevelScript icon generator.

A single ribbon on a uniform 5x5 grid: the L's stem runs down, the shared
bottom bar runs right, and the ribbon continues up and around into the S.
The ribbon (front face) is drawn as one rounded shape. Pieces are drawn on
top: mostly flaps (stretches showing the back face), each a set of cells with
its own corner radii. Every fold, where a piece meets the rest of the ribbon,
gets the same fold shadow; pieces can also darken toward their deep end.

Usage:
    pip install -r tools/requirements.txt
    python tools/levelscript_icon.py

Writes, relative to the repository root (so it works from any working
directory):
    resources/icon/levelscript.svg       the master
    resources/icon/levelscript-flat.svg  the flat variant, for tiny sizes
    resources/icon/levelscript.png       256 px, the debugger's window icon
    resources/icon/levelscript-macos.png 512 px with Apple's icon margin, the
                                         debugger's Dock icon on macOS
    resources/icon/levelscript.ico       Windows executables and installer:
                                         flat up to FLAT_MAX px, full above
    extension/icon.png                   128 px, the VS Code extension
    extension/file-icon.svg              the flat variant, .ls files in VS Code
    packaging/windows/wizard-*.bmp       Inno Setup wizard images, 100% and 200%
    packaging/macos/background*.png      Installer background (icon bottom-left),
                                         1x and @2x; build_pkg.py joins them
"""
from __future__ import annotations

import pathlib
from collections import defaultdict

# ---------------------------------------------------------------- config ---

ROOT = pathlib.Path(__file__).resolve().parent.parent   # the repository root

N = 5  # grid is N x N cells; A1 = top-left

# Ribbon path as waypoints; consecutive waypoints share a row or a column.
RIBBON = "A1 A5 E5 E3 C3 C1 E1"

# Ribbon thickness in cells (1 = uniform grid). Columns the ribbon runs down
# and rows it runs along get this width/height; the remaining columns and rows
# (the counters between the bands) share what is left, so the ribbon grows
# inward and the outline stays the same size. All other lengths in cells
# (radii, shadow widths) use the nominal cell, 1/N of the icon.
THICKNESS = 1.25

# Corner radii of the ribbon outline, in cells (0.5 = semicircular band end).
# Where two corners share an edge too short for both, a piece's explicit
# override wins first, then the icon-square corner, and the other corner gets
# the remainder (so the band ends at A1 and E1 become
# asymmetric: 0.8 on the outside, 0.2 on the inside). Between two corners of
# the same kind, the smaller is kept and the larger gets the remainder.
RADII = dict(
    outer=1.0,    # convex corners on the four corners of the icon square
    convex=0.5,   # all other convex corners
    concave=0.0,  # inside corners
)

# Pieces drawn in order on top of the ribbon. Most are flaps: stretches that
# show the ribbon's back face. A fold is where a piece meets the rest of the
# ribbon; folds are found automatically (a piece edge touching a ribbon cell
# outside the piece, unless a later piece covers that edge) and each gets the
# same fold shadow (FOLD_SHADOW).
#   cells   : cell range(s)
#   face    : "back" (default) or "front"
#   corners : radius overrides in cells, keyed by grid point (x, y) with
#             (0, 0) the top-left corner of the icon and (5, 5) the bottom-right;
#             corners not listed follow RADII, like the ribbon
#   shadow  : optional per-fold length in cells, keyed by side, overriding
#             FOLD_SHADOW["width"], e.g. {"right": 1.5}
#   depth   : optional side that is far away (deep); the piece darkens toward
#             it with DEPTH strength, so it reads as rising toward the viewer
# Convention: folds sit in the straight run next to a corner, never in the
# corner cell itself; the ribbon changes direction by bending through the
# corner cell (its rounded outer corner). At a fold the piece's corner on the
# ribbon's outer edge is rounded (0.5) and the inner one is square.
PIECES = [
    # L stem on the front face: deep at the bottom, rising toward A1. It bends
    # through the corner A5 and continues into B5, where it is mostly hidden by
    # the bar; only its corner shows.
    dict(cells="A1:A5 B5", face="front", corners={(2, 4): 0, (2, 5): 0}, depth="bottom"),
    # shared bottom bar: folds out from the stem at B5, rounded at the
    # bottom-left of B5; deep at D5 where it comes out from the S-bottom fold
    dict(cells="B5:D5", corners={(1, 4): 0, (1, 5): 0.5, (4, 4): 0, (4, 5): 0.5},
         depth="right"),
    # middle of the S; folds from column E (right) and into column C (left)
    dict(cells="D3", corners={(3, 2): 0, (4, 2): 0.5, (4, 3): 0, (3, 3): 0.5}),
    # top band; folds out of column C (left)
    dict(cells="D1:E1", corners={(3, 0): 0.5, (3, 1): 0}),
]

# Colours. Both faces share one gradient axis (top-left to bottom-right) so
# the ribbon reads as a single material; the back face is the front mixed
# toward BACK_TINT (a deep tint keeps it saturated, unlike mixing with black).
# BACK_TINT also colours the fold shadows and depth darkening.
FRONT = ["#F5DD7E", "#3FC46E", "#0F8C72"]  # gradient stops, top-left to bottom-right
BACK_TINT = "#06352C"
BACK_DARKEN = 0.38     # 0 = same as front, 1 = BACK_TINT
FOLD_SHADOW = dict(
    opacity=0.55,      # darkness at the fold, fading to 0
    width=1.2,         # how far it reaches into the piece, in cells
)
DEPTH = 0.45           # darkness at a piece's far end (see PIECES "depth")
HIGHLIGHT = 0.22       # white sheen from the top-left; 0 disables

# Flat variant for tiny sizes (file icons): one solid colour per face, no
# gradients, shadows or highlight. The face change alone marks the folds.
FLAT = dict(
    front="#3FC46E",
    back="#339B75",
)
FLAT_MAX = 32   # .ico sizes up to this use the flat variant
# macOS draws app icons at 824 of 1024 px, centred; a full-bleed icon looks
# oversized in the Dock next to every other app.
MACOS_PADDING = 100 / 1024

# Installer art. Inno Setup's wizard: a small image top-right on the inner
# pages (on white) and a tall one on the Welcome/Finished pages, given at 100%
# and 200% so it stays sharp on high-DPI screens. Sizes in pixels at 100%.
WIZARD_SMALL = (55, 55)
WIZARD_SMALL_ICON = 47       # inside WIZARD_SMALL, leaving a margin
WIZARD_LARGE = (164, 314)
WIZARD_LARGE_BG = "#EAF6EE"   # the band behind the icon: a light tint, so the dark folds read
# macOS Installer background: the icon in the bottom-left corner, on a
# transparent canvas (so one image serves light and dark), in points.
PKG_BACKGROUND = (128, 128)
PKG_ICON = 80
PKG_INSET = 20   # from the window's left and bottom edges

PADDING = 0.0          # fraction of the canvas left empty on each side
CANVAS = 1024
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]

# ------------------------------------------------------------- geometry ---


def cell(s: str) -> tuple[int, int]:
    return ord(s[0].upper()) - ord("A"), int(s[1:]) - 1


def cell_range(spec: str) -> set[tuple[int, int]]:
    """'B2', 'A1:A4', or several of those separated by spaces."""
    out = set()
    for part in spec.split():
        a, b = part.split(":") if ":" in part else (part, part)
        (c0, r0), (c1, r1) = cell(a), cell(b)
        out |= {(c, r) for c in range(min(c0, c1), max(c0, c1) + 1)
                for r in range(min(r0, r1), max(r0, r1) + 1)}
    return out


def ribbon_cells(spec: str) -> set[tuple[int, int]]:
    way = [cell(w) for w in spec.split()]
    cells = {way[0]}
    for a, b in zip(way, way[1:]):
        if a[0] != b[0] and a[1] != b[1]:
            raise ValueError(f"{a} -> {b}: waypoints must share a row or column")
        step = ((b[0] > a[0]) - (b[0] < a[0]), (b[1] > a[1]) - (b[1] < a[1]))
        c = a
        while c != b:
            c = (c[0] + step[0], c[1] + step[1])
            cells.add(c)
    return cells


def cross(u, v):
    return u[0] * v[1] - u[1] * v[0]


def boundary_loops(cells):
    """Outline loops of a cell set, clockwise on screen."""
    edges = set()
    for c, r in cells:
        p = [(c, r), (c + 1, r), (c + 1, r + 1), (c, r + 1)]
        for i in range(4):
            e = (p[i], p[(i + 1) % 4])
            if (e[1], e[0]) in edges:
                edges.remove((e[1], e[0]))
            else:
                edges.add(e)
    out = defaultdict(list)
    for a, b in edges:
        out[a].append(b)
    loops = []
    while edges:
        a, b = min(edges)
        edges.discard((a, b))
        out[a].remove(b)
        loop, prev, cur = [a], a, b
        while cur != a:
            loop.append(cur)
            d_in = (cur[0] - prev[0], cur[1] - prev[1])
            nxt = max(out[cur], key=lambda q: cross(d_in, (q[0] - cur[0], q[1] - cur[1])))
            out[cur].remove(nxt)
            edges.discard((cur, nxt))
            prev, cur = cur, nxt
        n = len(loop)
        loops.append([loop[i] for i in range(n) if cross(
            (loop[i][0] - loop[i - 1][0], loop[i][1] - loop[i - 1][1]),
            (loop[(i + 1) % n][0] - loop[i][0], loop[(i + 1) % n][1] - loop[i][1])) != 0])
    return loops


def band_lines(spec: str):
    """Column and row indices the ribbon runs along (its bands)."""
    way = [cell(w) for w in spec.split()]
    cols, rows = set(), set()
    for a, b in zip(way, way[1:]):
        if a[0] == b[0]:
            cols.add(a[0])
        else:
            rows.add(a[1])
    return cols, rows


def sizes(bands):
    """Widths of the N columns (or rows), in nominal cells, summing to N."""
    rest = N - len(bands)
    other = (N - THICKNESS * len(bands)) / rest if rest else 0
    if rest and other <= 0:
        raise ValueError(f"THICKNESS {THICKNESS} leaves no room between the bands")
    return [THICKNESS if i in bands else other for i in range(N)]


class Grid:
    def __init__(self, canvas=CANVAS, padding=PADDING):
        self.size = canvas * (1 - 2 * padding)
        self.o = canvas * padding
        self.c = self.size / N  # nominal cell, the unit for radii and shadows
        cols, rows = band_lines(RIBBON)
        self.xs = self._lines(sizes(cols))
        self.ys = self._lines(sizes(rows))

    def _lines(self, widths):
        out, acc = [self.o], self.o
        for w in widths:
            acc += w * self.c
            out.append(acc)
        return out

    def pt(self, gx, gy):
        """Canvas position of grid point (gx, gy); gx, gy are integers 0..N."""
        return self.xs[gx], self.ys[gy]


def corner_table(cells, g: Grid, override=None):
    """Per outline loop: grid vertices, canvas points, convexity, final radii.

    override maps a grid point to a radius in cells; other points use RADII.
    """
    override = override or {}
    icon_corners = {(0, 0), (N, 0), (0, N), (N, N)}
    table = []
    for loop in boundary_loops(cells):
        n = len(loop)
        P = [g.pt(*v) for v in loop]
        convex, rad, rank = [], [], []
        for i in range(n):
            a, b, c = loop[i - 1], loop[i], loop[(i + 1) % n]
            cv = cross((b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])) > 0
            convex.append(cv)
            kind = "outer" if cv and b in icon_corners else "convex" if cv else "concave"
            # explicit overrides outrank the RADII rules when clamping
            rank.append(3 if b in override else {"outer": 2, "convex": 1, "concave": 0}[kind])
            rad.append(override.get(b, RADII[kind]) * g.c)
        # an edge can't hold more than its length of rounding: the higher-ranked
        # corner keeps its radius, the other gets what is left; on a tie the
        # smaller radius (up to half the edge) is kept
        lim = rad[:]
        for i in range(n):
            j = (i + 1) % n
            length = abs(P[j][0] - P[i][0]) + abs(P[j][1] - P[i][1])
            if rad[i] + rad[j] <= length:
                continue
            if rank[i] != rank[j]:
                hi, lo = (i, j) if rank[i] > rank[j] else (j, i)
                keep = min(rad[hi], length)
                lim[hi], lim[lo] = min(lim[hi], keep), min(lim[lo], length - keep)
            else:
                small = min(rad[i], rad[j], length / 2)
                for k, other in ((i, j), (j, i)):
                    lim[k] = min(lim[k], small if rad[k] <= rad[other] else length - small)
        table.append((loop, P, convex, lim))
    return table


def rounded_path(cells, g: Grid, override=None) -> str:
    """SVG path of a cell set with rounded corners (see corner_table)."""

    def unit(p, q):
        dx, dy = q[0] - p[0], q[1] - p[1]
        m = abs(dx) + abs(dy)
        return dx / m, dy / m

    parts = []
    for _, P, convex, rad in corner_table(cells, g, override):
        n = len(P)
        seg = []
        for i in range(n + 1):
            k = i % n
            p, r = P[k], rad[k]
            u_in, u_out = unit(P[k - 1], p), unit(p, P[(k + 1) % n])
            before = (p[0] - u_in[0] * r, p[1] - u_in[1] * r)
            after = (p[0] + u_out[0] * r, p[1] + u_out[1] * r)
            if i == 0:
                seg.append(f"M{after[0]:.2f},{after[1]:.2f}")
                continue
            seg.append(f"L{before[0]:.2f},{before[1]:.2f}")
            if r > 0:
                seg.append(f"A{r:.2f},{r:.2f} 0 0 {1 if convex[k] else 0} "
                           f"{after[0]:.2f},{after[1]:.2f}")
        parts.append(" ".join(seg) + " Z")
    return " ".join(parts)


# ---------------------------------------------------------------- render ---


def mix(c1, c2, t):
    a = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(c2[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(a, b))


def gradient(gid, colors, a, b, opacities=None):
    stops = []
    for i, col in enumerate(colors):
        off = i / max(1, len(colors) - 1)
        op = f' stop-opacity="{opacities[i]}"' if opacities else ""
        stops.append(f'<stop offset="{off:.3f}" stop-color="{col}"{op}/>')
    return (f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{a[0]:.2f}" '
            f'y1="{a[1]:.2f}" x2="{b[0]:.2f}" y2="{b[1]:.2f}">{"".join(stops)}</linearGradient>')


def folds(cells, ribbon):
    """Edges of a piece where it meets the rest of the ribbon.

    Yields (side, edge, (lo, hi)): side of the piece, the grid line of that
    edge, and the extent lo..hi along it, one per contiguous stretch.
    """
    outside = ribbon - cells
    found = defaultdict(set)  # (side, edge) -> positions along the edge
    for c, r in cells:
        for side, (dc, dr), edge, pos in (("left", (-1, 0), c, r), ("right", (1, 0), c + 1, r),
                                          ("top", (0, -1), r, c), ("bottom", (0, 1), r + 1, c)):
            if (c + dc, r + dr) in outside:
                found[(side, edge)].add(pos)
    for (side, edge), positions in sorted(found.items()):
        run = sorted(positions)
        start = prev = run[0]
        for p in run[1:] + [None]:
            if p != prev + 1 if p is not None else True:
                yield side, edge, (start, prev + 1)
                start = p
            prev = p if p is not None else prev


def strip_rect(side, edge, span, length, g):
    """Canvas rect (x0, y0, x1, y1) inside a piece along its fold edge."""
    step = {"left": 1, "right": -1, "top": 1, "bottom": -1}[side] * length
    lo, hi = span
    if side in ("left", "right"):
        x, y0, y1 = g.xs[edge], g.ys[lo], g.ys[hi]
        x0, x1 = sorted((x, x + step))
        return x0, y0, x1, y1
    y, x0, x1 = g.ys[edge], g.xs[lo], g.xs[hi]
    y0, y1 = sorted((y, y + step))
    return x0, y0, x1, y1


def fold_shadow(gid, side, edge, span, g, defs, clip, width=None):
    """A strip inside the piece along a fold, dark at the fold, fading inward.

    It covers only the extent of the ribbon cells touching that side.
    """
    w = (FOLD_SHADOW["width"] if width is None else width) * g.c
    x0, y0, x1, y1 = strip_rect(side, edge, span, w, g)
    if side in ("left", "right"):
        x = g.xs[edge]
        a, b = (x, y0), (x + (w if side == "left" else -w), y0)
    else:
        y = g.ys[edge]
        a, b = (x0, y), (x0, y + (w if side == "top" else -w))
    defs.append(gradient(gid, [BACK_TINT, BACK_TINT], a, b, [FOLD_SHADOW["opacity"], 0]))
    return (f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" '
            f'fill="url(#{gid})" clip-path="url(#{clip})"/>')


def visible_folds(pieces, ribbon_set):
    """(piece index, side, edge, span) for every fold not hidden by a later piece."""
    out = []
    for i, cells in enumerate(pieces):
        later = set().union(*pieces[i + 1:]) if i + 1 < len(pieces) else set()
        for side, edge, span in folds(cells, ribbon_set):
            lo, hi = span
            if side in ("left", "right"):
                col = edge if side == "left" else edge - 1
                strip = {(col, r) for r in range(lo, hi)}
            else:
                row = edge if side == "top" else edge - 1
                strip = {(c, row) for c in range(lo, hi)}
            if not strip <= later:
                out.append((i, side, edge, span))
    return out


def build_svg(canvas=CANVAS, padding=PADDING, flat=False) -> str:
    """The icon as SVG; flat=True gives the simplified variant for tiny sizes."""
    g = Grid(canvas, padding)
    tl, br = g.pt(0, 0), g.pt(N, N)
    if flat:
        fill = {"front": FLAT["front"], "back": FLAT["back"]}
        defs = []
    else:
        fill = {"front": "url(#front)", "back": "url(#back)"}
        defs = [
            gradient("front", FRONT, tl, br),
            gradient("back", [mix(c, BACK_TINT, BACK_DARKEN) for c in FRONT], tl, br),
        ]
    ribbon_set = ribbon_cells(RIBBON)
    ribbon = rounded_path(ribbon_set, g)
    body = [f'<path d="{ribbon}" fill="{fill["front"]}"/>']

    pieces = [cell_range(p["cells"]) for p in PIECES]
    fold_list = visible_folds(pieces, ribbon_set)
    for i, (piece, cells) in enumerate(zip(PIECES, pieces)):
        d = rounded_path(cells, g, piece.get("corners"))
        body.append(f'<path d="{d}" fill="{fill[piece.get("face", "back")]}"/>')
        if flat:
            continue
        defs.append(f'<clipPath id="piece{i}"><path d="{d}"/></clipPath>')
        x0, y0 = min(c for c, _ in cells), min(r for _, r in cells)
        x1, y1 = max(c for c, _ in cells) + 1, max(r for _, r in cells) + 1

        far = piece.get("depth")
        if far and DEPTH > 0:
            (ax, ay), (bx, by) = g.pt(x0, y0), g.pt(x1, y1)
            mx, my = (ax + bx) / 2, (ay + by) / 2
            near_pt, far_pt = {
                "left": ((bx, my), (ax, my)), "right": ((ax, my), (bx, my)),
                "top": ((mx, by), (mx, ay)), "bottom": ((mx, ay), (mx, by)),
            }[far]
            defs.append(gradient(f"depth{i}", [BACK_TINT, BACK_TINT], near_pt, far_pt, [0, DEPTH]))
            body.append(f'<path d="{d}" fill="url(#depth{i})"/>')

        if FOLD_SHADOW["opacity"] > 0:
            for j, (k, side, edge, span) in enumerate(f for f in fold_list if f[0] == i):
                body.append(fold_shadow(f"fold{i}-{j}", side, edge, span, g, defs, f"piece{i}",
                                        piece.get("shadow", {}).get(side)))

    if not flat and HIGHLIGHT > 0:
        defs.append(gradient("hl", ["#ffffff", "#ffffff"], tl, ((tl[0] + br[0]) / 2, (tl[1] + br[1]) / 2),
                             [HIGHLIGHT, 0]))
        body.append(f'<path d="{ribbon}" fill="url(#hl)"/>')

    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{canvas}" height="{canvas}" '
            f'viewBox="0 0 {canvas} {canvas}"><defs>{"".join(defs)}</defs>{"".join(body)}</svg>\n')


# ---------------------------------------------------------------- export ---


def rasterize(svg: str, size: int) -> bytes:
    import resvg_py
    return bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))


def image(svg: str, size: int):
    """Rendered from the vector at this size, not downscaled - small sizes stay crisp."""
    import io
    from PIL import Image
    return Image.open(io.BytesIO(rasterize(svg, size))).convert("RGBA")


def centred(im, box):
    """(image, position, mask) for Image.paste: im centred in a box of size box."""
    return im, ((box[0] - im.width) // 2, (box[1] - im.height) // 2), im


def main():
    from PIL import Image
    out = ROOT / "resources" / "icon"
    out.mkdir(parents=True, exist_ok=True)

    svg = build_svg()
    flat = build_svg(flat=True)
    (out / "levelscript.svg").write_text(svg)
    (out / "levelscript-flat.svg").write_text(flat)
    (out / "levelscript.png").write_bytes(rasterize(svg, 256))
    (out / "levelscript-macos.png").write_bytes(rasterize(build_svg(padding=MACOS_PADDING), 512))
    ico = [image(flat if s <= FLAT_MAX else svg, s) for s in ICO_SIZES]
    ico[-1].save(out / "levelscript.ico", format="ICO",
                 sizes=[im.size for im in ico], append_images=ico[:-1])

    ext = ROOT / "extension"
    (ext / "icon.png").write_bytes(rasterize(svg, 128))
    (ext / "file-icon.svg").write_text(flat)

    win = ROOT / "packaging" / "windows"
    for scale, suffix in ((1, ""), (2, "@2x")):
        w, h = WIZARD_SMALL[0] * scale, WIZARD_SMALL[1] * scale
        small = Image.new("RGB", (w, h), "white")
        small.paste(*centred(image(svg, WIZARD_SMALL_ICON * scale), (w, h)))
        small.save(win / f"wizard-small{suffix}.bmp")
        w, h = WIZARD_LARGE[0] * scale, WIZARD_LARGE[1] * scale
        large = Image.new("RGB", (w, h), WIZARD_LARGE_BG)
        large.paste(*centred(image(svg, w * 3 // 4), (w, h)))
        large.save(win / f"wizard-large{suffix}.bmp")

    mac = ROOT / "packaging" / "macos"
    for scale, suffix in ((1, ""), (2, "@2x")):
        w, h = PKG_BACKGROUND[0] * scale, PKG_BACKGROUND[1] * scale
        bg = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        icon = image(svg, PKG_ICON * scale)
        inset = PKG_INSET * scale
        bg.alpha_composite(icon, (inset, h - inset - icon.height))   # bottom-left
        bg.save(mac / f"background{suffix}.png")
    print(f"wrote {out}, {ext}, {win} and {mac}")


if __name__ == "__main__":
    main()