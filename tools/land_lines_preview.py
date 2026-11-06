"""Headless preview of the glowing land lines and the flat-ground fade
(heightMapShader.frag, branch ss/land-lines, 2026-11-06).

Applies the shader's formulas (colour map fill or black, land lines in
"Band edges" or "Every N mm" mode, water colour, water lines, flat-ground
fade) to a synthetic box at 1 mm per pixel (taken as 1 projector px per mm):

- flat sand at +9 mm with +-1 mm noise per Kinect pixel (1.9 mm), linearly
  interpolated between pixels as the shader's mesh does;
- two mounds reaching 75 mm with 30-degree sides; a plant-cover patch
  (cover 0.8) on part of the left mound shows "Dim lines under plants";
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

W, H = 760, 360          # mm = px
KPX = 1.9                # mm per Kinect pixel (one water-grid cell)
NOISE_MM = 1.0
SAND, TABLE_SURFACE = 9.0, -14.0
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
    return np.where(wall < t, wall, t)


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
DEPTH = np.maximum(0.0, TABLE_SURFACE - terrain(CX, CY))
WET = DEPTH > 0.0
COVER = np.where((np.abs(X - 95) < 30) & (np.abs(Y - 150) < 45), 0.8, 0.0)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def glow(dpx, glow_px):
    core = np.clip(1 - dpx, 0, 1)
    halo = 0.6 * np.exp(-dpx / glow_px) if glow_px > 0.01 else 0.0
    return np.clip(core + halo, 0, 1)


def flat_fade(amount):
    if amount <= 0:
        return np.ones_like(SLOPE)
    s = np.tan(np.radians(15.0 * amount))
    return smoothstep(0.5 * s, s, SLOPE)


def land_light(mode, spacing=10.0, offset=5.0, glow_px=2.0):
    """(light 0..1, level mm) per pixel, before brightness and fades."""
    ok = HW > 1e-6
    hw = np.maximum(HW, 1e-6)
    if mode == "edges":
        best = np.zeros_like(HMM)
        level = np.zeros_like(HMM)
        for e, g in zip(EDGES, GAPS):
            l = glow(np.abs(HMM - e) / hw, glow_px) * (1 - smoothstep(0.25, 0.5, hw / max(g, 0.01)))
            take = l > best
            best = np.where(take, l, best)
            level = np.where(take, e, level)
    else:
        u = (HMM - offset) / spacing
        k = np.floor(u + 0.5)
        uw = hw / spacing
        best = glow(np.abs(u - k) / uw, glow_px) * (1 - smoothstep(0.25, 0.5, uw))
        level = offset + k * spacing
    return best * ok, level


def water_light(spacing=10.0, glow_px=2.0):
    u = HMM / spacing  # sign of the elevation does not change the lines
    w = np.maximum(HW, 1e-6)
    f = u - np.floor(u)
    l = glow(np.minimum(f, 1 - f) / w, glow_px) * (1 - smoothstep(0.25, 0.5, w)) * (HW > 1e-6)
    return l


def render(mode, fill, fade_amount, brightness=0.8, dim=0.8):
    img = colour_at(BED + noise_at(X, Y)) if fill else np.zeros((H, W, 3))
    ff = flat_fade(fade_amount)
    light, level = land_light(mode)
    gain = brightness * (1 - dim * COVER) * ff
    land = np.minimum(img + colour_at(level + 0.5) * (gain * light)[..., None], 1)
    img = np.where(WET[..., None], img, land)
    # water (unchanged colours) + water lines with the flat fade
    t = np.clip(DEPTH / RANGE_MM / MAX_WATER_DEPTH_FRAC, 0, 1)
    lake = np.array([0.722, 0.690, 0.627])
    water = (1 - t)[..., None] * np.array([0.255, 0.714, 0.769]) + t[..., None] * np.array([0.031, 0.188, 0.420])
    a = (0.6 + 0.35 * t)[..., None]
    wcol = lake * (1 - a) + water * a
    wcol = np.minimum(wcol + LINE_RGB * (0.8 * water_light() * ff)[..., None], 1)
    img = np.where(WET[..., None], wcol, img)
    return (img * 255 + 0.5).astype(np.uint8)


# ------------------------------------------------------------ measurements
mound_r = np.minimum(np.hypot(X - 125, Y - 180), np.hypot(X - 365, Y - 180))
FLAT = (mound_r > 135) & (np.hypot(X - 620, Y - 180) > 130) & (X > 10) & (X < W - 10) & (Y > 10) & (Y < H - 10)
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
    (f"Band edges, fill ON, flat fade {DEFAULT}", render("edges", True, DEFAULT)),
    (f"Band edges, fill OFF, flat fade {DEFAULT}", render("edges", False, DEFAULT)),
    (f"Every 10 mm (+5), fill ON, flat fade {DEFAULT}", render("everyN", True, DEFAULT)),
    (f"Every 10 mm (+5), fill OFF, flat fade {DEFAULT}", render("everyN", False, DEFAULT)),
    ("Band edges, fill OFF, flat fade OFF (0)", render("edges", False, 0.0)),
    ("Every 10 mm (+5), fill OFF, flat fade OFF (0)", render("everyN", False, 0.0)),
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
print("wrote", OUT, "(1 mm per pixel; each panel 760 x 360 mm; plant cover 0.8 in the box on the left mound)")
