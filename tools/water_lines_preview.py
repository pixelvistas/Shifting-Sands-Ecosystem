"""Headless preview of the water-lines display (heightMapShader.frag, 2026-11-05).

Applies the shader's water colour, water-line and shoreline-glow formulas to a
synthetic basin and writes a PNG at 1 mm per pixel (taken as 1 projector pixel
per mm), so line spacing and glow width can be judged before a sandbox test.

Basin: flat sand at +9 mm (resting level); a bowl dug 30 mm deep (floor at
-21 mm, floor radius 40 mm) with 30-degree walls back up to the sand.
Panels:
  A  water at the water table (surface -14 mm: table -15, basins settle ~1 mm
     above it), spacing 10 mm, glow 2 px
  B  bowl filled to +5 mm, spacing 10 mm, glow 2 px
  C  bowl filled to +5 mm, spacing 5 mm, glow 2 px
  D  as B with glow 4 px and "Shoreline glow" on

Keep the formulas in sync with the shader (search "Water lines" there).
Usage: python3 tools/water_lines_preview.py <out.png> [repoRoot]
"""
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

OUT = sys.argv[1]
ROOT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..")

SIZE = 240            # mm (= px)
MM_PER_CELL = 1.9     # water-grid cell (one Kinect pixel)
SAND_MM, FLOOR_MM, FLOOR_R = 9.0, -21.0, 40.0
SLOPE_DEG = 30.0
RANGE_MM = 130.0      # colour-map range (-45..85 mm)
MAX_WATER_DEPTH_FRAC = 0.05
LINE_RGB = np.array([0x6F, 0xD3, 0xFF]) / 255.0


def colour_map():
    xml = open(os.path.join(ROOT, "bin/data/colorMaps/HeightColorMap.xml")).read()
    keys = []
    for k in re.findall(r"<key>(.*?)</key>", xml, re.S):
        g = lambda t: float(re.search(rf"<{t}>(.*?)</{t}>", k).group(1))
        keys.append((g("height"), g("color-r") / 255, g("color-g") / 255, g("color-b") / 255))
    return sorted(keys)


def land_colour(h, keys):
    out = np.zeros(h.shape + (3,))
    out[:] = keys[0][1:]
    for k in keys:  # stepped map: the last key at or below the height
        out[h >= k[0]] = k[1:]
    return out


def bed():
    y, x = np.mgrid[0:SIZE, 0:SIZE] + 0.5
    r = np.hypot(x - SIZE / 2, y - SIZE / 2)
    wall = FLOOR_MM + (r - FLOOR_R) * np.tan(np.radians(SLOPE_DEG))
    return np.clip(wall, FLOOR_MM, SAND_MM)


def fwidth(u):
    # GPU-style: |dFdx| + |dFdy|, forward differences per pixel.
    dx = np.abs(np.diff(u, axis=1, append=u[:, -1:]))
    dy = np.abs(np.diff(u, axis=0, append=u[-1:, :]))
    return dx + dy


def render(surface_mm, spacing, glow_px, brightness=0.8, shoreline=False):
    keys = colour_map()
    h = bed()
    img = land_colour(h, keys)
    # Water grid (nearest-filtered texture): one value per 1.9 mm cell.
    n = int(np.ceil(SIZE / MM_PER_CELL))
    cc = (np.arange(n) + 0.5) * MM_PER_CELL
    cy, cx = np.meshgrid(cc, cc, indexing="ij")
    rc = np.hypot(cx - SIZE / 2, cy - SIZE / 2)
    bed_c = np.clip(FLOOR_MM + (rc - FLOOR_R) * np.tan(np.radians(SLOPE_DEG)), FLOOR_MM, SAND_MM)
    depth_c = np.maximum(0.0, surface_mm - bed_c)
    wet_c = depth_c > 0.0
    py = np.arange(SIZE) + 0.5
    gi = np.minimum(n - 1, (py / MM_PER_CELL).astype(int))
    GI, GJ = np.meshgrid(gi, gi, indexing="ij")
    wet = wet_c[GI, GJ]
    t = np.clip(depth_c[GI, GJ] / RANGE_MM / MAX_WATER_DEPTH_FRAC, 0, 1)

    # Water colour (unchanged shader formula): lakebed, then shallow/deep mix.
    lake = np.array([0.722, 0.690, 0.627])
    water = (1 - t)[..., None] * np.array([0.255, 0.714, 0.769]) + t[..., None] * np.array([0.031, 0.188, 0.420])
    a = (0.6 + (0.95 - 0.6) * t)[..., None]
    img = np.where(wet[..., None], lake * (1 - a) + water * a, img)

    # Water lines: u = elevation / spacing.
    u = h / spacing
    w = fwidth(u)
    f = u - np.floor(u)
    with np.errstate(divide="ignore", invalid="ignore"):
        dpx = np.where(w > 1e-6, np.minimum(f, 1 - f) / np.maximum(w, 1e-6), 1e9)
    core = np.clip(1 - dpx, 0, 1)
    halo = 0.6 * np.exp(-dpx / glow_px) if glow_px > 0.01 else 0.0
    fade = 1 - smoothstep(0.25, 0.5, w)
    light = np.clip(core + halo, 0, 1) * fade * (w > 1e-6)

    if shoreline:
        # Distance (cells) to the nearest dry texel within 3 cells.
        vx = np.broadcast_to(py / MM_PER_CELL, (SIZE, SIZE))   # vegUV.x
        vy = vx.T
        bx, by = np.floor(vx) + 0.5, np.floor(vy) + 0.5
        dmin = np.full((SIZE, SIZE), 1e3)
        for oy in range(-3, 4):
            for ox in range(-3, 4):
                ci = np.clip(GI + oy, 0, n - 1)
                cj = np.clip(GJ + ox, 0, n - 1)
                dry = ~wet_c[ci, cj]
                ddx = np.maximum(np.abs(vx - (bx + ox)) - 0.5, 0)
                ddy = np.maximum(np.abs(vy - (by + oy)) - 0.5, 0)
                dmin = np.where(dry, np.minimum(dmin, np.hypot(ddx, ddy)), dmin)
        sp = dmin * MM_PER_CELL  # 1 px per mm
        s_core = np.clip(1 - sp, 0, 1)
        s_halo = 0.6 * np.exp(-sp / glow_px) if glow_px > 0.01 else 0.0
        light = np.maximum(light, np.clip(s_core + s_halo, 0, 1))

    lit = np.minimum(img + LINE_RGB * (brightness * light)[..., None], 1.0)
    img = np.where(wet[..., None], lit, img)  # never on land
    return (img * 255 + 0.5).astype(np.uint8)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


panels = [
    ("A  water at table (-14 mm), 10 mm, glow 2", render(-14.0, 10.0, 2.0)),
    ("B  filled to +5 mm, 10 mm, glow 2", render(5.0, 10.0, 2.0)),
    ("C  filled to +5 mm, 5 mm, glow 2", render(5.0, 5.0, 2.0)),
    ("D  as B, glow 4, shoreline glow", render(5.0, 10.0, 4.0, shoreline=True)),
]
gap, head = 8, 18
sheet = Image.new("RGB", (2 * SIZE + 3 * gap, 2 * (SIZE + head) + 3 * gap), "white")
d = ImageDraw.Draw(sheet)
for k, (title, im) in enumerate(panels):
    x0 = gap + (k % 2) * (SIZE + gap)
    y0 = gap + (k // 2) * (SIZE + head + gap)
    d.text((x0, y0 + 3), title, fill=(30, 30, 30))
    sheet.paste(Image.fromarray(im), (x0, y0 + head))
sheet.save(OUT)
print("wrote", OUT, "(1 mm per pixel; each panel 240 x 240 mm)")
