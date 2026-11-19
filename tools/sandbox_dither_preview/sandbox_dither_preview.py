#!/usr/bin/env python3
"""Headless preview and checks for the Sandbox dither filter (DitherFilter).

A Sandbox frame is rendered with the app's own height-map shader
(bin/data/shaders/shadersGL3|GL2/heightMapShader.*), unchanged, through a
stand-in calibration (pinhole Kinect 640 x 480 with the ROI 519 x 295, a
projector 1280 x 800 seeing the box at ~1.17 px/mm, rotated 1.5 deg), on the
synthetic terrain of tools/ascii_preview (flat sand at 9 mm, a mound to
71 mm, a basin dug to -36 mm and a ramp -40..70 mm through every band), with
simulated water where the bed is below the water table (surface -14 mm),
plant cover (grain dither, the default) and soil. Shader uniforms are the
SandSurfaceRenderer defaults (header, plus the repository's
sandSurfaceRendererSettings.xml). The seed and flow-particle layers are
stand-ins composited with the app's blend equations (seeds: premultiplied
"over" at 0.7 on the fill; Dewitz particles: 1-px streaks, additive);
critters default to 0 and are not drawn.

The frame then goes through bin/data/shaders/dither (both passes, as
DitherFilter::draw), with the defaults parsed from src/Common/DitherFilter.cpp.

    python3 sandbox_dither_preview.py frames   # out/: off, every mode x pattern, zoomed crops, sheets
    python3 sandbox_dither_preview.py lines    # line protection: hit rate on lines, false hits elsewhere
    python3 sandbox_dither_preview.py colour   # band colours kept (hue, luminance) per band
    python3 sandbox_dither_preview.py moire    # low-pass beat energy, grain x dots, per dot size
    python3 sandbox_dither_preview.py compare  # GL2 vs GL3 (height shader and filter)
    python3 sandbox_dither_preview.py timing   # filter passes on this GL (software: not the installation PC)

Needs numpy, Pillow, PyOpenGL and an EGL driver (Mesa: surfaceless).
"""

import argparse
import math
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ascii_preview"))
import ascii_preview as A  # EGL Context, rect_tex, fbo_for, ortho, Sandbox heightmap

import numpy as np
from PIL import Image
from OpenGL import GL

REPO = A.REPO
OUT = os.path.join(HERE, "out")
R = GL.GL_TEXTURE_RECTANGLE
PW, PH = 1280, 800
KW, KH = 640, 480
ROI = (60, 92, 519, 295)
F_K = 580.0                          # Kinect focal length, px
CX, CY = 319.5, 239.5
Z0 = 1.9 * F_K                       # mm: 1.9 mm per Kinect pixel at the base plane
WATER_SURFACE = -14.0                # mm: the table (-15) + ~1 mm
FULL_DEPTH_MM = 0.05 * 285.2         # MAX_WATER_DEPTH_FRAC x this box's range (+-142.6 mm)
PALETTES = {0: ["1A1206", "9C7A2E", "F2D58A"], 1: ["05070D", "2E4466", "C9D6E8"], 2: ["020806", "2F7A55", "CFE8D2"]}
MODES = {0: "hue", 1: "channel", 2: "palette"}
PATTERNS = {0: "bayer4", 1: "bayer8", 2: "lines", 3: "halftone"}


def rgb(h):
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


def lum(c):
    return c[..., 0] * 0.2126 + c[..., 1] * 0.7152 + c[..., 2] * 0.0722


# ------------------------------------------------------------ defaults
def dither_defaults():
    s = open(os.path.join(REPO, "src", "Common", "DitherFilter.cpp")).read()
    body = s[s.index("makeDefaults()"):s.index("DitherFilter::DEFAULTS = makeDefaults")]
    enums = {"MODE_HUE": 0, "MODE_CHANNEL": 1, "MODE_PALETTE": 2, "PATTERN_BAYER4": 0, "PATTERN_BAYER8": 1,
             "PATTERN_LINES": 2, "PATTERN_HALFTONE": 3, "PROTECT_OFF": 0, "PROTECT_BRIGHT": 1,
             "PROTECT_BRIGHT_DARK": 2, "PALETTE_GOLD": 0, "PALETTE_NIGHT": 1, "PALETTE_GREEN": 2}
    d = {}
    for n, v in re.findall(r"p\.(\w+) = ([^;]+);", body):
        v = v.strip()
        if v in ("true", "false"):
            d[n] = v == "true"
        elif v.startswith("DitherFilter::"):
            d[n] = enums[v.split("::")[1]]
        else:
            d[n] = float(v.rstrip("f"))
    m = re.search(r"applyPalettePreset\(p, DitherFilter::(\w+)\)", body)
    d["palettePreset"] = enums[m.group(1)]
    return d


def renderer_defaults():
    h = open(os.path.join(REPO, "src", "SandSurfaceRenderer", "SandSurfaceRenderer.h")).read()
    d = {"drawContourLines": False, "contourLineDistance": 10.0}  # set in setup(), then the settings file
    for t, n, v in re.findall(r"^\s*(bool|int|float) (\w+) = ([^;]+);", h, re.M):
        v = v.strip()
        d[n] = (v == "true") if t == "bool" else float(v.rstrip("f"))
    xml = open(os.path.join(REPO, "bin", "data", "settings", "sandSurfaceRendererSettings.xml")).read()
    for n, v in re.findall(r"<(\w+)>([^<]*)</\1>", xml):
        if n in d:
            d[n] = (v.strip() == "1") if isinstance(d[n], bool) else float(v)
    return d


def colour_keys():
    xml = open(os.path.join(REPO, "bin", "data", "colorMaps", "HeightColorMap.xml")).read()
    keys = []
    for k in re.findall(r"<key>(.*?)</key>", xml, re.S):
        g = lambda t: float(re.search(rf"<{t}>(.*?)</{t}>", k).group(1))
        keys.append((g("height"), np.array([g("color-r"), g("color-g"), g("color-b")], np.float64)))
    return sorted(keys, key=lambda k: k[0])


def colour_map_texture(keys, n=512):
    """ColorMap::updateColormap(): n entries, piecewise linear between keys, 8-bit."""
    lo, hi = keys[0][0], keys[-1][0]
    hs = np.array([k[0] for k in keys])
    out = np.zeros((1, n, 4), np.uint8)
    for i in range(n):
        val = i * (hi - lo) / (n - 1) + lo
        l, r = 0, len(keys) - 1
        while r - l > 1:
            m = (l + r) >> 1
            if hs[m] <= val:
                l = m
            else:
                r = m
        w = (val - hs[l]) / (hs[r] - hs[l]) if hs[r] != hs[l] else 0.0
        c = keys[l][1] * (1 - w) + keys[r][1] * w
        out[0, i, :3] = np.clip(c, 0, 255).astype(np.uint8)  # ofColor: truncation
        out[0, i, 3] = 255
    return out


# ------------------------------------------------------------ the box
class Box:
    def __init__(self, seed=5):
        rng = np.random.default_rng(seed)
        sb = A.Sandbox()
        self.cols, self.rows = ROI[2], ROI[3]
        h = sb.heightmap().astype(np.float64)
        gy, gx = np.mgrid[0:self.rows, 0:self.cols].astype(np.float64)
        h += rng.uniform(-0.4, 0.4, h.shape)                       # filtered-depth noise
        self.h = h
        depth = WATER_SURFACE - h
        self.water = depth > 0.3
        code = np.where(self.water, 1.0 + 248.0 * np.clip(depth / FULL_DEPTH_MM, 0, 1), 0.0)
        g = lambda x0, y0, s: np.exp(-((gx - x0) ** 2 + (gy - y0) ** 2) / (2 * s * s))
        pioneer = 0.75 * g(250, 160, 45) + 0.55 * g(330, 60, 30)
        inter = 0.6 * g(150, 205, 28) + 0.45 * g(95, 95, 25)
        climax = 0.7 * g(200, 260, 22)
        tot = pioneer + inter + climax
        k = np.where(tot > 1.0, 1.0 / np.maximum(tot, 1e-6), 1.0)
        land = ~self.water
        self.cover = [climax * k * land, pioneer * k * land, inter * k * land]   # R, G, B as the texture
        veg = np.zeros((self.rows, self.cols, 4), np.uint8)
        for i in range(3):
            veg[..., i] = np.clip(self.cover[i] * 255, 0, 255).astype(np.uint8)
        veg[..., 3] = code.astype(np.uint8)
        self.veg = veg
        soil = np.clip(0.15 + 0.7 * g(200, 180, 80), 0, 1)
        site = np.zeros((self.rows, self.cols, 4), np.float32)
        site[..., 0] = soil
        site[..., 1] = np.clip(1.0 - np.abs(h + 14.0) / 40.0, 0, 1)
        self.site = site
        # Kinect depth image (mm from the camera), whole sensor; flat sand outside the ROI.
        z = np.full((KH, KW), Z0 - 9.0, np.float32)
        z[ROI[1]:ROI[1] + self.rows, ROI[0]:ROI[0] + self.cols] = (Z0 - h).astype(np.float32)
        self.depth = z


def matrices():
    world = np.array([[1 / F_K, 0, 0, -CX / F_K], [0, 1 / F_K, 0, -CY / F_K], [0, 0, 0, 1], [0, 0, 0, 1]], np.float32)
    s = 0.9 * min(PW / ROI[2], PH / ROI[3])        # projector px per Kinect px
    fp = s * F_K
    a = math.radians(1.5)
    c, sn = math.cos(a), math.sin(a)
    proj = np.array([[fp * c, -fp * sn, PW / 2.0, 0], [fp * sn, fp * c, PH / 2.0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], np.float32)
    return world, proj


# ------------------------------------------------------------ GL
def tex2d(w, h, internal, fmt, typ, data, linear=False):
    t = A.rect_tex(w, h, internal, fmt, typ, data)
    if linear:
        GL.glTexParameteri(R, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
        GL.glTexParameteri(R, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    return t


def compile_program(gl2, vert, frag, patch=None):
    p = GL.glCreateProgram()
    for path, kind in ((vert, GL.GL_VERTEX_SHADER), (frag, GL.GL_FRAGMENT_SHADER)):
        src = open(path).read()
        s = GL.glCreateShader(kind)
        GL.glShaderSource(s, src)
        GL.glCompileShader(s)
        if not GL.glGetShaderiv(s, GL.GL_COMPILE_STATUS):
            raise RuntimeError("%s: %s" % (path, GL.glGetShaderInfoLog(s).decode()))
        log = GL.glGetShaderInfoLog(s)
        if log and log.strip():
            print("  compile log %s: %s" % (os.path.relpath(path, REPO), log.decode().strip()))
        GL.glAttachShader(p, s)
    if not gl2:
        GL.glBindAttribLocation(p, 0, "position")
        GL.glBindAttribLocation(p, 3, "texcoord")
    GL.glLinkProgram(p)
    if not GL.glGetProgramiv(p, GL.GL_LINK_STATUS):
        raise RuntimeError("link %s: %s" % (frag, GL.glGetProgramInfoLog(p).decode()))
    return p


class Target:
    def __init__(self, w, h, internal):
        self.w, self.h = w, h
        fmt_t = GL.GL_FLOAT if internal != GL.GL_RGBA8 else GL.GL_UNSIGNED_BYTE
        self.tex = A.rect_tex(w, h, internal, GL.GL_RGBA, fmt_t)
        self.fbo = A.fbo_for(self.tex)

    def begin(self, clear=(0, 0, 0, 0)):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glViewport(0, 0, self.w, self.h)
        GL.glClearColor(*clear)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read8(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        return np.frombuffer(a, np.uint8).reshape(self.h, self.w, 4).copy()


class Renderer:
    def __init__(self, ctx, gl2):
        self.gl2 = gl2
        self.glctx = ctx.create(gl2)
        ctx.make_current(self.glctx)
        v = "GL2" if gl2 else "GL3"
        sh = os.path.join(REPO, "bin", "data", "shaders")
        self.p_height = compile_program(gl2, os.path.join(sh, "shaders" + v, "heightMapShader.vert"),
                                        os.path.join(sh, "shaders" + v, "heightMapShader.frag"))
        dv = os.path.join(sh, "dither", v)
        self.p_dots = compile_program(gl2, os.path.join(dv, "dither.vert"), os.path.join(dv, "ditherDots.frag"))
        self.p_out = compile_program(gl2, os.path.join(dv, "dither.vert"), os.path.join(dv, "ditherOut.frag"))
        if not gl2:
            GL.glBindVertexArray(GL.glGenVertexArrays(1))
        GL.glDisable(GL.GL_BLEND)
        self.sand = Target(PW, PH, GL.GL_RGBA8)
        self.out = Target(PW, PH, GL.GL_RGBA8)
        self.src_tex = A.rect_tex(PW, PH, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.dots = {}

    # -- geometry
    def draw(self, prog, pos, texc, idx, w, h, mode=GL.GL_TRIANGLES):
        mvp = A.ortho(w, h)
        pos = np.ascontiguousarray(pos, np.float32)
        texc = np.ascontiguousarray(texc, np.float32)
        idx = np.ascontiguousarray(idx, np.uint32)
        if self.gl2:
            GL.glMatrixMode(GL.GL_PROJECTION)
            GL.glLoadMatrixf(mvp)
            GL.glMatrixMode(GL.GL_MODELVIEW)
            GL.glLoadIdentity()
            GL.glEnableClientState(GL.GL_VERTEX_ARRAY)
            GL.glEnableClientState(GL.GL_TEXTURE_COORD_ARRAY)
            GL.glVertexPointer(2, GL.GL_FLOAT, 0, pos)
            GL.glTexCoordPointer(2, GL.GL_FLOAT, 0, texc)
            GL.glDrawElements(mode, len(idx), GL.GL_UNSIGNED_INT, idx)
            return
        GL.glUniformMatrix4fv(GL.glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL.GL_FALSE, mvp)
        bufs = GL.glGenBuffers(3)
        for b, arr, loc in ((bufs[0], pos, 0), (bufs[1], texc, 3)):
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, b)
            GL.glBufferData(GL.GL_ARRAY_BUFFER, arr.nbytes, arr, GL.GL_STREAM_DRAW)
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, bufs[2])
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, idx.nbytes, idx, GL.GL_STREAM_DRAW)
        GL.glDrawElements(mode, len(idx), GL.GL_UNSIGNED_INT, None)
        GL.glDisableVertexAttribArray(0)
        GL.glDisableVertexAttribArray(3)
        GL.glDeleteBuffers(3, bufs)

    def quad(self, prog, w, h):
        pos = np.array([[0, 0], [w, 0], [0, h], [w, h]], np.float32)
        self.draw(prog, pos, pos, np.array([0, 1, 2, 1, 3, 2], np.uint32), w, h)

    # -- uniforms
    def u(self, prog, name, *v, i=False):
        loc = GL.glGetUniformLocation(prog, name)
        if loc < 0:
            return
        f = {1: GL.glUniform1i if i else GL.glUniform1f, 2: GL.glUniform2f, 3: GL.glUniform3f, 4: GL.glUniform4f}[len(v)]
        f(loc, *v)

    def tex(self, prog, name, unit, t):
        GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
        GL.glBindTexture(R, t)
        loc = GL.glGetUniformLocation(prog, name)
        if loc >= 0:
            GL.glUniform1i(loc, unit)

    # -- the Sandbox height shader (SandSurfaceRenderer::drawSandbox)
    def sandbox(self, box, rd, overrides=None):
        d = dict(rd)
        d.update(overrides or {})
        keys = colour_keys()
        cmap = colour_map_texture(keys)
        n = cmap.shape[1]
        elev_min, elev_max = -keys[0][0], -keys[-1][0]
        hm_scale = (n - 1) / (elev_max - elev_min)
        hm_offset = 0.5 / n - hm_scale * elev_min
        edges = [keys[i][0] for i in range(1, len(keys)) if not np.array_equal(keys[i][1], keys[i - 1][1])][:16]
        gaps = []
        for i in range(len(edges)):
            g = 1e3
            if i > 0:
                g = min(g, edges[i] - edges[i - 1])
            if i + 1 < len(edges):
                g = min(g, edges[i + 1] - edges[i])
            gaps.append(g)
        t_cmap = tex2d(n, 1, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, cmap, linear=True)
        t_depth = tex2d(KW, KH, GL.GL_R32F, GL.GL_RED, GL.GL_FLOAT, box.depth, linear=True)
        t_veg = tex2d(box.cols, box.rows, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, box.veg, linear=True)
        t_site = tex2d(box.cols, box.rows, GL.GL_RGBA32F, GL.GL_RGBA, GL.GL_FLOAT, box.site, linear=True)
        t_contour = A.rect_tex(PW + 1, PH + 1, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        world, proj = matrices()
        p = self.p_height
        self.sand.begin((0, 0, 0, 1))
        GL.glUseProgram(p)
        self.tex(p, "tex0", 0, t_depth)
        self.tex(p, "heightColorMapSampler", 2, t_cmap)
        self.tex(p, "pixelCornerElevationSampler", 3, t_contour)
        self.tex(p, "vegetationSampler", 5, t_veg)
        self.tex(p, "siteSampler", 6, t_site)
        for name, m in (("kinectProjMatrix", proj), ("kinectWorldMatrix", world)):
            GL.glUniformMatrix4fv(GL.glGetUniformLocation(p, name), 1, GL.GL_TRUE, m)
        self.u(p, "heightColorMapTransformation", hm_scale, hm_offset)
        self.u(p, "depthTransformation", 1.0, 0.0)
        self.u(p, "basePlaneEq", 0.0, 0.0, 1.0, -Z0)
        self.u(p, "contourLineFactor", 1.0)
        self.u(p, "drawContourLines", int(d["drawContourLines"]), i=True)
        self.u(p, "heightMapNumEntries", float(n))
        self.u(p, "time", 0.0)
        wl_den = hm_scale * max(0.1, d["waterLineDistance"])
        self.u(p, "drawWaterLines", int(d["drawWaterLines"]), i=True)
        self.u(p, "waterLineTransform", 1.0 / wl_den, -hm_offset / wl_den)
        self.u(p, "waterLineGlowPx", d["waterLineGlowPx"])
        self.u(p, "waterLineBrightness", d["waterLineBrightness"])
        self.u(p, "drawShorelineGlow", int(d["drawShorelineGlow"]), i=True)
        self.u(p, "drawLandLines", int(d["drawLandLines"]), i=True)
        self.u(p, "landLineMode", int(d["landLineMode"]), i=True)
        self.u(p, "heightTransform", -1.0 / hm_scale, hm_offset / hm_scale)
        self.u(p, "landLineSpacing", d["landLineSpacing"])
        self.u(p, "landLineOffset", d["landLineOffset"])
        self.u(p, "landEdgeCount", len(edges), i=True)
        e16 = np.zeros(16, np.float32); e16[:len(edges)] = edges
        g16 = np.zeros(16, np.float32); g16[:len(gaps)] = gaps
        GL.glUniform1fv(GL.glGetUniformLocation(p, "landEdge"), 16, e16)
        GL.glUniform1fv(GL.glGetUniformLocation(p, "landEdgeGap"), 16, g16)
        self.u(p, "landLineGlowPx", d["landLineGlowPx"])
        self.u(p, "landLineBrightness", d["landLineBrightness"])
        self.u(p, "bandOutlineWidthPx", d["bandOutlineWidthPx"])
        self.u(p, "drawLandShore", int(d["drawLandShore"]), i=True)
        self.u(p, "landLineShade", d["landLineShade"])
        self.u(p, "dimUnderPlants", d["dimUnderPlants"])
        self.u(p, "bandFill", int(d["bandFill"]), i=True)
        fade = d["flatFadeAmount"] * 15.0
        self.u(p, "flatFadeSlope", math.tan(math.radians(fade)) if fade > 0 else 0.0)
        self.u(p, "kinectROIRect", ROI[0] - 0.5, ROI[1] - 0.5, ROI[0] + ROI[2] - 1.5, ROI[1] + ROI[3] - 1.5)
        self.u(p, "hasVegetation", 1, i=True)
        self.u(p, "drawSimWater", 1, i=True)
        self.u(p, "vegetationGridOrigin", float(ROI[0]), float(ROI[1]))
        self.u(p, "vegetationGridStep", 1.0)
        self.u(p, "hasSite", 1, i=True)
        self.u(p, "soilTintStrength", 0.35)
        self.u(p, "siteDebugView", 0, i=True)
        self.u(p, "hasPlants", 0, i=True)
        self.u(p, "plantDisplay", int(d.get("plantDisplay", 1)), i=True)
        self.u(p, "grainScale", float(d.get("grainScale", 1.0)))
        self.u(p, "grainBrightness", 1.0 if d["bandFill"] else 1.5)
        self.u(p, "debugShowSnow", 0, i=True)
        self.u(p, "waterLevelFrac", 0.5)
        self.u(p, "snowLevelFrac", 0.745)
        # setupMesh(): one vertex per ROI Kinect pixel, -0.5 px.
        gy, gx = np.mgrid[0:ROI[3], 0:ROI[2]].astype(np.float32)
        pts = np.stack([gx + ROI[0] - 0.5, gy + ROI[1] - 0.5], -1).reshape(-1, 2)
        mw, mh = ROI[2], ROI[3]
        a = (np.arange(mh - 1)[:, None] * mw + np.arange(mw - 1)[None, :]).ravel().astype(np.uint32)
        idx = np.stack([a, a + 1, a + mw, a + 1, a + mw + 1, a + mw], -1).ravel()
        self.draw(p, pts, pts, idx, PW, PH)
        GL.glUseProgram(0)
        img = self.sand.read8()
        GL.glDeleteTextures([t_cmap, t_depth, t_veg, t_site, t_contour])
        return img

    # -- DitherFilter::draw()
    def dither(self, frame8, prm):
        GL.glBindTexture(R, self.src_tex)
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
        GL.glTexSubImage2D(R, 0, 0, 0, PW, PH, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, np.ascontiguousarray(frame8))
        ds = int(max(1, min(8, prm["dotSize"])))
        cols, rows = (PW + ds - 1) // ds, (PH + ds - 1) // ds
        if (cols, rows) not in self.dots:
            self.dots[(cols, rows)] = Target(cols, rows, GL.GL_RGBA16F)
        dots = self.dots[(cols, rows)]
        ex, co, sa = prm["exposure"], prm["contrast"], prm["saturation"]
        p = self.p_dots
        dots.begin()
        GL.glUseProgram(p)
        self.tex(p, "srcTex", 0, self.src_tex)
        self.u(p, "srcSize", float(PW), float(PH))
        self.u(p, "dotSize", float(ds))
        self.u(p, "exposure", ex); self.u(p, "contrast", co); self.u(p, "saturation", sa)
        self.quad(p, cols, rows)
        p = self.p_out
        self.out.begin()
        GL.glUseProgram(p)
        self.tex(p, "srcTex", 0, self.src_tex)
        self.tex(p, "dotTex", 1, dots.tex)
        self.u(p, "srcSize", float(PW), float(PH))
        self.u(p, "dotSize", float(ds))
        self.u(p, "exposure", ex); self.u(p, "contrast", co); self.u(p, "saturation", sa)
        self.u(p, "mode", int(prm["mode"]), i=True)
        self.u(p, "pattern", int(prm["pattern"]), i=True)
        self.u(p, "levels", float(max(2, min(16, prm["levels"]))))
        self.u(p, "blackPoint", prm["blackPoint"])
        self.u(p, "linePeriod", float(max(1, min(8, prm["linePeriod"]))))
        a = math.radians(prm["halftoneAngle"])
        self.u(p, "halftoneRot", math.cos(a), math.sin(a))
        self.u(p, "halftoneCell", float(max(2, min(16, prm["halftoneCell"]))))
        self.u(p, "paletteStops", 2.0 if prm["paletteStops"] <= 2 else 3.0)
        pal = np.array([rgb(h) for h in PALETTES[int(prm["palettePreset"])]], np.float32).ravel()
        GL.glUniform3fv(GL.glGetUniformLocation(p, "paletteColor"), 3, pal)
        self.u(p, "lineMode", int(prm["lineProtect"]), i=True)
        self.u(p, "lineThreshold", prm["lineThreshold"])
        self.u(p, "lineReach", float(ds + 1))
        self.quad(p, PW, PH)
        GL.glUseProgram(0)
        return self.out.read8()


# ------------------------------------------------------------ the other layers (stand-ins)
def soft_points(h, w, pts, size_px, colours, alpha):
    """Premultiplied RGBA of soft round points (SeedLayer-like)."""
    img = np.zeros((h, w, 4), np.float64)
    r = int(math.ceil(size_px))
    for (x, y), c in zip(pts, colours):
        x0, y0 = int(x), int(y)
        for yy in range(max(0, y0 - r), min(h, y0 + r + 1)):
            for xx in range(max(0, x0 - r), min(w, x0 + r + 1)):
                d = math.hypot(xx + 0.5 - x, yy + 0.5 - y) / (0.5 * size_px)
                a = alpha * max(0.0, 1.0 - d * d)
                if a <= 0:
                    continue
                img[yy, xx, :3] = img[yy, xx, :3] * (1 - a) + np.array(c) * a
                img[yy, xx, 3] = img[yy, xx, 3] * (1 - a) + a
    return img


def to_proj(gx, gy):
    """Grid cell (ROI) to projector px through the stand-in calibration."""
    world, proj = matrices()
    kx, ky = gx + ROI[0], gy + ROI[1]
    hz = 9.0
    z = Z0 - hz
    X, Y = (kx - CX) / F_K * z, (ky - CY) / F_K * z
    v = proj @ np.array([X, Y, z, 1.0])
    return v[0] / v[2], v[1] / v[2]


def compose(sand8, box, rng):
    """Sandbox layers over the height shader's frame, as drawProjWindow draws them."""
    out = sand8[..., :3].astype(np.float64) / 255.0
    # Seeds (SeedLayer, band fill on): premultiplied over at 0.7; 4 px, pioneer colour by soil.
    pts, cols = [], []
    pio = box.cover[1]
    edge = (pio > 0.25) & (pio < 0.4)
    ys, xs = np.nonzero(edge)
    sel = rng.choice(len(xs), size=min(160, len(xs)), replace=False)
    base = np.array([0.788, 0.859, 0.431])
    for k in sel:
        x, y = to_proj(xs[k] + rng.uniform(), ys[k] + rng.uniform())
        s = box.site[ys[k], xs[k], 0]
        grey = base @ np.array([0.299, 0.587, 0.114]) * 0.45
        pts.append((x, y))
        cols.append(grey + (base - grey) * s)
    seeds = soft_points(PH, PW, pts, 4.0, cols, 1.0)
    o = 0.7
    out = out * (1 - o * seeds[..., 3:4]) + o * seeds[..., :3]
    out = np.round(np.clip(out, 0, 1) * 255) / 255
    # Dewitz particles: 1-px streaks with a fading trail, additive, on water.
    part = np.zeros((PH, PW, 3), np.float64)
    ys, xs = np.nonzero(box.water)
    sel = rng.choice(len(xs), size=min(500, len(xs)), replace=False)
    colour = np.array([0.75, 0.92, 1.0])
    for k in sel:
        gx, gy = xs[k] + rng.uniform(), ys[k] + rng.uniform()
        ang = math.atan2(gy - 110, gx - 290) + math.pi / 2  # a slow swirl round the basin centre
        L = rng.uniform(6, 14)
        for t in np.linspace(0, 1, 24):
            x, y = to_proj(gx - math.cos(ang) * L * t / 2.2, gy - math.sin(ang) * L * t / 2.2)
            xi, yi = int(x), int(y)
            if 0 <= xi < PW and 0 <= yi < PH:
                part[yi, xi] = np.maximum(part[yi, xi], colour * 0.6 * (1 - 0.08) ** (t * 20))
    out = np.minimum(out + part, 1.0)
    out = np.round(out * 255).astype(np.uint8)
    img = np.zeros((PH, PW, 4), np.uint8)
    img[..., :3] = out
    img[..., 3] = 255
    return img


# ------------------------------------------------------------ helpers
CROPS = {"mound": (180, 260, 160, 100), "water": (560, 250, 160, 100), "ramp": (960, 480, 160, 100)}


def save(img, name, d=OUT):
    os.makedirs(d, exist_ok=True)
    Image.fromarray(img[..., :3]).save(os.path.join(d, name))


def crop(img, key, zoom=4):
    x, y, w, h = CROPS[key]
    c = img[y:y + h, x:x + w, :3]
    return np.repeat(np.repeat(c, zoom, 0), zoom, 1)


def sheet(rows, labels):
    """rows: list of lists of zoomed crops; a 6-px black gutter."""
    g = 6
    h = sum(r[0].shape[0] for r in rows) + g * (len(rows) + 1)
    w = sum(c.shape[1] for c in rows[0]) + g * (len(rows[0]) + 1)
    s = np.zeros((h, w, 3), np.uint8)
    y = g
    for r in rows:
        x = g
        for c in r:
            s[y:y + c.shape[0], x:x + c.shape[1]] = c
            x += c.shape[1] + g
        y += r[0].shape[0] + g
    return s


FILL_OFF = False


def setup(gl2=False, seed=5):
    ctx = A.Context()
    rnd = Renderer(ctx, gl2)
    box = Box(seed)
    rd = renderer_defaults()
    if FILL_OFF:
        rd["bandFill"] = False
    sand = rnd.sandbox(box, rd)
    frame = compose(sand, box, np.random.default_rng(seed))
    return ctx, rnd, box, rd, sand, frame


def prefilter_np(c, ex, co, sa):
    c = np.maximum(c, 0) * ex
    l = lum(c)[..., None]
    c = np.maximum(l + (c - l) * sa, 0)
    ls = lum(c)[..., None]
    lt = np.maximum((l - 0.5) * co + 0.5, 0)
    c = np.where(ls > 1e-5, c * lt / np.maximum(ls, 1e-5), 0)
    m = c.max(-1, keepdims=True)
    return np.where(m > 1, c / np.maximum(m, 1e-9), c)


# ------------------------------------------------------------ commands
def cmd_frames(args):
    ctx, rnd, box, rd, sand, frame = setup(args.gl2)
    d = dither_defaults()
    save(frame, "off.png")
    print("defaults:", d)
    rows = [[crop(frame, k) for k in CROPS]]
    configs = []
    for m in (0, 1, 2):
        for pt in (0, 1, 2, 3):
            prm = dict(d, mode=m, pattern=pt)
            img = rnd.dither(frame, prm)
            name = "on_%s_%s.png" % (MODES[m], PATTERNS[pt])
            save(img, name)
            configs.append(name)
            rows.append([crop(img, k) for k in CROPS])
    s = sheet(rows[:5], None)
    save(s, "sheet_hue.png")
    save(sheet([rows[0]] + rows[5:9], None), "sheet_channel.png")
    save(sheet([rows[0]] + rows[9:13], None), "sheet_palette.png")
    # Line protection off vs on, hue / Bayer 4 (defaults otherwise).
    lp = [[crop(frame, k) for k in CROPS],
          [crop(rnd.dither(frame, dict(d, lineProtect=0)), k) for k in CROPS],
          [crop(rnd.dither(frame, d), k) for k in CROPS]]
    save(sheet(lp, None), "sheet_line_protection.png")
    # Dot sizes 1..4 (hue, Bayer 4).
    ds = [[crop(rnd.dither(frame, dict(d, dotSize=k)), "mound") for k in (1, 2, 3, 4)]]
    save(sheet(ds, None), "sheet_dot_sizes.png")
    print("wrote", len(configs) + 1, "frames and 5 sheets to", OUT)


def line_masks(rnd, box, rd):
    """Pixels the land and water lines change by > 0.06 luminance (and > 0.25)."""
    base = rnd.sandbox(box, rd, {"drawLandLines": False, "drawWaterLines": False})
    full = rnd.sandbox(box, rd)
    dl = np.abs(lum(full[..., :3] / 255.0) - lum(base[..., :3] / 255.0))
    return dl > 0.06, dl > 0.25


def line_kinds(rnd, box, rd):
    """Land-line and water-line pixels (each changes luminance by > 0.06)."""
    full = lum(rnd.sandbox(box, rd)[..., :3] / 255.0)
    no_land = lum(rnd.sandbox(box, rd, {"drawLandLines": False})[..., :3] / 255.0)
    no_water = lum(rnd.sandbox(box, rd, {"drawWaterLines": False})[..., :3] / 255.0)
    return np.abs(full - no_land) > 0.06, np.abs(full - no_water) > 0.06


def cmd_lines(args):
    """Share of pixels drawn undithered (protected) per kind of pixel. A pixel
    is protected if the output equals its pre-filtered colour while the
    unprotected output does not."""
    ctx, rnd, box, rd, sand, frame = setup(args.gl2)
    d = dither_defaults()
    line_any, line_strong = line_masks(rnd, box, rd)
    no_grain = rnd.sandbox(box, rd, {"plantDisplay": 2})
    grain = np.any(no_grain[..., :3] != sand[..., :3], -1) & ~line_any
    detail = (np.abs(lum(frame[..., :3] / 255.0) - lum(sand[..., :3] / 255.0)) > 0.06) & ~line_any
    inside = np.zeros(line_any.shape, bool)
    inside[1:-1, 1:-1] = sand[1:-1, 1:-1, :3].sum(-1) > 0          # inside the projected box
    near_grain = grain.copy()
    for _ in range(3):  # grain and its gaps (3 px)
        g = near_grain.copy()
        g[1:] |= near_grain[:-1]; g[:-1] |= near_grain[1:]; g[:, 1:] |= near_grain[:, :-1]; g[:, :-1] |= near_grain[:, 1:]
        near_grain = g
    grain = near_grain & ~line_any & ~detail
    plain = inside & ~(line_any | detail | grain)
    src = prefilter_np(frame[..., :3] / 255.0, d["exposure"], d["contrast"], d["saturation"])
    src8 = np.round(src * 255)
    raw = rnd.dither(frame, dict(d, lineProtect=0))
    raw_same = np.all(np.abs(raw[..., :3].astype(int) - src8) <= 1, -1)
    print("pixels: lines %d (strong %d), particles/seeds %d, plant grain and its gaps %d, plain %d"
          % (line_any.sum(), line_strong.sum(), detail.sum(), grain.sum(), plain.sum()))
    land, water = line_kinds(rnd, box, rd)
    print("land-line pixels %d (darker than the fill: %.0f %%), water-line pixels %d"
          % (land.sum(), 100 * (lum(sand[..., :3] / 255.0)[land] < lum(no_grain[..., :3] / 255.0)[land] + 1e-9).mean(), water.sum()))
    print("mode  threshold  lines  strong lines  land   water  particles/seeds  grain   plain (false)")
    for lp in (1, 2):
        for th in (0.06, 0.08, 0.10, 0.12, 0.15, 0.20, 0.30):
            img = rnd.dither(frame, dict(d, lineProtect=lp, lineThreshold=th))
            prot = np.all(np.abs(img[..., :3].astype(int) - src8) <= 1, -1) & ~raw_same
            print("%d     %.2f      %5.1f %%  %5.1f %%       %5.1f %% %5.1f %% %5.1f %%          %5.1f %%  %5.2f %%"
                  % (lp, th, 100 * prot[line_any].mean(), 100 * prot[line_strong].mean(),
                     100 * prot[land].mean(), 100 * prot[water].mean(),
                     100 * prot[detail].mean(), 100 * prot[grain].mean(), 100 * prot[plain].mean()))
    if args.save:
        img = rnd.dither(frame, d)
        prot = np.all(np.abs(img[..., :3].astype(int) - src8) <= 1, -1) & ~raw_same
        vis = (frame[..., :3] * 0.35).astype(np.uint8)
        vis[prot] = [255, 210, 0]
        save(vis, "protected_pixels.png")


def band_regions(box):
    keys = colour_keys()
    edges = [keys[i][0] for i in range(1, len(keys)) if not np.array_equal(keys[i][1], keys[i - 1][1])]
    return edges


def cmd_colour(args):
    """Per band: mean colour of the frame vs the dithered frame over plain
    pixels (no lines), in 8 x 8 blocks; hue angle and luminance ratio."""
    ctx, rnd, box, rd, sand, frame = setup(args.gl2)
    d = dither_defaults()
    base = rnd.sandbox(box, rd, {"drawLandLines": False, "drawWaterLines": False})
    line_any = np.abs(lum(sand[..., :3] / 255.0) - lum(base[..., :3] / 255.0)) > 0.02
    q = (frame[..., :3] // 8).astype(np.int32)
    key = q[..., 0] * 1024 + q[..., 1] * 32 + q[..., 2]
    vals, counts = np.unique(key[~line_any], return_counts=True)
    top = vals[np.argsort(-counts)][:12]

    def hue(c):
        mx, mn = c.max(), c.min()
        if mx - mn < 1e-6:
            return float("nan")
        r, g, b = c
        if mx == r:
            h = ((g - b) / (mx - mn)) % 6
        elif mx == g:
            h = (b - r) / (mx - mn) + 2
        else:
            h = (r - g) / (mx - mn) + 4
        return 60 * h

    for m in (0, 1, 2):
        img = rnd.dither(frame, dict(d, mode=m))
        print("mode %s (Bayer 4, dot %d, levels %d)" % (MODES[m], d["dotSize"], d["levels"]))
        print("  colour (frame)   px      hue frame  hue dots  d-hue   lum frame  lum dots")
        for k in top:
            mask = (key == k) & ~line_any
            if mask.sum() < 2000:
                continue
            a = frame[..., :3][mask].mean(0) / 255
            b = img[..., :3][mask].mean(0) / 255
            ha, hb = hue(a), hue(b)
            dh = ((hb - ha + 180) % 360) - 180 if not (math.isnan(ha) or math.isnan(hb)) else float("nan")
            print("  #%02X%02X%02X       %6d   %6.1f    %6.1f   %+5.1f    %.3f      %.3f"
                  % (*(np.round(a * 255).astype(int)), mask.sum(), ha, hb, dh, lum(a[None])[0], lum(b[None])[0]))


def gauss(img, s):
    r = int(3 * s)
    k = np.exp(-np.arange(-r, r + 1) ** 2 / (2 * s * s))
    k /= k.sum()
    pad = np.pad(img, ((r, r), (r, r)), mode="edge")
    t = np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 1, pad)
    return np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 0, t)


def cmd_moire(args):
    """Low-frequency error of the dots against the frame (Gaussian sigma 3 px,
    ~2.6 mm on the sand: structure the eye still resolves at ~1 m), in a
    grain region and in a plain-fill region; and its spectrum peak period."""
    ctx, rnd, box, rd, sand, frame = setup(args.gl2)
    d = dither_defaults()
    regions = {"grain (pioneer 1 px + intermediate 2 px + climax 4 px)": (150, 300, 260, 180),
               "plain fill (no plants)": (560, 120, 200, 110)}
    src = lum(frame[..., :3] / 255.0)
    for gs in (1.0, 1.5, 2.0):
        fr = frame if gs == 1.0 else compose(rnd.sandbox(box, rd, {"grainScale": gs}), box, np.random.default_rng(5))
        src = lum(fr[..., :3] / 255.0)
        print("grainScale %.1f" % gs)
        print("  dot  pattern   " + "  ".join("%-12s" % n.split(" ")[0] for n in regions) + "  (RMS low-pass luminance error; peak period px)")
        for ds in (1, 2, 3, 4, 6):
            for pt in (0, 1):
                img = rnd.dither(fr, dict(d, dotSize=ds, pattern=pt, lineProtect=0))
                out = lum(img[..., :3] / 255.0)
                cells = []
                for n, (x, y, w, h) in regions.items():
                    e = gauss(out[y:y + h, x:x + w], 3.0) - gauss(src[y:y + h, x:x + w], 3.0)
                    e = e[12:-12, 12:-12]
                    spec = np.abs(np.fft.rfft2(e - e.mean()))
                    spec[0, 0] = 0
                    iy, ix = np.unravel_index(np.argmax(spec), spec.shape)
                    fy = (iy if iy <= e.shape[0] // 2 else iy - e.shape[0]) / e.shape[0]
                    fx = ix / e.shape[1]
                    per = 1.0 / max(math.hypot(fx, fy), 1e-9)
                    cells.append("%.4f/%5.1f" % (np.sqrt((e ** 2).mean()), per))
                print("  %d    %-8s  %s" % (ds, PATTERNS[pt], "  ".join("%-12s" % c for c in cells)))


def cmd_compare(args):
    out = []
    for gl2 in (False, True):
        ctx, rnd, box, rd, sand, frame = setup(gl2)
        d = dither_defaults()
        imgs = [sand, frame]
        for m in (0, 1, 2):
            for pt in (0, 1, 2, 3):
                imgs.append(rnd.dither(frame, dict(d, mode=m, pattern=pt)))
        out.append(imgs)
    names = ["height shader", "composite"] + ["%s/%s" % (MODES[m], PATTERNS[p]) for m in (0, 1, 2) for p in (0, 1, 2, 3)]
    for n, a, b in zip(names, out[0], out[1]):
        diff = np.abs(a.astype(int) - b.astype(int))
        print("GL2 vs GL3, %-18s identical %s (max channel difference %d, pixels differing %.4f %%)"
              % (n, bool(diff.max() == 0), diff.max(), 100 * np.any(diff > 0, -1).mean()))


def cmd_timing(args):
    ctx, rnd, box, rd, sand, frame = setup(args.gl2)
    d = dither_defaults()
    for ds in (1, 2, 4, 8):
        prm = dict(d, dotSize=ds)
        rnd.dither(frame, prm)
        GL.glFinish()
        n = 10
        t = time.perf_counter()
        for _ in range(n):
            rnd.dither(frame, prm)
        GL.glFinish()
        print("dot %d: %.1f ms per filter (upload + 2 passes + read-back), %s" % (ds, 1000 * (time.perf_counter() - t) / n,
              GL.glGetString(GL.GL_RENDERER).decode()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["frames", "lines", "colour", "moire", "compare", "timing"])
    ap.add_argument("--gl2", action="store_true")
    ap.add_argument("--save", action="store_true", help="lines: also write out/protected_pixels.png")
    ap.add_argument("--fill-off", action="store_true", help="Band fill off (dark-room look)")
    a = ap.parse_args()
    globals()["FILL_OFF"] = a.fill_off
    globals()["cmd_" + a.cmd](a)


if __name__ == "__main__":
    main()
