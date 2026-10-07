#!/usr/bin/env python3
"""LevelScript icon generator.

A single ribbon on a uniform 5x5 grid: the L's stem runs down, the shared
bottom bar runs right, and the ribbon continues up and around into the S.
The ribbon (front face) is drawn as one rounded shape. Pieces are drawn on
top: mostly flaps (stretches showing the back face), each a set of cells with
its own corner radii. Every fold, where a piece meets the rest of the ribbon,
gets the same fold shadow; pieces can also darken toward their deep end.

Usage:
    python levelscript_icon.py

Writes the SVG master, a 512 px preview and every size in EXPORT_SIZES to
OUT_DIR (relative to this script, so it works from any working directory).
"""
from __future__ import annotations

import pathlib
from collections import defaultdict

# ---------------------------------------------------------------- config ---

OUT_DIR = "resources"  # relative to this script

N = 5  # grid is N x N uniform cells; A1 = top-left

# Ribbon path as waypoints; consecutive waypoints share a row or a column.
RIBBON = "A1 A5 E5 E3 C3 C1 E1"

# Corner radii of the ribbon outline, in cells (0.5 = semicircular band end).
# Where two corners share an edge too short for both, a piece's explicit
# override wins first, then the icon-square corner, and the other corner gets
# the remainder (so the band ends at A1 and E1 become
# asymmetric: 0.8 on the outside, 0.2 on the inside). Between two corners of
# the same kind, the smaller is kept and the larger gets the remainder.
RADII = dict(
    outer=0.8,    # convex corners on the four corners of the icon square
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
    # shared bottom bar: deep at D5 where it comes out from the S-bottom fold,
    # rising toward A5. It bends through the corner A5 and continues into A4,
    # where it is mostly hidden by the stem; only its corner shows there.
    dict(cells="A4:A5 B5:D5", corners={(0, 3): 0, (1, 3): 0, (4, 4): 0, (4, 5): 0.5},
         depth="right"),
    # L stem on the front face: deep at A4 where it folds out from under the
    # bar, rising toward A1; rounded at the bottom-left of A4
    dict(cells="A1:A4", face="front", corners={(0, 4): 0.5, (1, 4): 0}, depth="bottom"),
    # middle of the S; folds from column E (right) and into column C (left)
    dict(cells="D3", corners={(3, 2): 0, (4, 2): 0.5, (4, 3): 0, (3, 3): 0.5}),
    # top band; folds out of column C (left)
    dict(cells="D1:E1", corners={(3, 0): 0.5, (3, 1): 0}),
]

# Colours. Both faces share one gradient axis (top-left to bottom-right) so
# the ribbon reads as a single material; the back face is the front mixed
# toward BACK_TINT (a deep tint keeps it saturated, unlike mixing with black).
FRONT = ["#14DC85", "#00DBD4", "#D6DF30"]
BACK_TINT = "#2F7000"
BACK_DARKEN = 0.28     # 0 = same as front, 1 = BACK_TINT
FOLD_SHADOW = dict(
    opacity=0.55,      # darkness at the fold, fading to 0
    width=1.2,         # how far it reaches into the piece, in cells
)
DEPTH = 0.45           # darkness at a piece's far end (see PIECES "depth")
HIGHLIGHT = 0.22       # white sheen from the top-left; 0 disables

PADDING = 0.0          # fraction of the canvas left empty on each side
CANVAS = 1024
EXPORT_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 180, 192, 256, 512, 1024]

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


class Grid:
    def __init__(self, canvas=CANVAS, padding=PADDING):
        self.size = canvas * (1 - 2 * padding)
        self.o = canvas * padding
        self.c = self.size / N

    def pt(self, gx, gy):
        return self.o + gx * self.c, self.o + gy * self.c


def rounded_path(cells, g: Grid, override=None) -> str:
    """SVG path of a cell set with rounded corners.

    override maps a grid point to a radius in cells; other points use RADII.
    """
    override = override or {}
    icon_corners = {(0, 0), (N, 0), (0, N), (N, N)}
    parts = []
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
        rad = lim

        def unit(p, q):
            dx, dy = q[0] - p[0], q[1] - p[1]
            m = abs(dx) + abs(dy)
            return dx / m, dy / m

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
    """Sides of a piece where it meets the rest of the ribbon.

    Yields (side, edge, (lo, hi)) for each of left/right/top/bottom where ribbon
    cells outside the piece touch it: edge is the grid coordinate of that
    side, lo..hi the extent along it covered by the touching cells.
    """
    x0, y0 = min(c for c, _ in cells), min(r for _, r in cells)
    x1, y1 = max(c for c, _ in cells) + 1, max(r for _, r in cells) + 1
    outside = ribbon - cells
    checks = {
        "left": ({(x0 - 1, r) for r in range(y0, y1)}, x0),
        "right": ({(x1, r) for r in range(y0, y1)}, x1),
        "top": ({(c, y0 - 1) for c in range(x0, x1)}, y0),
        "bottom": ({(c, y1) for c in range(x0, x1)}, y1),
    }
    for side, (neighbours, edge) in checks.items():
        touching = neighbours & outside
        if touching:
            along = [r for _, r in touching] if side in ("left", "right") else [c for c, _ in touching]
            yield side, edge, (min(along), max(along) + 1)


def fold_shadow(gid, side, edge, span, g, defs, clip, width=None):
    """A strip inside the piece along a fold, dark at the fold, fading inward.

    It covers only the extent of the ribbon cells touching that side.
    """
    w = FOLD_SHADOW["width"] if width is None else width
    step = {"left": 1, "right": -1, "top": 1, "bottom": -1}[side] * w
    lo, hi = span
    a0, a1 = sorted((edge, edge + step))
    if side in ("left", "right"):
        rect, a, b = (a0, lo, a1, hi), g.pt(edge, lo), g.pt(edge + step, lo)
    else:
        rect, a, b = (lo, a0, hi, a1), g.pt(lo, edge), g.pt(lo, edge + step)
    defs.append(gradient(gid, [BACK_TINT, BACK_TINT], a, b, [FOLD_SHADOW["opacity"], 0]))
    (rx0, ry0), (rx1, ry1) = g.pt(rect[0], rect[1]), g.pt(rect[2], rect[3])
    return (f'<rect x="{rx0:.2f}" y="{ry0:.2f}" width="{rx1 - rx0:.2f}" height="{ry1 - ry0:.2f}" '
            f'fill="url(#{gid})" clip-path="url(#{clip})"/>')


def build_svg(canvas=CANVAS, padding=PADDING) -> str:
    g = Grid(canvas, padding)
    tl, br = g.pt(0, 0), g.pt(N, N)
    defs = [
        gradient("front", FRONT, tl, br),
        gradient("back", [mix(c, BACK_TINT, BACK_DARKEN) for c in FRONT], tl, br),
    ]
    ribbon_set = ribbon_cells(RIBBON)
    ribbon = rounded_path(ribbon_set, g)
    body = [f'<path d="{ribbon}" fill="url(#front)"/>']

    pieces = [cell_range(p["cells"]) for p in PIECES]
    for i, (piece, cells) in enumerate(zip(PIECES, pieces)):
        d = rounded_path(cells, g, piece.get("corners"))
        x0, y0 = min(c for c, _ in cells), min(r for _, r in cells)
        x1, y1 = max(c for c, _ in cells) + 1, max(r for _, r in cells) + 1
        body.append(f'<path d="{d}" fill="url(#{piece.get("face", "back")})"/>')
        defs.append(f'<clipPath id="piece{i}"><path d="{d}"/></clipPath>')

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
            later = set().union(*pieces[i + 1:]) if i + 1 < len(pieces) else set()
            for j, (side, edge, span) in enumerate(folds(cells, ribbon_set)):
                lo, hi = span
                if side in ("left", "right"):
                    col = edge if side == "left" else edge - 1
                    strip = {(col, r) for r in range(lo, hi)}
                else:
                    row = edge if side == "top" else edge - 1
                    strip = {(c, row) for c in range(lo, hi)}
                if strip <= later:  # this fold is hidden under a later piece
                    continue
                body.append(fold_shadow(f"fold{i}-{j}", side, edge, span, g, defs, f"piece{i}",
                                        piece.get("shadow", {}).get(side)))
    if HIGHLIGHT > 0:
        defs.append(gradient("hl", ["#ffffff", "#ffffff"], tl, ((tl[0] + br[0]) / 2, (tl[1] + br[1]) / 2),
                             [HIGHLIGHT, 0]))
        body.append(f'<path d="{ribbon}" fill="url(#hl)"/>')

    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{canvas}" height="{canvas}" '
            f'viewBox="0 0 {canvas} {canvas}"><defs>{"".join(defs)}</defs>{"".join(body)}</svg>\n')


# ---------------------------------------------------------------- export ---


def rasterize(svg: str, size: int) -> bytes:
    import resvg_py
    return bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))


def main():
    out = pathlib.Path(__file__).resolve().parent / OUT_DIR
    (out / "png").mkdir(parents=True, exist_ok=True)

    svg = build_svg()
    (out / "levelscript.svg").write_text(svg)
    (out / "levelscript-preview.png").write_bytes(rasterize(svg, 512))
    for s in EXPORT_SIZES:
        (out / "png" / f"levelscript-{s}.png").write_bytes(rasterize(svg, s))
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
