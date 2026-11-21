#!/usr/bin/env python3
"""Headless preview and checks for the Sandbox ASCII filter (AsciiFilter),
branch ss/ascii-baseline.

The Sandbox frame comes from ../sandbox_dither_preview (the app's own
height-map shader through a stand-in calibration, on a synthetic box: flat
sand, a mound through every land band, a basin under simulated water, and a
grid of pioneer / intermediate / climax patches). With the ASCII filter on,
the height shader is drawn twice, as SandSurfaceRenderer does: the colour
(plantDisplay 3, Sandbox lines off) and the parts (partsPass 1: band, layer,
stage). Marks are stand-ins for the three layers, each as the FBO the app
hands over: seeds (premultiplied RGBA), Dewitz particles (additive RGB) and
a few critters (straight alpha). Then bin/data/shaders/ascii (GL3 or GL2)
runs with the passes, FBO formats and filtering of src/Common/AsciiFilter.cpp
and its defaults (parsed from the .cpp). The font atlas is baked with Pillow
from bin/data/fonts/JetBrainsMono-Regular.ttf with the same fitting rule as
AsciiFilter::ensureAtlas(); FreeType through openFrameworks will differ by
a pixel here and there.

Motion: a "hand" (a 75 mm bump, 10 cells wide) crosses the flat sand at 6
cells per frame over 8 frames, while the particles and critters move, so
trails can be seen.

    python3 sandbox_ascii_preview.py frames    # out/: every cell preset, trails and glow on/off, crops
    python3 sandbox_ascii_preview.py checks    # exact black, Off/Dither identity, GL2 = GL3, parts
    python3 sandbox_ascii_preview.py timing    # per cell preset, this software GL (not the installation PC)

Needs numpy, Pillow, PyOpenGL and an EGL driver (Mesa: surfaceless), as
../sandbox_dither_preview; `checks` needs git (reads origin/ss/dither-baseline).
"""

import argparse
import math
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "sandbox_dither_preview"))
import sandbox_dither_preview as sdp  # noqa: E402  (EGL environment is set there)

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402
from OpenGL import GL  # noqa: E402

REPO = sdp.REPO
OUT = os.path.join(HERE, "out")
PW, PH = sdp.PW, sdp.PH
R = sdp.R
FONT = os.path.join(REPO, "bin", "data", "fonts", "JetBrainsMono-Regular.ttf")
SRC = os.path.join(REPO, "src", "Common", "AsciiFilter.cpp")
PRESET_NAMES = ["6x10", "7x10", "8x12", "10x16"]


def mark_glyph(path, cls):
    m = re.search(r"char %s::MARK_GLYPH = '(.)';" % cls, open(os.path.join(REPO, "src", "Games", path)).read())
    return m.group(1)


# The layers' own glyph choices (SeedLayer, WaterFlowField, CritterController).
GLYPHS = {"seed": mark_glyph("SeedLayer.cpp", "SeedLayer"), "particle": mark_glyph("WaterFlowField.cpp", "WaterFlowField"),
          "critter": mark_glyph("CritterController.cpp", "CCritterController")}

# The Sandbox with the ASCII filter on (SandSurfaceRenderer::drawSandboxPass).
LINES_OFF = {"drawLandLines": False, "drawWaterLines": False, "drawShorelineGlow": False, "drawContourLines": False}
ASCII_COLOUR = dict(plantDisplay=3, contourSolid=0, **LINES_OFF)
ASCII_PARTS = dict(ASCII_COLOUR, partsPass=1)


# ------------------------------------------------------------ the filter's own constants
def ascii_defaults():
    s = open(SRC).read()
    body = s[s.index("static AsciiFilter::Params makeDefaults()"):s.index("AsciiFilter::DEFAULTS = makeDefaults")]
    enums = {"CELLS_6x10": 0, "CELLS_7x10": 1, "CELLS_8x12": 2, "CELLS_10x16": 3,
             "GLYPH_CENTRE_LINE_BOX": 0, "GLYPH_CENTRE_INK_BOX": 1, "FONT_REFERENCE_ORDER": 0, "FONT_BUNDLED": 1}
    d = {}
    for n, idx, v in re.findall(r"p\.(\w+)(?:\[(\d)\])? = ([^;]+);", body):
        v = v.strip()
        key = n + idx
        if v in ("true", "false"):
            d[key] = v == "true"
        elif v.startswith("AsciiFilter::"):
            d[key] = enums[v.split("::")[1]]
        else:
            d[key] = float(v.rstrip("f"))
    return d


def cell_sizes():
    s = open(SRC).read()
    w = [int(x) for x in re.search(r"CELL_W\[AsciiFilter::CELL_PRESETS\] = \{([^}]*)\}", s).group(1).split(",")]
    h = [int(x) for x in re.search(r"CELL_H\[AsciiFilter::CELL_PRESETS\] = \{([^}]*)\}", s).group(1).split(",")]
    return list(zip(w, h))


def pools():
    s = open(SRC).read()
    m = re.search(r"POOLS\[4\] = \{(.*?)\};", s).group(1)   # the pools themselves contain braces
    return re.findall(r'"([^"]*)"', m)


# ------------------------------------------------------------ font atlas (AsciiFilter::ensureAtlas)
def cround(x):
    return math.copysign(math.floor(abs(x) + 0.5), x)


def measure(f):
    top = bottom = 0.0
    for c in range(33, 127):
        b = f.getbbox(chr(c), anchor="ls")
        top = min(top, b[1])
        bottom = max(bottom, b[3])
    return top, bottom, f.getlength("M")


def font_px(prm, ch):
    """AsciiFilter::fontPxFor(): 0 = auto, 1.1 x the cell height (the reference: 11 px in 10 px cells)."""
    return int(prm["fontPx"]) if prm.get("fontPx", 0) > 0 else int(math.floor(1.1 * ch + 0.5))


def bake_atlas(cw, ch, px, centring):
    """AsciiFilter::ensureAtlas(). The font here is always the bundled one: the
    reference order (Consolas, Menlo) finds neither on Linux. Slots carry a
    margin of ch px all round; only the cell inside is read."""
    if centring == 1:   # ink box (before 2026-11-21)
        ref = 64
        top, bottom, adv = measure(ImageFont.truetype(FONT, ref))
        if adv < 1:
            adv = 0.6 * ref
        size = max(4, int(math.floor(ref * min(cw / adv, ch / max(1.0, bottom - top)))))
    else:
        size = max(4, px)
    f = ImageFont.truetype(FONT, size)
    top, bottom, adv = measure(f)
    if centring == 1:
        baseline = cround(0.5 * (ch - (bottom - top)) - top)
        xoff = cround(0.5 * (cw - adv))
    else:               # line box (the reference)
        ascent, descent = f.getmetrics()
        asc, line_h = cround(ascent), cround(ascent + descent)
        baseline = math.floor((ch - line_h) / 2.0) + asc
        xoff = math.floor((cw - cround(adv)) / 2.0)
    pad = ch
    sw, sh = cw + 2 * pad, ch + 2 * pad
    img = Image.new("L", (16 * sw, 6 * sh), 0)
    d = ImageDraw.Draw(img)
    for c in range(33, 127):
        slot = c - 32
        d.text(((slot % 16) * sw + pad + xoff, (slot // 16) * sh + pad + baseline), chr(c), font=f, fill=255, anchor="ls")
    a = np.zeros((6 * sh, 16 * sw, 4), np.uint8)
    a[..., 0] = np.asarray(img)
    return a, size, pad


def atlas_cells(a, cw, ch, pad):
    """The atlas as the glyph pass sees it: each slot's cell only (16 x 6 cells)."""
    sw, sh = cw + 2 * pad, ch + 2 * pad
    out = np.zeros((6 * ch, 16 * cw), np.uint8)
    for r in range(6):
        for c in range(16):
            out[r * ch:(r + 1) * ch, c * cw:(c + 1) * cw] = a[r * sh + pad:r * sh + pad + ch, c * sw + pad:c * sw + pad + cw, 0]
    return out


# ------------------------------------------------------------ GL: the filter
class Fbo:
    """RGBA target (8-bit or 16F), nearest or linear, rectangle texture."""
    def __init__(self, w, h, internal, linear=False):
        self.w, self.h = w, h
        typ = GL.GL_UNSIGNED_BYTE if internal == GL.GL_RGBA8 else GL.GL_FLOAT
        self.tex = sdp.rect_tex(w, h, internal, GL.GL_RGBA, typ)
        if linear:
            GL.glTexParameteri(R, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
            GL.glTexParameteri(R, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
        self.fbo = sdp.fbo_for(self.tex)

    def bind(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glViewport(0, 0, self.w, self.h)

    def clear(self):
        self.bind()
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read8(self):
        self.bind()
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        return np.frombuffer(a, np.uint8).reshape(self.h, self.w, 4).copy()

    def readf(self):
        self.bind()
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_FLOAT)
        return np.frombuffer(a, np.float32).reshape(self.h, self.w, 4).copy()


def upload(tex, arr):
    GL.glBindTexture(R, tex)
    GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
    GL.glTexSubImage2D(R, 0, 0, 0, arr.shape[1], arr.shape[0], GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, np.ascontiguousarray(arr))


class Ascii:
    PASSES = ["asciiCells", "asciiParts", "asciiMarks", "asciiMarkWrite", "asciiDecide", "asciiGlyphs",
              "asciiTrail", "asciiDown", "asciiBlur", "asciiOut"]

    def __init__(self, rnd):
        self.rnd = rnd
        v = "GL2" if rnd.gl2 else "GL3"
        d = os.path.join(REPO, "bin", "data", "shaders", "ascii", v)
        self.p = {n: sdp.compile_program(rnd.gl2, os.path.join(d, "ascii.vert"), os.path.join(d, n + ".frag"))
                  for n in self.PASSES}
        GL.glDisable(GL.GL_BLEND)
        self.colour = sdp.rect_tex(PW, PH, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.parts = sdp.rect_tex(PW, PH, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.layer = sdp.rect_tex(PW, PH, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.marks = Fbo(PW, PH, GL.GL_RGBA8)
        self.frame = Fbo(PW, PH, GL.GL_RGBA16F)
        self.trail = [Fbo(PW, PH, GL.GL_RGBA16F), Fbo(PW, PH, GL.GL_RGBA16F)]
        self.out = Fbo(PW, PH, GL.GL_RGBA8)
        tw, th = (PW + 3) // 4, (PH + 3) // 4
        self.tight = [Fbo(tw, th, GL.GL_RGBA16F, True) for _ in range(2)]
        self.wide = [Fbo((tw + 1) // 2, (th + 1) // 2, GL.GL_RGBA16F, True) for _ in range(2)]
        self.grids = {}
        self.atlases = {}
        self.trail_cur = 0
        self.trail_valid = False
        self.pools = pools()

    def reset(self):
        self.trail_valid = False

    def grid(self, cw, ch):
        key = (cw, ch)
        if key not in self.grids:
            gw, gh = (PW + cw - 1) // cw, (PH + ch - 1) // ch
            self.grids[key] = {"cell": Fbo(gw, gh, GL.GL_RGBA16F), "part": Fbo(gw, gh, GL.GL_RGBA8),
                               "mark": Fbo(gw, gh, GL.GL_RGBA8), "decide": Fbo(gw, gh, GL.GL_RGBA16F)}
        return self.grids[key]

    def atlas(self, cw, ch, px, centring):
        key = (cw, ch, px, centring)
        if key not in self.atlases:
            a, size, pad = bake_atlas(cw, ch, px, centring)
            t = sdp.rect_tex(a.shape[1], a.shape[0], GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, a)
            self.atlases[key] = (t, size, atlas_cells(a, cw, ch, pad), pad)
        return self.atlases[key]

    def use(self, name):
        p = self.p[name]
        GL.glUseProgram(p)
        return p

    def run(self, colour8, parts8, layers, prm, t, cw, ch):
        """One frame of AsciiFilter::draw(); layers: [(rgba8, glyph, premultiplied)]."""
        rnd = self.rnd
        u, tex = rnd.u, rnd.tex
        upload(self.colour, colour8)
        upload(self.parts, parts8)
        g = self.grid(cw, ch)
        gw, gh = g["cell"].w, g["cell"].h
        atlas_tex, _, _, pad = self.atlas(cw, ch, font_px(prm, ch), int(prm["glyphCentring"]))

        # Marks: beginMarks() + addMarks() per layer.
        self.marks.clear()
        for arr, glyph, pre in layers:
            upload(self.layer, arr)
            self.marks.bind()
            p = self.use("asciiMarkWrite")
            tex(p, "srcTex", 0, self.layer)
            u(p, "srcScale", 1.0, 1.0)
            u(p, "premultiplied", 1 if pre else 0, i=True)
            u(p, "glyph", float(ord(glyph)))
            u(p, "threshold", prm["markThreshold"])
            rnd.quad(p, PW, PH)
        marks_on = len(layers) > 0

        g["part"].bind()
        p = self.use("asciiParts")
        tex(p, "partsTex", 0, self.parts)
        u(p, "srcSize", float(PW), float(PH))
        u(p, "cellSize", float(cw), float(ch))
        rnd.quad(p, gw, gh)

        g["cell"].bind()
        p = self.use("asciiCells")
        tex(p, "srcTex", 0, self.colour)
        tex(p, "partsTex", 1, self.parts)
        tex(p, "cellPartTex", 2, g["part"].tex)
        u(p, "srcSize", float(PW), float(PH))
        u(p, "partsScale", 1.0, 1.0)
        u(p, "cellSize", float(cw), float(ch))
        u(p, "blackThreshold", prm["blackThreshold"])
        u(p, "exposure", prm["exposure"])
        u(p, "saturation", prm["saturation"])
        rnd.quad(p, gw, gh)

        if marks_on:
            g["mark"].bind()
            p = self.use("asciiMarks")
            tex(p, "marksTex", 0, self.marks.tex)
            u(p, "srcSize", float(PW), float(PH))
            u(p, "cellSize", float(cw), float(ch))
            rnd.quad(p, gw, gh)

        fill = np.array([ord(pool[k % len(pool)]) for pool in self.pools for k in range(12)], np.float32)
        g["decide"].bind()
        p = self.use("asciiDecide")
        tex(p, "cellTex", 0, g["cell"].tex)
        tex(p, "partTex", 1, g["part"].tex)
        tex(p, "markTex", 2, (g["mark"] if marks_on else g["part"]).tex)
        u(p, "marksOn", 1 if marks_on else 0, i=True)
        u(p, "gridSize", float(gw), float(gh))
        u(p, "minCoverage", prm["minCoverage"])
        u(p, "edgeFloor", prm["edgeFloor"])
        u(p, "oneSided", 1 if prm["edgeOneSided"] else 0, i=True)
        u(p, "sideOnly", 1 if prm["edgeSideOnly"] else 0, i=True)
        GL.glUniform1fv(GL.glGetUniformLocation(p, "fillGlyph"), 48, fill)
        u(p, "fillFloor", prm["fillFloor"])
        u(p, "fillStart", prm["fillStart"])
        u(p, "fillScale", max(0.0, prm["fillScale"]))
        GL.glUniform1fv(GL.glGetUniformLocation(p, "shimmerRate"), 3,
                        np.array([prm["shimmerRate0"], prm["shimmerRate1"], prm["shimmerRate2"]], np.float32))
        u(p, "time", float(t))
        rnd.quad(p, gw, gh)

        self.frame.bind()
        p = self.use("asciiGlyphs")
        tex(p, "decideTex", 0, g["decide"].tex)
        tex(p, "atlasTex", 1, atlas_tex)
        u(p, "cellSize", float(cw), float(ch))
        u(p, "atlasPad", float(pad))
        rnd.quad(p, PW, PH)

        trail = min(max(prm["trailAmount"], 0.0), 0.98)
        result = self.frame
        if trail > 0:
            prev = self.trail[self.trail_cur]
            if not self.trail_valid:
                prev.clear()
            nxt = self.trail[1 - self.trail_cur]
            nxt.bind()
            p = self.use("asciiTrail")
            tex(p, "curTex", 0, self.frame.tex)
            tex(p, "prevTex", 1, prev.tex)
            u(p, "amount", trail)
            rnd.quad(p, PW, PH)
            self.trail_cur = 1 - self.trail_cur
            self.trail_valid = True
            result = nxt
        else:
            self.trail_valid = False

        glow_on = prm["glowTight"] > 0 or prm["glowWide"] > 0
        if glow_on:
            def down(src, dst, factor):
                dst.bind()
                p = self.use("asciiDown")
                tex(p, "srcTex", 0, src.tex)
                u(p, "srcSize", float(src.w), float(src.h))
                u(p, "factor", float(factor))
                rnd.quad(p, dst.w, dst.h)

            def blur(src, dst, dx, dy, sigma, reach):
                dst.bind()
                p = self.use("asciiBlur")
                tex(p, "srcTex", 0, src.tex)
                u(p, "dir", dx, dy)
                u(p, "sigma", sigma)
                u(p, "reach", reach)
                rnd.quad(p, dst.w, dst.h)
            down(result, self.tight[0], 4)
            blur(self.tight[0], self.tight[1], 1.0, 0.0, 1.78, 4.0)
            blur(self.tight[1], self.tight[0], 0.0, 1.0, 1.78, 4.0)
            down(self.tight[0], self.wide[0], 2)
            blur(self.wide[0], self.wide[1], 1.0, 0.0, 2.40, 5.0)
            blur(self.wide[1], self.wide[0], 0.0, 1.0, 2.40, 5.0)

        self.out.bind()
        p = self.use("asciiOut")
        tex(p, "frameTex", 0, result.tex)
        tex(p, "tightTex", 1, self.tight[0].tex)
        tex(p, "wideTex", 2, self.wide[0].tex)
        u(p, "glowOn", 1 if glow_on else 0, i=True)
        u(p, "tightAmount", max(0.0, prm["glowTight"]))
        u(p, "wideAmount", max(0.0, prm["glowWide"]))
        u(p, "blackFloor", min(max(prm["glowFloor"], 0.0), 0.5))
        u(p, "glowOutside", 1 if prm["glowOutside"] else 0, i=True)
        rnd.quad(p, PW, PH)
        GL.glUseProgram(0)
        return self.out.read8()

    def decide(self, cw, ch):
        return self.grid(cw, ch)["decide"].readf()


# ------------------------------------------------------------ the scene
def hand_bump(box, k, frames=8):
    """Grid elevation (mm) of a hand crossing the flat sand: frame k of frames."""
    gy, gx = np.mgrid[0:box.rows, 0:box.cols].astype(np.float64)
    x0 = 30.0 + 6.0 * k
    return 75.0 * np.exp(-((gx - x0) ** 2 + (gy - 262.0) ** 2) / (2 * 10.0 ** 2)) if k < frames else 0.0 * gx


def set_depth(box, extra):
    z = box.depth.copy()
    z[sdp.ROI[1]:sdp.ROI[1] + box.rows, sdp.ROI[0]:sdp.ROI[0] + box.cols] = (sdp.Z0 - (box.h + extra)).astype(np.float32)
    return z


def mark_layers(box, k, seed=5):
    """The three mark layers at frame k, each as the app hands it over."""
    rng = np.random.default_rng(seed)
    # Seeds (SeedLayer FBO, premultiplied): soft 4 px points near pioneer patch edges.
    pio = box.cover[1]
    ys, xs = np.nonzero((pio > 0.25) & (pio < 0.4))
    sel = rng.choice(len(xs), size=min(60, len(xs)), replace=False)
    base = np.array([0.788, 0.859, 0.431])
    pts, cols = [], []
    for i in sel:
        x, y = sdp.to_proj(xs[i] + 0.3 * k, ys[i] - 0.2 * k)
        pts.append((x, y))
        cols.append(base * (0.6 + 0.4 * box.site[ys[i], xs[i], 0]))
    s = sdp.soft_points(PH, PW, pts, 4.0, cols, 1.0)
    seeds = np.zeros((PH, PW, 4), np.uint8)
    seeds[..., :3] = np.round(np.clip(s[..., :3], 0, 1) * 255)
    seeds[..., 3] = np.round(np.clip(s[..., 3], 0, 1) * 255)
    # Dewitz particles (RGB FBO, additive): short streaks swirling in the basin
    # ("Water only": particles fade out in still water, so only the flowing part).
    part = np.zeros((PH, PW, 3), np.float64)
    ys, xs = np.nonzero(box.water)
    sel = rng.choice(len(xs), size=min(120, len(xs)), replace=False)
    colour = np.array([0.75, 0.92, 1.0])
    for i in sel:
        gx, gy = xs[i] + rng.uniform(), ys[i] + rng.uniform()
        ang0 = math.atan2(gy - 95, gx - 250)
        r = math.hypot(gx - 250, gy - 95)
        ang = ang0 + 0.05 * k
        cx, cy = 250 + r * math.cos(ang), 95 + r * math.sin(ang)
        tang = ang + math.pi / 2
        for tt in np.linspace(0, 1, 16):
            x, y = sdp.to_proj(cx - math.cos(tang) * 5 * tt, cy - math.sin(tang) * 5 * tt)
            xi, yi = int(x), int(y)
            if 0 <= xi < PW and 0 <= yi < PH:
                part[yi, xi] = np.maximum(part[yi, xi], colour * 0.6 * (0.92 ** (tt * 20)))
    flow = np.zeros((PH, PW, 4), np.uint8)
    flow[..., :3] = np.round(np.clip(part, 0, 1) * 255)
    flow[..., 3] = 255
    # Critters (CritterController FBO, straight alpha over white): 6 dots.
    crit = np.zeros((PH, PW, 4), np.uint8)
    crit[..., :3] = 255
    for j in range(6):
        gx, gy = 330 + 25 * j + 2 * k, 110 + 7 * j
        x, y = sdp.to_proj(gx, gy)
        yy, xx = np.mgrid[0:PH, 0:PW]
        d = np.hypot(xx + 0.5 - x, yy + 0.5 - y)
        m = d < 5.0
        crit[m, 0], crit[m, 1], crit[m, 2], crit[m, 3] = 222, 170, 110, 255
    return [(seeds, GLYPHS["seed"], True), (flow, GLYPHS["particle"], True), (crit, GLYPHS["critter"], False)]


def crops():
    sdp.init_crops()
    c = dict(sdp.CROPS)
    x, y = sdp.to_proj(70, 262)
    c["hand"] = (int(x) - 90, int(y) - 45, 220, 90)
    return c


class Scene:
    """The Sandbox inputs (colour, parts, marks) for frames 0..frames-1, cached."""
    def __init__(self, rnd, box, rd, frames=8, hand=True):
        self.colour, self.parts, self.layers = [], [], []
        base = box.depth
        for k in range(frames):
            box.depth = set_depth(box, hand_bump(box, k, frames)) if hand else base
            self.colour.append(rnd.sandbox(box, rd, ASCII_COLOUR))
            self.parts.append(rnd.sandbox(box, rd, ASCII_PARTS))
            self.layers.append(mark_layers(box, k))
        box.depth = base


def render_sequence(asc, scene, prm, cw, ch, fps=60.0):
    asc.reset()
    out = None
    for k in range(len(scene.colour)):
        out = asc.run(scene.colour[k], scene.parts[k], scene.layers[k], prm, k / fps, cw, ch)
    return out


def save(img, name):
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(img[..., :3]).save(os.path.join(OUT, name))


def crop(img, rect, zoom):
    x, y, w, h = rect
    c = img[y:y + h, x:x + w, :3]
    return np.repeat(np.repeat(c, zoom, 0), zoom, 1)


# ------------------------------------------------------------ commands
def setup(gl2=False, fill_off=False):
    ctx = sdp.Context()
    rnd = sdp.Renderer(ctx, gl2)
    box = sdp.Box(5)
    rd = sdp.renderer_defaults()
    if fill_off:
        rd["bandFill"] = False
    return ctx, rnd, box, rd


def cmd_frames(args):
    ctx, rnd, box, rd = setup(args.gl2, args.fill_off)
    asc = Ascii(rnd)
    d = ascii_defaults()
    print("filter defaults:", d)
    scene = Scene(rnd, box, rd)
    tag = "_filloff" if args.fill_off else ""
    save(scene.colour[-1], "input_colour%s.png" % tag)
    parts = scene.parts[-1].astype(np.int32)
    vis = np.zeros_like(scene.parts[-1])
    vis[..., 0] = np.clip(parts[..., 0] * 18, 0, 255)
    vis[..., 1] = parts[..., 1] * 110
    vis[..., 2] = parts[..., 2] * 80
    vis[..., 3] = 255
    save(vis, "input_parts%s.png" % tag)
    off = sdp.compose(rnd.sandbox(box, rd, sdp.OLD), box, np.random.default_rng(5))
    save(off, "filter_off%s.png" % tag)
    C = crops()
    sizes = cell_sizes()
    rows_zoom, rows_hand = [], []
    for pi, (cw, ch) in enumerate(sizes):
        row_z, row_h = [], []
        for trail_on in (True, False):
            for glow_on in (True, False):
                prm = dict(d, trailAmount=d["trailAmount"] if trail_on else 0.0,
                           glowTight=d["glowTight"] if glow_on else 0.0, glowWide=d["glowWide"] if glow_on else 0.0)
                img = render_sequence(asc, scene, prm, cw, ch)
                name = "ascii_%s_trail%s_glow%s%s.png" % (PRESET_NAMES[pi], "On" if trail_on else "Off",
                                                          "On" if glow_on else "Off", tag)
                save(img, name)
                lab = "%s trail %s glow %s" % (PRESET_NAMES[pi], "on" if trail_on else "off", "on" if glow_on else "off")
                if trail_on == glow_on:
                    row_z.append(sdp.label(crop(img, C["mound"], 3), lab))
                    row_z.append(sdp.label(crop(img, C["water"], 3), lab))
                if glow_on:
                    row_h.append(sdp.label(crop(img, C["hand"], 2), lab))
        rows_zoom.append(row_z)
        rows_hand.append(row_h)
        print("preset %s: atlas font %d px" % (PRESET_NAMES[pi], asc.atlas(cw, ch, font_px(d, ch), int(d["glyphCentring"]))[1]))
    save(sdp.sheet(rows_zoom), "sheet_zoom_mound_water%s.png" % tag)
    save(sdp.sheet(rows_hand), "sheet_hand_trail%s.png" % tag)
    # Stages, default preset, zoomed.
    cw, ch = sizes[d["cellPreset"]] if isinstance(d["cellPreset"], int) else sizes[1]
    img = render_sequence(asc, scene, d, cw, ch)
    save(np.asarray(sdp.sheet([[crop(img, C["stages"], 2)], [crop(off, C["stages"], 2)]])), "sheet_stages_vs_off%s.png" % tag)
    for i, (cw, ch) in enumerate(sizes):
        a = asc.atlas(cw, ch, font_px(d, ch), int(d["glyphCentring"]))[2]
        save(np.repeat(np.repeat(np.stack([a] * 4, -1), 4, 0), 4, 1), "atlas_%s.png" % PRESET_NAMES[i])
    print("wrote", OUT)


def empty_pixels(asc, cw, ch):
    """Per pixel: True in cells the filter left empty (glyph code 0)."""
    dec = asc.decide(cw, ch)
    empty = dec[..., 3] < 0.5
    return np.repeat(np.repeat(empty, ch, 0), cw, 1)[:PH, :PW]


def cmd_checks(args):
    d = ascii_defaults()
    sizes = cell_sizes()
    # 1. Exact black in empty cells.
    for fill_off in (False, True):
        ctx, rnd, box, rd = setup(False, fill_off)
        asc = Ascii(rnd)
        scene = Scene(rnd, box, rd, frames=1, hand=False)
        for pi, (cw, ch) in enumerate(sizes):
            for outside in (False, True):
                prm = dict(d, glowOutside=outside)
                asc.reset()
                for k in range(6):   # a still scene: trails settle
                    img = asc.run(scene.colour[0], scene.parts[0], scene.layers[0], prm, k / 60.0, cw, ch)
                em = empty_pixels(asc, cw, ch)
                nz = (img[em, :3] > 0).any(-1)
                print("exact black, band fill %-3s %-5s glow on, floor %.2f, glow lights empty cells %-5s: "
                      "%7d empty-cell px, %6d not (0,0,0), max %d"
                      % ("off" if fill_off else "on", PRESET_NAMES[pi], d["glowFloor"], outside,
                         em.sum(), nz.sum(), img[em, :3].max() if em.any() else 0))
        # A hand trail fades to exact black once the hand has gone.
        asc.reset()
        cw, ch = sizes[1]
        mv = Scene(rnd, box, rd, frames=8, hand=True)
        for k in range(8):
            asc.run(mv.colour[k], mv.parts[k], [], d, k / 60.0, cw, ch)
        for k in range(8, 48):
            img = asc.run(scene.colour[0], scene.parts[0], [], d, k / 60.0, cw, ch)
        em = empty_pixels(asc, cw, ch)
        print("exact black after a hand trail (40 still frames later), band fill %s: %d of %d empty-cell px not 0"
              % ("off" if fill_off else "on", (img[em, :3] > 0).any(-1).sum(), em.sum()))
        if fill_off:
            continue
        # Parts: what the height shader wrote.
        p = scene.parts[0].astype(int)
        inside = p[..., 3] == 255
        print("parts: bands %s, layers %s, stages %s; water pixels %d, plant pixels %d"
              % (sorted(set(np.unique(p[inside, 0]))), sorted(set(np.unique(p[inside, 1]))),
                 sorted(set(np.unique(p[inside, 2]))), (p[..., 1] == 2).sum(), (p[..., 1] == 1).sum()))
        col = scene.colour[0]
        code = 255 - col[..., 3].astype(int)
        print("parts stage = the colour's dither stage code (alpha) wherever drawn: %s"
              % bool(((code == p[..., 2]) | ~inside).all()))
    # 2. Off and Dither identical to ss/dither-baseline.
    tmp = tempfile.mkdtemp()
    for v in ("GL2", "GL3"):
        os.makedirs(os.path.join(tmp, v))
        for f in ("heightMapShader.vert", "heightMapShader.frag"):
            src = subprocess.check_output(["git", "-C", REPO, "show", "origin/ss/dither-baseline:bin/data/shaders/shaders%s/%s" % (v, f)])
            open(os.path.join(tmp, v, f), "wb").write(src)
    unchanged = subprocess.call(["git", "-C", REPO, "diff", "--quiet", "origin/ss/dither-baseline", "--",
                                 "bin/data/shaders/dither", "src/Common/DitherFilter.cpp", "src/Common/DitherFilter.h"]) == 0
    print("dither shaders and DitherFilter.* unchanged from ss/dither-baseline: %s" % unchanged)
    variants = {"Off, defaults": dict(sdp.OLD), "Off, band fill off": dict(sdp.OLD, bandFill=False),
                "Off, every N mm": dict(sdp.OLD, landLineMode=1), "Off, plants off": dict(sdp.OLD, plantDisplay=2),
                "Off, contour lines": dict(sdp.OLD, drawContourLines=True, contourLineFactor=0.1),
                "Dither, defaults": dict(sdp.NEW), "Dither, band fill off": dict(sdp.NEW, bandFill=False)}
    dd = sdp.dither_defaults()
    for gl2 in (False, True):
        ctx = sdp.Context()
        ref = sdp.Renderer(ctx, gl2, os.path.join(tmp, "GL2" if gl2 else "GL3"))
        cur = sdp.Renderer(ctx, gl2)
        box = sdp.Box(5)
        rd = sdp.renderer_defaults()
        for name, ov in variants.items():
            rdx = dict(rd)
            if "bandFill" in ov:
                rdx["bandFill"] = ov["bandFill"]
            a = ref.sandbox(box, rdx, ov)
            b = cur.sandbox(box, rdx, ov)
            same = np.array_equal(a, b)
            extra = ""
            if name.startswith("Dither"):
                ca = sdp.compose(a, box, np.random.default_rng(5))
                cb = sdp.compose(b, box, np.random.default_rng(5))
                fa, fb = cur.dither(ca, dd), cur.dither(cb, dd)
                extra = ", dither filter output identical: %s" % np.array_equal(fa, fb)
            print("%s %-22s height shader = ss/dither-baseline's: %s (RGBA)%s"
                  % ("GL2" if gl2 else "GL3", name, same, extra))
    # 3. GL2 = GL3 for the ASCII filter.
    outs = {}
    for gl2 in (False, True):
        ctx, rnd, box, rd = setup(gl2)
        asc = Ascii(rnd)
        scene = Scene(rnd, box, rd, frames=4, hand=True)
        outs[gl2] = [scene.colour[-1], scene.parts[-1]]
        for cw, ch in sizes:
            outs[gl2].append(render_sequence(asc, scene, d, cw, ch))
    names = ["colour (ASCII on)", "parts"] + ["ASCII %s, 4 frames" % n for n in PRESET_NAMES]
    for n, a, b in zip(names, outs[False], outs[True]):
        diff = np.abs(a.astype(int) - b.astype(int))
        print("GL2 vs GL3, %-24s identical %s (max channel difference %d)" % (n, bool(diff.max() == 0), diff.max()))


def cmd_timing(args):
    ctx, rnd, box, rd = setup(args.gl2)
    asc = Ascii(rnd)
    d = ascii_defaults()
    scene = Scene(rnd, box, rd, frames=1, hand=False)
    print(GL.glGetString(GL.GL_RENDERER).decode())
    for pi, (cw, ch) in enumerate(cell_sizes()):
        for label, prm in (("defaults", d), ("trail and glow off", dict(d, trailAmount=0.0, glowTight=0.0, glowWide=0.0))):
            asc.run(scene.colour[0], scene.parts[0], scene.layers[0], prm, 0.0, cw, ch)
            GL.glFinish()
            n = 10
            ts = []
            for k in range(n):
                t = time.perf_counter()
                asc.run(scene.colour[0], scene.parts[0], scene.layers[0], prm, k / 60.0, cw, ch)
                GL.glFinish()
                ts.append(1000 * (time.perf_counter() - t))
            # The same uploads and read-back with no passes, to subtract.
            t = time.perf_counter()
            for k in range(n):
                upload(asc.colour, scene.colour[0])
                upload(asc.parts, scene.parts[0])
                for arr, _, _ in scene.layers[0]:
                    upload(asc.layer, arr)
                asc.out.read8()
            GL.glFinish()
            io = 1000 * (time.perf_counter() - t) / n
            print("1280 x 800, cells %-5s %-19s median %6.1f ms per frame, of which uploads + read-back %5.1f ms"
                  % (PRESET_NAMES[pi], label, float(np.median(ts)), io))


def cmd_pairs(args):
    """Before / after sheets: args.before = an earlier out/ directory (same file names)."""
    C = crops()
    tag = "_filloff" if args.fill_off else ""
    for name in PRESET_NAMES:
        f = "ascii_%s_trailOn_glowOn%s.png" % (name, tag)
        b = np.asarray(Image.open(os.path.join(args.before, f)).convert("RGB"))
        a = np.asarray(Image.open(os.path.join(OUT, f)).convert("RGB"))
        rows = [[sdp.label(np.ascontiguousarray(b[::2, ::2]), "before, %s, trail + glow" % name),
                 sdp.label(np.ascontiguousarray(a[::2, ::2]), "after, %s, trail + glow" % name)]]
        for k in ("mound", "water", "stages"):
            z = 2 if k == "stages" else 3
            rows.append([sdp.label(crop(b, C[k], z), "before: " + k), sdp.label(crop(a, C[k], z), "after: " + k)])
        save(sdp.sheet(rows), "pair_%s%s.png" % (name, tag))
    print("wrote pair_*.png to", OUT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["frames", "checks", "timing", "pairs"])
    ap.add_argument("--before", help="pairs: the out/ directory of an earlier run")
    ap.add_argument("--gl2", action="store_true")
    ap.add_argument("--fill-off", action="store_true", help="frames: Band fill off (dark-room look)")
    a = ap.parse_args()
    globals()["cmd_" + a.cmd](a)


if __name__ == "__main__":
    main()
