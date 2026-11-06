"""Headless preview of the glowing land lines and the flat-ground fade
(heightMapShader.frag, branch ss/land-lines, 2026-11-06).

Applies the shader's formulas (colour map fill or black, land lines in
"Band edges" or "Every N mm" mode, water colour, water lines, flat-ground
fade) to a synthetic box at 1 mm per pixel (taken as 1 projector px per mm):

Band edges mode outlines each band in its own colour (2026-11-07); every N
mm colours a line by the band at its level.

Band fill on: land lines are mixed over the fill (coverage = core + 0.35
halo) in a deepened colour ("Line shade on fill", 0.35); band fill off:
additive glow. Water lines are always additive. Panels: fill on | fill off.

- flat sand at +9 mm with +-1 mm noise per Kinect pixel (1.9 mm), linearly
  interpolated between pixels as the shader's mesh does;
- two mounds reaching 75 mm with 30-degree sides; a plant-cover patch
  (cover 0.8) on part of the left mound shows "Dim lines under plants";
- a shallow inflow lake along the bottom edge (bed -12 mm, water -4 mm,
  15-degree beach) and the basin below show the shoreline outlines: land side
  in the land pixel's band colour, water side in blue ("Shoreline glow");
- a dug basin, 30-degree walls, a nearly flat bed (-20 mm, tilted 1 deg)
  under shallow water (surface -14 mm, the water table + ~1 mm).

The flat-ground fade uses the slope fitted to 8 samples at +-5 Kinect
pixels (as the shader does from the filtered depth texture). The script prints speckle and
line-retention figures per fade setting (used to choose the default) and
writes a contact sheet.

Usage: python3 tools/land_lines_preview.py <out.png> [repoRoot]
Keep in sync with the shader (search "Glowing land lines" and "flat-ground").
"""
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

OUT = sys.argv[1]
ROOT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

W, H = 760, 460          # mm = px
KPX = 1.9                # mm per Kinect pixel (one water-grid cell)
NOISE_MM = 1.0
SAND, TABLE_SURFACE = 9.0, -14.0
LAKE_BED, LAKE_SURFACE, LAKE_Y = -12.0, -4.0, 400.0  # inflow lake along the bottom edge
TAN30 = np.tan(np.radians(30))
LINE_RGB = np.array([0x6F, 0xD3, 0xFF]) / 255.0
RANGE_MM, MAX_WATER_DEPTH_FRAC = 130.0, 0.05
rng = np.random.default_rng(3)


# ------------------------------------------------------------ colour map
def colour_keys():
    xml = open(os.path.join(ROOT, "bin/data/colorMaps/HeightColorMap.xml")).read()
    keys = []
    for k in re.findall(r"<key>(.*?)</key>", xml, re.S):
        g = lambda t: float(re.search(rf"<{t}>(.*?)</{t}>", k).group(1))
        keys.append((g("height"), np.array([g("color-r"), g("color-g"), g("color-b")]) / 255))
    return sorted(keys, key=lambda k: k[0])


KEYS = colour_keys()
# SandSurfaceRenderer::updateLandBandEdges(): a key whose colour differs
# from the key below it starts a band.
EDGES = [KEYS[i][0] for i in range(1, len(KEYS)) if not np.allclose(KEYS[i][1], KEYS[i - 1][1])][:16]
GAPS = [min([EDGES[i] - EDGES[i - 1]] if i > 0 else [1e3]) for i in range(len(EDGES))]
GAPS = [min(GAPS[i], EDGES[i + 1] - EDGES[i]) if i + 1 < len(EDGES) else GAPS[i] for i in range(len(EDGES))]


def colour_at(h):
    h = np.asarray(h, float)
    out = np.zeros(h.shape + (3,))
    out[:] = KEYS[0][1]
    for kh, c in KEYS:  # stepped: the last key at or below
        out[h >= kh] = c
    return out


# ------------------------------------------------------------ terrain
def terrain(x, y):
    t = np.full(np.broadcast(x, y).shape, SAND)
    for cx in (125.0, 365.0):  # mounds: cones to 75 mm, 30-degree sides
        r = np.hypot(x - cx, y - 180.0)
        t = np.maximum(t, 75.0 - r * TAN30)
    # basin: floor radius 60 mm at -20 (tilted 1 deg), 30-degree walls
    r = np.hypot(x - 620.0, y - 180.0)
    floor = -20.0 + np.tan(np.radians(1.0)) * (x - 620.0)
    wall = floor + np.maximum(0.0, r - 60.0) * TAN30
    t = np.where(wall < t, wall, t)
    # lake along the bottom edge: bed -12 mm, 15-degree beach up to the sand
    lake = LAKE_BED + np.maximum(0.0, LAKE_Y - y) * np.tan(np.radians(15.0))
    return np.where(lake < SAND, np.minimum(t, lake), t)


nx, ny = int(W / KPX) + 3, int(H / KPX) + 3
NOISE = rng.uniform(-NOISE_MM, NOISE_MM, (ny, nx))


def noise_at(x, y):
    """Per-Kinect-pixel noise, bilinear between pixels (the shader's mesh)."""
    gx, gy = np.clip(x / KPX, 0, nx - 1.001), np.clip(y / KPX, 0, ny - 1.001)
    i, j = np.floor(gy).astype(int), np.floor(gx).astype(int)
    fy, fx = gy - i, gx - j
    n = NOISE
    return (n[i, j] * (1 - fx) * (1 - fy) + n[i, j + 1] * fx * (1 - fy)
            + n[i + 1, j] * (1 - fx) * fy + n[i + 1, j + 1] * fx * fy)


def height(x, y):
    return terrain(x, y) + noise_at(x, y)


Y, X = np.mgrid[0:H, 0:W] + 0.5
BED = terrain(X, Y)
HMM = height(X, Y)


def fwidth(u):
    dx = np.abs(np.diff(u, axis=1, append=u[:, -1:]))
    dy = np.abs(np.diff(u, axis=0, append=u[-1:, :]))
    return dx + dy


HW = fwidth(HMM)
# Flat-fade slope: 8 samples at +-5 Kinect pixels (axes and diagonals),
# least-squares gradient, as the shader.
B = 5 * KPX
D = B / np.sqrt(2)
GX = (height(X + B, Y) - height(X - B, Y)) / (2 * B)
GY = (height(X, Y + B) - height(X, Y - B)) / (2 * B)
G1 = (height(X + D, Y + D) - height(X - D, Y - D)) / (2 * B)
G2 = (height(X + D, Y - D) - height(X - D, Y + D)) / (2 * B)
SLOPE = np.hypot(0.5 * (GX + (G1 + G2) / np.sqrt(2)), 0.5 * (GY + (G1 - G2) / np.sqrt(2)))

# water: per cell, as the texture (nearest), where the bed is below the surface
CX = (np.floor(X / KPX) + 0.5) * KPX
CY = (np.floor(Y / KPX) + 0.5) * KPX
SURFACE = np.where(CY > 300.0, LAKE_SURFACE, TABLE_SURFACE)
DEPTH = np.maximum(0.0, SURFACE - terrain(CX, CY))
WET = DEPTH > 0.0
# Water texture cells (one per Kinect pixel), for the shoreline searches.
NCX, NCY = int(np.ceil(W / KPX)), int(np.ceil(H / KPX))
_cc = lambda n: (np.arange(n) + 0.5) * KPX
_CY, _CX = np.meshgrid(_cc(NCY), _cc(NCX), indexing="ij")
WET_CELLS = (np.where(_CY > 300.0, LAKE_SURFACE, TABLE_SURFACE) - terrain(_CX, _CY)) > 0.0


def shore_distance_px(target_cells):
    """Per pixel: distance (px, 1 px/mm) to the nearest target texel within
    3 cells, from the pixel to the texel's edge (the shader's search)."""
    vx, vy = X / KPX, Y / KPX                 # vegUV
    bi, bj = np.floor(vy).astype(int), np.floor(vx).astype(int)
    dmin = np.full(X.shape, 1e3)
    for oy in range(-3, 4):
        for ox in range(-3, 4):
            ci, cj = np.clip(bi + oy, 0, NCY - 1), np.clip(bj + ox, 0, NCX - 1)
            hit = target_cells[ci, cj]
            ddx = np.maximum(np.abs(vx - (cj + 0.5)) - 0.5, 0)
            ddy = np.maximum(np.abs(vy - (ci + 0.5)) - 0.5, 0)
            dmin = np.where(hit, np.minimum(dmin, np.hypot(ddx, ddy)), dmin)
    return dmin * KPX


DIST_TO_WATER = shore_distance_px(WET_CELLS)   # land side
DIST_TO_DRY = shore_distance_px(~WET_CELLS)    # water side
COVER = np.where((np.abs(X - 95) < 30) & (np.abs(Y - 150) < 45), 0.8, 0.0)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


ADD_HALO, MIX_HALO = 0.6, 0.35  # halo weight: additive glow / coverage mixed over the fill


def deepen(c, k):
    """The shader's deepen(): darker by k, saturation raised by the same fraction."""
    luma = (c * np.array([0.299, 0.587, 0.114])).sum(-1, keepdims=True)
    return np.clip((luma + (c - luma) * (1 + k)) * (1 - k), 0, 1)


def glow(dpx, glow_px, halo_k=ADD_HALO):
    core = np.clip(1 - dpx, 0, 1)
    halo = halo_k * np.exp(-dpx / glow_px) if glow_px > 0.01 else 0.0
    return np.clip(core + halo, 0, 1)


def flat_fade(amount):
    if amount <= 0:
        return np.ones_like(SLOPE)
    s = np.tan(np.radians(15.0 * amount))
    return smoothstep(0.5 * s, s, SLOPE)


OUTLINE_PX = 1.5  # "Band outline width (px)" default


def land_light(mode, spacing=10.0, offset=5.0, glow_px=2.0, outline_px=OUTLINE_PX, halo_k=ADD_HALO):
    """(light 0..1, line colour RGB) per pixel, before brightness and fades."""
    ok = HW > 1e-6
    hw = np.maximum(HW, 1e-6)
    if mode == "edges":
        # Each band outlined in its own colour: every line pixel (core and
        # halo) takes the colour of the band the pixel itself is in. Core:
        # outline_px on each side of the boundary; halo from the core's edge.
        best = np.zeros_like(HMM)
        for e, g in zip(EDGES, GAPS):
            dpx = np.abs(HMM - e) / hw
            core = np.clip(outline_px + 0.5 - dpx, 0, 1)
            halo = halo_k * np.exp(-np.maximum(dpx - outline_px, 0) / glow_px) if glow_px > 0.01 else 0.0
            l = np.clip(core + halo, 0, 1) * (1 - smoothstep(0.25, 0.5, hw / max(g, 0.01)))
            best = np.maximum(best, l)
        colour = colour_at(HMM)
    else:
        u = (HMM - offset) / spacing
        k = np.floor(u + 0.5)
        uw = hw / spacing
        best = glow(np.abs(u - k) / uw, glow_px, halo_k) * (1 - smoothstep(0.25, 0.5, uw))
        colour = colour_at(offset + k * spacing + 0.5)  # the band at the line's level
    return best * ok, colour


def water_light(spacing=10.0, glow_px=2.0, halo_k=ADD_HALO):
    u = HMM / spacing  # sign of the elevation does not change the lines
    w = np.maximum(HW, 1e-6)
    f = u - np.floor(u)
    l = glow(np.minimum(f, 1 - f) / w, glow_px, halo_k) * (1 - smoothstep(0.25, 0.5, w)) * (HW > 1e-6)
    return l


def outline(dpx, w=OUTLINE_PX, glow_px=2.0, halo_k=ADD_HALO):
    core = np.clip(w + 0.5 - dpx, 0, 1)
    halo = halo_k * np.exp(-np.maximum(dpx - w, 0) / glow_px) if glow_px > 0.01 else 0.0
    return np.clip(core + halo, 0, 1)


def glow_water_side(dpx, glow_px=2.0, halo_k=ADD_HALO):
    # Existing water-side formula: ~1 px core from the edge plus halo.
    return np.clip(np.clip(1 - dpx, 0, 1) + (halo_k * np.exp(-dpx / glow_px) if glow_px > 0.01 else 0.0), 0, 1)


def render(mode, fill, fade_amount, brightness=0.8, dim=0.8, land_shore=True, water_shore=True,
           land_shade=0.35, additive_on_fill=False):
    """additive_on_fill=True reproduces the previous build (glow added over the fill)."""
    band = colour_at(HMM)  # this pixel's own band (before any soil tint)
    img = band.copy() if fill else np.zeros((H, W, 3))
    ff = flat_fade(fade_amount)
    gain = brightness * (1 - dim * COVER) * ff
    mix_mode = fill and not additive_on_fill
    if not mix_mode:  # band fill off: additive glow (unchanged)
        light, colour = land_light(mode)
        lit = colour * light[..., None]
        if land_shore:  # "Outline shoreline (land side)": own band colour
            lit = np.maximum(lit, band * outline(DIST_TO_WATER)[..., None])
        land = np.minimum(img + lit * gain[..., None], 1)
    else:  # band fill on: mix a deepened colour by coverage (core + softer halo)
        cov, colour = land_light(mode, halo_k=MIX_HALO)
        a = (cov * gain)[..., None]
        land = img * (1 - a) + deepen(colour, land_shade) * a
        if land_shore:
            a = (outline(DIST_TO_WATER, halo_k=MIX_HALO) * gain)[..., None]
            land = land * (1 - a) + deepen(band, land_shade) * a
    img = np.where(WET[..., None], img, land)
    # water (unchanged colours) + water lines with the flat fade
    t = np.clip(DEPTH / RANGE_MM / MAX_WATER_DEPTH_FRAC, 0, 1)
    lake = np.array([0.722, 0.690, 0.627])
    water = (1 - t)[..., None] * np.array([0.255, 0.714, 0.769]) + t[..., None] * np.array([0.031, 0.188, 0.420])
    a = (0.6 + 0.35 * t)[..., None]
    wcol = lake * (1 - a) + water * a
    # Water lines and water-side shore: additive glow whatever the band fill.
    wlight = water_light() * ff
    if water_shore:  # "Shoreline glow" (water side), not flat-faded, as the shader
        wlight = np.maximum(wlight, glow_water_side(DIST_TO_DRY))
    wcol = np.minimum(wcol + LINE_RGB * (0.8 * wlight)[..., None], 1)
    img = np.where(WET[..., None], wcol, img)
    return (img * 255 + 0.5).astype(np.uint8)


# ------------------------------------------------------------ measurements
mound_r = np.minimum(np.hypot(X - 125, Y - 180), np.hypot(X - 365, Y - 180))
FLAT = (mound_r > 135) & (np.hypot(X - 620, Y - 180) > 130) & (X > 10) & (X < W - 10) & (Y > 10) & (Y < 290)
SLOPED = (mound_r * TAN30 > 8) & (mound_r * TAN30 < 60) & (COVER == 0)  # 30-degree mound sides
FLATBED = WET & (np.hypot(X - 620, Y - 180) < 50)


def stats(amount):
    ff = flat_fade(amount)
    out = {}
    for mode in ("edges", "everyN"):
        l, _ = land_light(mode)
        lit = l * ff * 0.8
        out[mode] = (100 * np.mean(lit[FLAT] > 0.05), np.mean(lit[SLOPED]))
    wl = water_light() * ff * 0.8
    out["water"] = 100 * np.mean(wl[FLATBED] > 0.05)
    return out


base = stats(0.0)
print("fade  flat sand lit>0.05 (%)    flat-bed water lit (%)   30-deg slope line light kept (%)")
print("      band-edges  every-N                               band-edges  every-N")
rows = []
for amount in (0.0, 0.2, 0.4, 0.5, 0.6, 0.67, 0.7, 0.8, 1.0):
    s = stats(amount)
    keep_e = 100 * s["edges"][1] / max(base["edges"][1], 1e-9)
    keep_n = 100 * s["everyN"][1] / max(base["everyN"][1], 1e-9)
    rows.append((amount, s, keep_e, keep_n))
    print(f"{amount:4.1f}  {s['edges'][0]:9.2f}  {s['everyN'][0]:8.2f}        {s['water']:9.2f}               "
          f"{keep_e:9.1f}  {keep_n:8.1f}   (threshold {15 * amount:4.1f} deg, off below {7.5 * amount:4.1f})")

DEFAULT = 0.6
panels = [
    (f"Band edges, fill ON (mixed, shade 0.35), flat fade {DEFAULT}", render("edges", True, DEFAULT)),
    (f"Band edges, fill OFF (additive), flat fade {DEFAULT}", render("edges", False, DEFAULT)),
    (f"Every 10 mm (+5), fill ON (mixed, shade 0.35), flat fade {DEFAULT}", render("everyN", True, DEFAULT)),
    (f"Every 10 mm (+5), fill OFF (additive), flat fade {DEFAULT}", render("everyN", False, DEFAULT)),
    ("Band edges, fill ON, previous build (additive over fill)", render("edges", True, DEFAULT, additive_on_fill=True)),
    ("Band edges, fill ON (mixed), shade 0 (no deepening)", render("edges", True, DEFAULT, land_shade=0.0)),
]
gap, head = 8, 18
sheet = Image.new("RGB", (2 * W + 3 * gap, 3 * (H + head) + 4 * gap), "white")
d = ImageDraw.Draw(sheet)
for k, (title, im) in enumerate(panels):
    x0 = gap + (k % 2) * (W + gap)
    y0 = gap + (k // 2) * (H + head + gap)
    d.text((x0, y0 + 3), title, fill=(30, 30, 30))
    sheet.paste(Image.fromarray(im), (x0, y0 + head))
sheet.save(OUT)
print("band edges (mm):", [round(e, 2) for e in EDGES])
print("wrote", OUT, "(1 mm per pixel; each panel 760 x 460 mm; plant cover 0.8 in the box on the left mound;"
      " shoreline outlines on unless stated)")
