#!/usr/bin/env python3
"""Headless preview and checks for the Sandbox dither filter (DitherFilter),
branch ss/dither-baseline.

A Sandbox frame is rendered with the app's own height-map shader
(bin/data/shaders/shadersGL3|GL2/heightMapShader.*) through a stand-in
calibration (pinhole Kinect 640 x 480 with the ROI 519 x 295, a projector
1280 x 800 seeing the box at ~1.17 px/mm, rotated 1.5 deg) on a synthetic
terrain: flat sand at 9 mm, a mound to 71 mm (every land band, band-edge
lines), a basin dug to -36 mm (simulated water below the table, water
lines), and a grid of plant patches on flat sand: rows pioneer,
intermediate, climax; columns cover 0.2, 0.45, 0.7, 0.95 (soil S 0.7).
Shader uniforms are the SandSurfaceRenderer defaults (header, plus the
repository's sandSurfaceRendererSettings.xml). Seeds and Dewitz particles
are stand-ins composited with the app's blend equations; with the filter
on they write colour only (as ofApp's glColorMask), so the sand's alpha
(stage code) is kept. Critters default to 0 and are not drawn.

"Old" = filter off: plantDisplay 1 (grain), as ss/plant-grain.
"New" = filter on: plantDisplay 3 (flat, stage code in alpha), contour
diagonals solid, then bin/data/shaders/dither (dots, pyramid, output) with
the defaults parsed from src/Common/DitherFilter.cpp.

    python3 sandbox_dither_preview.py frames    # out/: old, new, sheets (zoomed crops)
    python3 sandbox_dither_preview.py stages    # dot size measured per patch, old and new
    python3 sandbox_dither_preview.py identity  # filter off: height shader = ss/plant-grain's, bit for bit
    python3 sandbox_dither_preview.py lines     # line protection per pixel kind (--save: map; --fill-off)
    python3 sandbox_dither_preview.py colour    # band hue and luminance kept
    python3 sandbox_dither_preview.py moire     # low-pass error in the planted patches, dot sizes 1..8
    python3 sandbox_dither_preview.py compare   # GL2 vs GL3
    python3 sandbox_dither_preview.py timing    # on this software GL, not the installation PC

Needs numpy, Pillow, PyOpenGL and an EGL driver (Mesa: surfaceless).
"""

import argparse
import ctypes
import math
import os
import re
import subprocess
import sys
import tempfile
import time

os.environ.setdefault("PYOPENGL_PLATFORM", "egl")
os.environ.setdefault("EGL_PLATFORM", "surfaceless")

import numpy as np
from PIL import Image
from OpenGL import EGL, GL

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
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
    for n, idx, v in re.findall(r"p\.(\w+)(?:\[(\d)\])? = ([^;]+);", body):
        n = n + idx
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
# Plant patches on flat sand: rows (grid y centre) by stage, columns by cover.
PATCH_ROWS = {"pioneer": 152.0, "intermediate": 204.0, "climax": 256.0}
PATCH_COLS = {0.2: 335.0, 0.45: 383.0, 0.7: 431.0, 0.95: 479.0}
PATCH_HALF = 20.0                    # cells (~45 projector px)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


class Box:
    def __init__(self, seed=5):
        rng = np.random.default_rng(seed)
        self.cols, self.rows = ROI[2], ROI[3]
        gy, gx = np.mgrid[0:self.rows, 0:self.cols].astype(np.float64)
        g = lambda x0, y0, s: np.exp(-((gx - x0) ** 2 + (gy - y0) ** 2) / (2 * s * s))
        h = 9.0 + 62.0 * g(110, 160, 42) - 45.0 * g(250, 95, 34)
        h += rng.uniform(-0.4, 0.4, h.shape)                       # filtered-depth noise
        self.h = h
        depth = WATER_SURFACE - h
        self.water = depth > 0.3
        code = np.where(self.water, 1.0 + 248.0 * np.clip(depth / FULL_DEPTH_MM, 0, 1), 0.0)
        cover = {k: np.zeros_like(h) for k in PATCH_ROWS}
        for stage, yc in PATCH_ROWS.items():
            for c, xc in PATCH_COLS.items():
                d = np.maximum(np.abs(gx - xc), np.abs(gy - yc))
                cover[stage] += c * (1 - smoothstep(PATCH_HALF - 4, PATCH_HALF, d))
        # Plants on the mound too (under the band-edge lines).
        cover["pioneer"] += 0.5 * g(150, 200, 18)
        cover["intermediate"] += 0.4 * g(75, 120, 16)
        land = ~self.water
        tot = cover["pioneer"] + cover["intermediate"] + cover["climax"]
        k = np.where(tot > 1.0, 1.0 / np.maximum(tot, 1e-6), 1.0) * land
        self.stage_cover = {st: cover[st] * k for st in cover}
        self.cover = [self.stage_cover["climax"], self.stage_cover["pioneer"], self.stage_cover["intermediate"]]
        veg = np.zeros((self.rows, self.cols, 4), np.uint8)
        for i in range(3):   # R climax, G pioneer, B intermediate (VegetationField's texture)
            veg[..., i] = np.clip(self.cover[i] * 255, 0, 255).astype(np.uint8)
        veg[..., 3] = code.astype(np.uint8)
        self.veg = veg
        soil = np.where((gx > 305) & (gy > 122), 0.7, 0.4)
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
class Context:
    """One EGL display; GL 3.2 core (GL3 shaders) or compatibility (GL2) contexts."""
    def __init__(self):
        self.display = EGL.eglGetDisplay(EGL.EGL_DEFAULT_DISPLAY)
        ma, mi = EGL.EGLint(), EGL.EGLint()
        if not EGL.eglInitialize(self.display, ctypes.pointer(ma), ctypes.pointer(mi)):
            raise RuntimeError("eglInitialize failed")
        cfg, n = EGL.EGLConfig(), EGL.EGLint()
        att = (EGL.EGLint * 5)(EGL.EGL_SURFACE_TYPE, EGL.EGL_PBUFFER_BIT,
                               EGL.EGL_RENDERABLE_TYPE, EGL.EGL_OPENGL_BIT, EGL.EGL_NONE)
        if not EGL.eglChooseConfig(self.display, att, ctypes.pointer(cfg), 1, ctypes.pointer(n)) or n.value < 1:
            raise RuntimeError("no EGL config")
        self.config = cfg
        EGL.eglBindAPI(EGL.EGL_OPENGL_API)
        self.surface = EGL.eglCreatePbufferSurface(self.display, cfg,
                                                   (EGL.EGLint * 5)(EGL.EGL_WIDTH, 16, EGL.EGL_HEIGHT, 16, EGL.EGL_NONE))

    def create(self, gl2):
        if gl2:
            ca = (EGL.EGLint * 3)(EGL.EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                  EGL.EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT, EGL.EGL_NONE)
        else:
            ca = (EGL.EGLint * 7)(EGL.EGL_CONTEXT_MAJOR_VERSION, 3, EGL.EGL_CONTEXT_MINOR_VERSION, 2,
                                  EGL.EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL.EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                  EGL.EGL_NONE)
        c = EGL.eglCreateContext(self.display, self.config, EGL.EGL_NO_CONTEXT, ca)
        if not c:
            raise RuntimeError("eglCreateContext failed")
        EGL.eglMakeCurrent(self.display, self.surface, self.surface, c)
        return c


def rect_tex(w, h, internal, fmt, typ, data=None):
    t = GL.glGenTextures(1)
    GL.glBindTexture(R, t)
    GL.glTexParameteri(R, GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST)
    GL.glTexParameteri(R, GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST)
    GL.glTexParameteri(R, GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE)
    GL.glTexParameteri(R, GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)
    GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
    GL.glTexImage2D(R, 0, internal, w, h, 0, fmt, typ, data)
    return t


def fbo_for(tex):
    f = GL.glGenFramebuffers(1)
    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, f)
    GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0, R, tex, 0)
    assert GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER) == GL.GL_FRAMEBUFFER_COMPLETE
    return f


def ortho(w, h):
    """Column-major projection: x, y (y down) -> texel row y (oF FBO convention)."""
    m = np.array([[2.0 / w, 0, 0, -1], [0, 2.0 / h, 0, -1], [0, 0, -1, 0], [0, 0, 0, 1]], np.float32)
    return m.T.copy()


def tex2d(w, h, internal, fmt, typ, data, linear=False):
    t = rect_tex(w, h, internal, fmt, typ, data)
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
        self.tex = rect_tex(w, h, internal, GL.GL_RGBA, fmt_t)
        self.fbo = fbo_for(self.tex)

    def begin(self, clear=(0, 0, 0, 0)):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glViewport(0, 0, self.w, self.h)
        GL.glClearColor(*clear)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read8(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        return np.frombuffer(a, np.uint8).reshape(self.h, self.w, 4).copy()

    def readf(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_FLOAT)
        return np.frombuffer(a, np.float32).reshape(self.h, self.w, 4).copy()


class Renderer:
    def __init__(self, ctx, gl2, height_dir=None):
        self.gl2 = gl2
        self.glctx = ctx.create(gl2)
        v = "GL2" if gl2 else "GL3"
        sh = os.path.join(REPO, "bin", "data", "shaders")
        hd = height_dir or os.path.join(sh, "shaders" + v)
        self.p_height = compile_program(gl2, os.path.join(hd, "heightMapShader.vert"),
                                        os.path.join(hd, "heightMapShader.frag"))
        dv = os.path.join(sh, "dither", v)
        self.p_dots = compile_program(gl2, os.path.join(dv, "dither.vert"), os.path.join(dv, "ditherDots.frag"))
        self.p_down = compile_program(gl2, os.path.join(dv, "dither.vert"), os.path.join(dv, "ditherDown.frag"))
        self.p_out = compile_program(gl2, os.path.join(dv, "dither.vert"), os.path.join(dv, "ditherOut.frag"))
        if not gl2:
            GL.glBindVertexArray(GL.glGenVertexArrays(1))
        GL.glDisable(GL.GL_BLEND)
        self.sand = Target(PW, PH, GL.GL_RGBA8)
        self.out = Target(PW, PH, GL.GL_RGBA8)
        self.src_tex = rect_tex(PW, PH, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.dots = {}

    # -- geometry
    def draw(self, prog, pos, texc, idx, w, h, mode=GL.GL_TRIANGLES):
        mvp = ortho(w, h)
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
        t_contour = rect_tex(PW + 1, PH + 1, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        world, proj = matrices()
        p = self.p_height
        # ASCII preview: the parts (partsPass 1) and surface (2) passes clear to
        # 0 = nothing drawn; the surface pass needs a float target.
        if d.get("partsPass") == 2 and not hasattr(self, "sandf"):
            self.sandf = Target(PW, PH, GL.GL_RGBA32F)
        target = self.sandf if d.get("partsPass") == 2 else self.sand
        target.begin((0, 0, 0, 0) if d.get("partsPass") else (0, 0, 0, 1))
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
        self.u(p, "contourLineFactor", d.get("contourLineFactor", 1.0))
        self.u(p, "drawContourLines", int(d["drawContourLines"]), i=True)
        self.u(p, "contourSolid", int(d.get("contourSolid", 0)), i=True)
        self.u(p, "partsPass", int(d.get("partsPass", 0)), i=True)        # ASCII filter parts (absent before ss/ascii-baseline)
        for name, unit, t in d.get("extraTex", ()):                        # ASCII preview: e.g. prevPartsSampler
            self.tex(p, name, unit, t)
        for name, (v, is_int) in d.get("extraU", {}).items():              # ASCII preview: e.g. bandMargin
            self.u(p, name, *(v if isinstance(v, tuple) else (v,)), i=is_int)
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
        img = target.readf() if target is not self.sand else self.sand.read8()
        GL.glDeleteTextures([t_cmap, t_depth, t_veg, t_site, t_contour])
        return img

    # -- DitherFilter::draw()
    def dither(self, frame8, prm):
        GL.glBindTexture(R, self.src_tex)
        GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
        GL.glTexSubImage2D(R, 0, 0, 0, PW, PH, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, np.ascontiguousarray(frame8))
        ds = int(max(1, min(8, prm["dotSize"])))
        level_of = lambda m: 0 if m < 2 else 1 if m < 4 else 2 if m < 8 else 3
        class_level = [0.0, 0.0, 0.0, 0.0]
        nlev = 1
        if prm["classesOn"]:
            for c in (1, 2, 3):
                class_level[c] = float(level_of(int(prm["classMult%d" % c])))
                nlev = max(nlev, int(class_level[c]) + 1)
        cols, rows = (PW + ds - 1) // ds, (PH + ds - 1) // ds
        levels = []
        for k in range(nlev):
            if (cols, rows) not in self.dots:
                self.dots[(cols, rows)] = Target(cols, rows, GL.GL_RGBA16F)
            levels.append(self.dots[(cols, rows)])
            cols, rows = (cols + 1) // 2, (rows + 1) // 2
        ex, co, sa = prm["exposure"], prm["contrast"], prm["saturation"]
        p = self.p_dots
        dots = levels[0]
        dots.begin()
        GL.glUseProgram(p)
        self.tex(p, "srcTex", 0, self.src_tex)
        self.u(p, "srcSize", float(PW), float(PH))
        self.u(p, "dotSize", float(ds))
        self.u(p, "exposure", ex); self.u(p, "contrast", co); self.u(p, "saturation", sa)
        self.quad(p, dots.w, dots.h)
        for k in range(1, nlev):
            p = self.p_down
            levels[k].begin()
            GL.glUseProgram(p)
            self.tex(p, "lowerTex", 0, levels[k - 1].tex)
            self.u(p, "lowerSize", float(levels[k - 1].w), float(levels[k - 1].h))
            self.quad(p, levels[k].w, levels[k].h)
        p = self.p_out
        self.out.begin()
        GL.glUseProgram(p)
        self.tex(p, "srcTex", 0, self.src_tex)
        self.tex(p, "dotTex", 1, dots.tex)
        for k in (1, 2, 3):
            self.tex(p, "dotTex%d" % k, 1 + k, levels[k].tex if k < nlev else dots.tex)
        self.u(p, "classesOn", int(bool(prm["classesOn"])), i=True)
        GL.glUniform1fv(GL.glGetUniformLocation(p, "classLevel"), 4, np.array(class_level, np.float32))
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
        ang = math.atan2(gy - 95, gx - 250) + math.pi / 2  # a slow swirl round the basin centre
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
    img[..., 3] = sand8[..., 3]   # colour-only layers (glColorMask): the sand's alpha stays
    return img


# ------------------------------------------------------------ helpers
def proj_rect(gx0, gy0, gx1, gy1):
    """Axis-aligned projector rectangle inside the grid rectangle (rotation 1.5 deg)."""
    pts = [to_proj(x, y) for x, y in ((gx0, gy0), (gx1, gy0), (gx0, gy1), (gx1, gy1))]
    xs = sorted(p[0] for p in pts)
    ys = sorted(p[1] for p in pts)
    x0, x1, y0, y1 = int(math.ceil(xs[1])), int(xs[2]), int(math.ceil(ys[1])), int(ys[2])
    return x0, y0, x1 - x0, y1 - y0


def patch_rect(stage, cover, inset=7.0):
    """Projector rectangle of a patch's flat interior, snapped to multiples of 16 px."""
    yc, xc = PATCH_ROWS[stage], PATCH_COLS[cover]
    x, y, w, h = proj_rect(xc - PATCH_HALF + inset, yc - PATCH_HALF + inset, xc + PATCH_HALF - inset, yc + PATCH_HALF - inset)
    x1, y1 = (x + w) // 16 * 16, (y + h) // 16 * 16
    x, y = (x + 15) // 16 * 16, (y + 15) // 16 * 16
    return x, y, x1 - x, y1 - y


CROPS = {}


def init_crops():
    CROPS["stages"] = proj_rect(PATCH_COLS[0.2] - PATCH_HALF - 2, PATCH_ROWS["pioneer"] - PATCH_HALF - 2,
                                PATCH_COLS[0.95] + PATCH_HALF + 2, PATCH_ROWS["climax"] + PATCH_HALF + 2)
    x, y = to_proj(150, 175)
    CROPS["mound"] = (int(x) - 80, int(y) - 50, 160, 100)
    x, y = to_proj(250, 95)
    CROPS["water"] = (int(x) - 40, int(y) - 50, 160, 100)
    x, y = to_proj(PATCH_COLS[0.45], PATCH_ROWS["intermediate"])
    CROPS["patch"] = (int(x) - 60, int(y) - 100, 120, 200)


def save(img, name, d=OUT):
    os.makedirs(d, exist_ok=True)
    Image.fromarray(img[..., :3]).save(os.path.join(d, name))


def crop(img, key, zoom=4):
    x, y, w, h = CROPS[key]
    c = img[y:y + h, x:x + w, :3]
    return np.repeat(np.repeat(c, zoom, 0), zoom, 1)


def sheet(rows, gutter=6):
    """rows: list of lists of images (same height per row); black gutters."""
    g = gutter
    h = sum(r[0].shape[0] for r in rows) + g * (len(rows) + 1)
    w = max(sum(c.shape[1] for c in r) + g * (len(r) + 1) for r in rows)
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
OLD = {"plantDisplay": 1, "contourSolid": 0}           # filter off (ss/plant-grain)
NEW = {"plantDisplay": 3, "contourSolid": 1}           # filter on


def setup(gl2=False, seed=5, height_dir=None):
    ctx = Context()
    rnd = Renderer(ctx, gl2, height_dir)
    box = Box(seed)
    rd = renderer_defaults()
    if FILL_OFF:
        rd["bandFill"] = False
    init_crops()
    sand_old = rnd.sandbox(box, rd, OLD)
    sand_new = rnd.sandbox(box, rd, NEW)
    old = compose(sand_old, box, np.random.default_rng(seed))
    new = compose(sand_new, box, np.random.default_rng(seed))
    return ctx, rnd, box, rd, (sand_old, old), (sand_new, new)


def prefilter_np(c, ex, co, sa):
    c = np.maximum(c, 0) * ex
    l = lum(c)[..., None]
    c = np.maximum(l + (c - l) * sa, 0)
    ls = lum(c)[..., None]
    lt = np.maximum((l - 0.5) * co + 0.5, 0)
    c = np.where(ls > 1e-5, c * lt / np.maximum(ls, 1e-5), 0)
    m = c.max(-1, keepdims=True)
    return np.where(m > 1, c / np.maximum(m, 1e-9), c)


def label(img, text):
    """A black strip with text above an image (PIL default font)."""
    from PIL import ImageDraw
    strip = Image.new("RGB", (img.shape[1], 18), (0, 0, 0))
    ImageDraw.Draw(strip).text((4, 3), text, fill=(230, 230, 230))
    return np.concatenate([np.asarray(strip), img], 0)


# ------------------------------------------------------------ commands
def cmd_frames(args):
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    print("filter defaults:", d)
    save(old, "old_filter_off.png")
    save(new, "new_source_flat.png")
    a = np.zeros_like(new)
    code = 255 - new[..., 3].astype(int)
    for c, col in ((1, (201, 219, 110)), (2, (94, 155, 76)), (3, (47, 94, 52))):
        a[code == c, :3] = col
    save(a, "new_stage_code.png")
    old_on = rnd.dither(old, dict(d, classesOn=False))          # ss/sandbox-dither behaviour (grain re-dithered)
    imgs = {"old: grain, filter off": old, "ss/sandbox-dither: grain + filter, dot 2": old_on}
    for ds in (2, 4):
        img = rnd.dither(new, dict(d, dotSize=ds))
        imgs["new: flat + filter, dot %d (x1/x2/x4)" % ds] = img
        save(img, "new_filter_dot%d.png" % ds)
    rows = [[label(crop(v, "stages", 3), k)] for k, v in imgs.items()]
    save(sheet(rows), "sheet_stages.png")
    rows = [[label(crop(v, "patch", 4), k.split(":")[0] + (" d" + k[-12] if "dot" in k else ""))
             for k, v in imgs.items()]]
    save(sheet(rows), "sheet_stage_zoom.png")
    rows = [[crop(v, k2) for k2 in ("mound", "water")] for v in imgs.values()]
    save(sheet(rows), "sheet_lines.png")
    rows = []
    for m in (0, 1, 2):
        rows.append([crop(rnd.dither(new, dict(d, mode=m, pattern=pt)), "patch", 2) for pt in (0, 1, 2, 3)])
    save(sheet(rows), "sheet_modes_patterns.png")
    print("wrote frames and sheets to", OUT)


def block_size(img, rect, sizes=(1, 2, 4, 8, 16, 32)):
    """Largest k such that >= 95 % of the k x k blocks (aligned to the
    projector grid) inside rect are one colour; and the uniform share per k."""
    x, y, w, h = rect
    c = img[y:y + h, x:x + w, :3].astype(np.int32)
    key = (c[..., 0] << 16) | (c[..., 1] << 8) | c[..., 2]
    share = {}
    for k in sizes:
        hh, ww = (h // k) * k, (w // k) * k
        if hh < k or ww < k:
            break
        b = key[:hh, :ww].reshape(hh // k, k, ww // k, k)
        uni = (b == b[:, :1, :, :1]).all(axis=(1, 3))
        share[k] = uni.mean()
    best = max([k for k, v in share.items() if v >= 0.95] or [0])
    return best, share


def cmd_stages(args):
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    outs = {"old grain (filter off)": old}
    for ds in (1, 2, 4, 8):
        outs["new, dot %d" % ds] = rnd.dither(new, dict(d, dotSize=ds))
    print("Measured dot (or grain) size per patch: largest k with >= 95 % one-colour k x k blocks")
    print("(projector-aligned, patch interior). Old: the grain cells; new: the filter's dots.")
    print("%-24s %-13s %s" % ("", "", "  ".join("cover %.2f" % c for c in PATCH_COLS)))
    for name, img in outs.items():
        for st in PATCH_ROWS:
            cells = []
            for c in PATCH_COLS:
                k, share = block_size(img, patch_rect(st, c))
                cells.append("%2d px     " % k)
            print("%-24s %-13s %s" % (name, st, "  ".join(cells)))
    # Stage code coverage of the new source in each patch.
    code = 255 - new[..., 3].astype(int)
    print("stage code in the patch interiors (share of pixels with the patch's own stage):")
    for si, st in enumerate(PATCH_ROWS):
        r = []
        for c in PATCH_COLS:
            x, y, w, h = patch_rect(st, c)
            r.append("%.3f" % (code[y:y + h, x:x + w] == si + 1).mean())
        print("  %-13s %s" % (st, "  ".join(r)))


def cmd_identity(args):
    """Filter off: the height shader on this branch against ss/plant-grain's
    (git show), same uniforms, GL3 and GL2, a few settings; bit for bit, RGBA."""
    tmp = tempfile.mkdtemp()
    for v in ("GL2", "GL3"):
        os.makedirs(os.path.join(tmp, v))
        for f in ("heightMapShader.vert", "heightMapShader.frag"):
            src = subprocess.check_output(["git", "-C", REPO, "show", "origin/ss/plant-grain:bin/data/shaders/shaders%s/%s" % (v, f)])
            open(os.path.join(tmp, v, f), "wb").write(src)
    variants = {"defaults": {}, "band fill off": {"bandFill": False}, "contour lines on": {"drawContourLines": True, "contourLineFactor": 0.1},
                "every N mm lines": {"landLineMode": 1}, "plants off": {"plantDisplay": 2}}
    for gl2 in (False, True):
        ref = setup(gl2, height_dir=os.path.join(tmp, "GL2" if gl2 else "GL3"))
        cur = setup(gl2)
        for name, ov in variants.items():
            o = dict(OLD); o.update(ov)
            a = ref[1].sandbox(ref[2], ref[3], o)
            b = cur[1].sandbox(cur[2], cur[3], o)
            diff = np.abs(a.astype(int) - b.astype(int))
            print("%s, %-18s identical to ss/plant-grain: %s (max difference %d, RGBA)"
                  % ("GL2" if gl2 else "GL3", name, bool(diff.max() == 0), diff.max()))


def line_kinds(rnd, box, rd, base):
    full = lum(rnd.sandbox(box, rd, base)[..., :3] / 255.0)
    o = dict(base); o["drawLandLines"] = False
    no_land = lum(rnd.sandbox(box, rd, o)[..., :3] / 255.0)
    o = dict(base); o["drawWaterLines"] = False
    no_water = lum(rnd.sandbox(box, rd, o)[..., :3] / 255.0)
    return np.abs(full - no_land) > 0.06, np.abs(full - no_water) > 0.06


def cmd_lines(args):
    """Share of pixels drawn undithered (protected): output equals the
    pre-filtered source while the unprotected output does not."""
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    for name, frame, sand, base, prm in (("ss/sandbox-dither (grain re-dithered)", old, so, OLD, dict(d, classesOn=False)),
                                          ("new (flat plants, dot classes)", new, sn, NEW, d)):
        land, water = line_kinds(rnd, box, rd, base)
        lines = land | water
        detail = (np.abs(lum(frame[..., :3] / 255.0) - lum(sand[..., :3] / 255.0)) > 0.06) & ~lines
        planted = (box_cover_proj(rnd, box, rd) > 0.04) & ~lines & ~detail
        inside = sand[..., :3].sum(-1) > 0
        plain = inside & ~(lines | detail | planted)
        src8 = np.round(prefilter_np(frame[..., :3] / 255.0, d["exposure"], d["contrast"], d["saturation"]) * 255)
        raw = rnd.dither(frame, dict(prm, lineProtect=0))
        raw_same = np.all(np.abs(raw[..., :3].astype(int) - src8) <= 1, -1)
        print("== %s: land-line px %d, water-line px %d, particles/seeds %d, planted %d, plain %d"
              % (name, land.sum(), water.sum(), detail.sum(), planted.sum(), plain.sum()))
        print("   mode threshold  land    water   particles  planted  plain")
        for lp in (1, 2):
            for th in (0.06, 0.10, 0.15, 0.20):
                img = rnd.dither(frame, dict(prm, lineProtect=lp, lineThreshold=th))
                prot = np.all(np.abs(img[..., :3].astype(int) - src8) <= 1, -1) & ~raw_same
                print("   %d    %.2f      %5.1f%%  %5.1f%%  %5.1f%%     %5.2f%%   %5.2f%%"
                      % (lp, th, 100 * prot[land].mean(), 100 * prot[water].mean(), 100 * prot[detail].mean(),
                         100 * prot[planted].mean(), 100 * prot[plain].mean()))
        if args.save:
            img = rnd.dither(frame, prm)
            prot = np.all(np.abs(img[..., :3].astype(int) - src8) <= 1, -1) & ~raw_same
            vis = (frame[..., :3] * 0.35).astype(np.uint8)
            vis[prot] = [255, 210, 0]
            save(vis, "protected_%s.png" % ("old" if frame is old else "new"))


def box_cover_proj(rnd, box, rd):
    """Plant cover per projector pixel: the stage-code-free way, rendering
    the flat mode with and without plants and taking where it differs."""
    a = rnd.sandbox(box, rd, NEW)
    o = dict(NEW); o["plantDisplay"] = 2
    b = rnd.sandbox(box, rd, o)
    return np.abs(a[..., :3].astype(int) - b[..., :3].astype(int)).max(-1) / 255.0 * 25.0


def cmd_colour(args):
    """Mean colour per patch: the flat source vs the filtered frame (dot 2)."""
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    img = rnd.dither(new, d)

    def hue(c):
        mx, mn = c.max(), c.min()
        if mx - mn < 1e-6:
            return float("nan")
        r, g, b = c
        h = ((g - b) / (mx - mn)) % 6 if mx == r else (b - r) / (mx - mn) + 2 if mx == g else (r - g) / (mx - mn) + 4
        return 60 * h
    print("patch                source     hue src  hue dots  lum src  lum dots   old grain mean")
    for st in PATCH_ROWS:
        for c in PATCH_COLS:
            x, y, w, h = patch_rect(st, c)
            a = new[y:y + h, x:x + w, :3].reshape(-1, 3).mean(0) / 255
            b = img[y:y + h, x:x + w, :3].reshape(-1, 3).mean(0) / 255
            o = old[y:y + h, x:x + w, :3].reshape(-1, 3).mean(0) / 255
            print("%-12s %.2f  #%02X%02X%02X   %6.1f   %6.1f    %.3f    %.3f      #%02X%02X%02X lum %.3f"
                  % (st, c, *np.round(a * 255).astype(int), hue(a), hue(b), lum(a[None])[0], lum(b[None])[0],
                     *np.round(o * 255).astype(int), lum(o[None])[0]))


def gauss(img, s):
    r = int(3 * s)
    k = np.exp(-np.arange(-r, r + 1) ** 2 / (2 * s * s))
    k /= k.sum()
    pad = np.pad(img, ((r, r), (r, r)), mode="edge")
    t = np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 1, pad)
    return np.apply_along_axis(lambda v: np.convolve(v, k, "valid"), 0, t)


def cmd_moire(args):
    """Beat check in the planted patches: low-pass (Gaussian sigma 3 px) error
    of the dots against the flat source, and the period of its strongest
    spectral peak, per dot size; a plain-fill patch of sand for reference.
    A beat shows as error well above the plain fill at a period longer than
    the filter's own (4 dots x the largest dot of the patch's class)."""
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    src = lum(new[..., :3] / 255.0)
    x, y = to_proj(390, 60)
    regions = {"plain sand": (int(x) - 48, int(y) - 32, 96, 64)}
    for st in PATCH_ROWS:
        regions[st + " 0.45"] = patch_rect(st, 0.45, inset=3)
    print("dot  " + "  ".join("%-17s" % n for n in regions) + "   (RMS low-pass error / peak period px)")
    for ds in (1, 2, 3, 4, 5, 6, 8):
        img = rnd.dither(new, dict(d, dotSize=ds, lineProtect=0))
        out = lum(img[..., :3] / 255.0)
        cells = []
        for n, (rx, ry, rw, rh) in regions.items():
            e = gauss(out[ry:ry + rh, rx:rx + rw], 3.0) - gauss(src[ry:ry + rh, rx:rx + rw], 3.0)
            e = e[10:-10, 10:-10]
            spec = np.abs(np.fft.rfft2(e - e.mean()))
            spec[0, 0] = 0
            iy, ix = np.unravel_index(np.argmax(spec), spec.shape)
            fy = (iy if iy <= e.shape[0] // 2 else iy - e.shape[0]) / e.shape[0]
            per = 1.0 / max(math.hypot(ix / e.shape[1], fy), 1e-9)
            cells.append("%.4f / %5.1f" % (np.sqrt((e ** 2).mean()), per))
        print("%d    %s" % (ds, "  ".join("%-17s" % c for c in cells)))


def cmd_compare(args):
    out = []
    for gl2 in (False, True):
        ctx, rnd, box, rd, (so, old), (sn, new) = setup(gl2)
        d = dither_defaults()
        imgs = [so, sn, old, new]
        for m in (0, 1, 2):
            for pt in (0, 1, 2, 3):
                imgs.append(rnd.dither(new, dict(d, mode=m, pattern=pt)))
        for ds in (1, 3, 8):
            imgs.append(rnd.dither(new, dict(d, dotSize=ds)))
        out.append(imgs)
    names = ["height shader old", "height shader new (flat + code)", "composite old", "composite new"] + \
        ["%s/%s" % (MODES[m], PATTERNS[p]) for m in (0, 1, 2) for p in (0, 1, 2, 3)] + ["hue dot 1", "hue dot 3", "hue dot 8"]
    for n, a, b in zip(names, out[0], out[1]):
        diff = np.abs(a.astype(int) - b.astype(int))
        print("GL2 vs GL3, %-32s identical %s (max channel difference %d)" % (n, bool(diff.max() == 0), diff.max()))


def cmd_timing(args):
    ctx, rnd, box, rd, (so, old), (sn, new) = setup(args.gl2)
    d = dither_defaults()
    for ds in (1, 2, 4, 8):
        for classes in (False, True):
            prm = dict(d, dotSize=ds, classesOn=classes)
            rnd.dither(new, prm)
            GL.glFinish()
            n = 10
            t = time.perf_counter()
            for _ in range(n):
                rnd.dither(new, prm)
            GL.glFinish()
            print("dot %d, classes %-5s: %.1f ms per filter (upload + passes + read-back), %s"
                  % (ds, classes, 1000 * (time.perf_counter() - t) / n, GL.glGetString(GL.GL_RENDERER).decode()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["frames", "stages", "identity", "lines", "colour", "moire", "compare", "timing"])
    ap.add_argument("--gl2", action="store_true")
    ap.add_argument("--save", action="store_true", help="lines: also write out/protected_*.png")
    ap.add_argument("--fill-off", action="store_true", help="Band fill off (dark-room look)")
    a = ap.parse_args()
    globals()["FILL_OFF"] = a.fill_off
    globals()["cmd_" + a.cmd](a)


if __name__ == "__main__":
    main()
