#!/usr/bin/env python3
"""Headless preview and checks for Prototype 4 ("Scene").

Runs the module's own shader files (bin/data/shaders/scene/GL3, or GL2
with --gl2) in an offscreen OpenGL context (EGL, as tools/ascii_preview),
in the order of SceneRenderer::update(): mock terrain -> display height
(EMA + Gaussian) -> RippleField -> sceneRender -> sceneWarp (the grid mesh
through a stand-in calibration) -> SceneStylizer (sceneCells,
sceneStylize). Defaults are parsed from the .cpp files under
src/Prototypes/Scene; the font atlas and its coverage-sorted ramp follow
AsciiFontAtlas / SceneStylizer (Pillow, FreeType).

    python3 scene_preview.py styles     # one frame in each style + zooms
    python3 scene_preview.py sequence   # frames + GIF: wind, dapple, the
                                        # mock hand digging beside the water, drops
    python3 scene_preview.py noise      # +-1 mm per frame on flat mock terrain, 60 s
    python3 scene_preview.py compare    # GL2 vs GL3
    python3 scene_preview.py timing     # per-pass time on this (software) GL
    python3 scene_preview.py identity   # succession / water bit-identical with Scene reading (needs build.sh)

Needs numpy, Pillow, PyOpenGL and an EGL driver (Mesa: surfaceless).
"""

import argparse
import ctypes
import math
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "ascii_preview"))
import ascii_preview as A  # Context, atlas, Sandbox (EGL set up there)

import numpy as np
from PIL import Image
from OpenGL import GL

REPO = A.REPO
SHADERS = os.path.join(REPO, "bin", "data", "shaders", "scene")
SRC = os.path.join(REPO, "src", "Prototypes", "Scene")
OUT = os.path.join(HERE, "out")
R = GL.GL_TEXTURE_RECTANGLE
LUT_STEP = 4


# ------------------------------------------------------------ defaults
def read_defaults():
    """Every static default in src/Prototypes/Scene/*.cpp, by name."""
    d = {}
    for f in ("SceneRenderer.cpp", "SceneHeightInput.cpp", "RippleField.cpp", "SceneStylizer.cpp"):
        s = open(os.path.join(SRC, f)).read()
        for m in re.finditer(r"^(float|int|bool) \w+::(\w+) = ([^;]+);", s, re.M):
            t, n, v = m.groups()
            v = v.strip()
            if t == "bool":
                d[n] = v == "true"
            elif re.match(r"^-?[0-9.]+f?$", v):
                d[n] = float(v.rstrip("f")) if t == "float" else int(v)
            else:  # enum constant, e.g. SceneStylizer::STYLE_ASCII
                d[n] = v
        for m in re.finditer(r"^ofFloatColor \w+::(\w+) = hexColor\(0x([0-9A-Fa-f]{6})\);", s, re.M):
            d[m.group(1)] = m.group(2)
        m = re.search(r'std::string SceneStylizer::RAMP = "((?:[^"\\]|\\.)*)";', s)
        if m:
            d["RAMP"] = m.group(1).encode().decode("unicode_escape")
        m = re.search(r"ofFloatColor SceneStylizer::PALETTE\[3\] = \{(.*?)\};", s, re.S)
        if m:
            d["PALETTE"] = re.findall(r"hexColor\(0x([0-9A-Fa-f]{6})\)", m.group(1))
    enums = {"SceneStylizer::STYLE_ASCII": 1, "SceneStylizer::STYLE_OFF": 0, "SceneStylizer::STYLE_DITHER": 2,
             "SceneStylizer::STYLE_BOTH": 3, "SceneStylizer::PALETTE_GOLD": 0}
    for k, v in list(d.items()):
        if isinstance(v, str) and v in enums:
            d[k] = enums[v]
    return d


def rgb(h):
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


PALETTES = {0: ["1A1206", "9C7A2E", "F2D58A"], 1: ["05070D", "2E4466", "C9D6E8"], 2: ["0B2A1E", "2F7A55", "CFE8D2"]}


# ------------------------------------------------------------ GL helpers
def tex(w, h, internal, fmt=GL.GL_RGBA, typ=GL.GL_FLOAT, data=None, linear=False):
    t = GL.glGenTextures(1)
    GL.glBindTexture(R, t)
    f = GL.GL_LINEAR if linear else GL.GL_NEAREST
    GL.glTexParameteri(R, GL.GL_TEXTURE_MIN_FILTER, f)
    GL.glTexParameteri(R, GL.GL_TEXTURE_MAG_FILTER, f)
    GL.glTexParameteri(R, GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE)
    GL.glTexParameteri(R, GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)
    GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
    GL.glTexImage2D(R, 0, internal, w, h, 0, fmt, typ, data)
    return t


class Target:
    """A rectangle texture with its FBO."""

    def __init__(self, w, h, internal, linear=False):
        self.w, self.h = w, h
        self.tex = tex(w, h, internal, linear=linear)
        self.fbo = A.fbo_for(self.tex)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def begin(self, clear=(0, 0, 0, 0)):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glViewport(0, 0, self.w, self.h)
        GL.glClearColor(*clear)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read(self, fmt=GL.GL_FLOAT):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, fmt)
        return np.frombuffer(a, np.float32 if fmt == GL.GL_FLOAT else np.uint8).reshape(self.h, self.w, 4)


def program(gl2, frag):
    d = os.path.join(SHADERS, "GL2" if gl2 else "GL3")
    p = GL.glCreateProgram()
    for path, kind in ((os.path.join(d, "scene.vert"), GL.GL_VERTEX_SHADER), (os.path.join(d, frag + ".frag"), GL.GL_FRAGMENT_SHADER)):
        s = GL.glCreateShader(kind)
        GL.glShaderSource(s, open(path).read())
        GL.glCompileShader(s)
        if not GL.glGetShaderiv(s, GL.GL_COMPILE_STATUS):
            raise RuntimeError("%s: %s" % (path, GL.glGetShaderInfoLog(s).decode()))
        GL.glAttachShader(p, s)
    if not gl2:
        GL.glBindAttribLocation(p, 0, "position")
        GL.glBindAttribLocation(p, 3, "texcoord")
    GL.glLinkProgram(p)
    if not GL.glGetProgramiv(p, GL.GL_LINK_STATUS):
        raise RuntimeError("link %s: %s" % (frag, GL.glGetProgramInfoLog(p).decode()))
    return p


class Pipeline:
    """SceneRenderer::update() with the mock terrain."""

    def __init__(self, ctx, gl2, box, d):
        self.ctx, self.gl2, self.box, self.d = ctx, gl2, box, dict(d)
        self.glctx = ctx.create(gl2)
        ctx.make_current(self.glctx)
        if not gl2:
            GL.glBindVertexArray(GL.glGenVertexArrays(1))
        GL.glDisable(GL.GL_BLEND)
        self.p = {n: program(gl2, n) for n in ("sceneMock", "sceneHeightEma", "sceneBlur", "rippleStep", "sceneRender",
                                              "sceneWarp", "sceneCells", "sceneStylize")}
        c, r = box.cols, box.rows
        self.c, self.r = c, r
        self.mm = box.mm_per_cell
        self.mock_raw = Target(c, r, GL.GL_RGBA32F)
        self.mock_water = Target(c, r, GL.GL_RGBA32F, linear=True)
        self.ema = [Target(c, r, GL.GL_RGBA32F) for _ in range(2)]
        self.blurx = Target(c, r, GL.GL_RGBA32F)
        self.disp = [Target(c, r, GL.GL_RGBA32F, linear=True) for _ in range(2)]
        self.rm = max(1, min(4, int(self.d["RIPPLE_MULT"])))
        self.rip = [Target(c * self.rm, r * self.rm, GL.GL_RGBA32F, linear=True) for _ in range(2)]
        self.sm = max(1, min(6, int(self.d["SCENE_MULT"])))
        self.scene = Target(c * self.sm, r * self.sm, GL.GL_RGBA16F, linear=True)
        self.W, self.H = box.W, box.H
        self.warped = Target(self.W, self.H, GL.GL_RGBA16F)
        self.out = Target(self.W, self.H, GL.GL_RGBA8)
        self.cur = self.rcur = 0
        self.reset = True
        self.frame = 0
        self.anim = 0.0
        self.rng = np.random.default_rng(20261118)
        self.mesh = self.build_mesh()
        self.set_cells()

    # -- stylizer cells, atlas, coverage-sorted ramp (SceneStylizer)
    def set_cells(self):
        ch = int(round(self.d["CELL_MM"] * self.box.px_per_mm()))
        atlas, self.cw, self.font = A.build_atlas(ch)
        self.ch = atlas.shape[0]
        cover = []
        for g in range(A.CHAR_COUNT):
            cell = atlas[:, g * self.cw:(g + 1) * self.cw]
            cover.append(float((cell >= 128).mean()))
        uniq = []
        for ch_ in self.d["RAMP"]:
            if 32 <= ord(ch_) <= 126 and ch_ not in uniq and len(uniq) < 32:
                uniq.append(ch_)
        if " " not in uniq:
            uniq[0] = " "
        self.ramp = "".join(sorted(uniq, key=lambda c: cover[A.atlas_index(c)]))  # stable, as std::stable_sort
        rgba = np.zeros(atlas.shape + (4,), np.uint8)
        rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = atlas
        rgba[..., 3] = 255
        self.atlas = tex(atlas.shape[1], atlas.shape[0], GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, rgba)
        self.cols = int(math.ceil(self.W / self.cw))
        self.rows = int(math.ceil(self.H / self.ch))
        self.cells = Target(self.cols, self.rows, GL.GL_RGBA16F)

    def build_mesh(self):
        pos, texc = [], []
        mw = (self.c - 1 + LUT_STEP - 1) // LUT_STEP + 1
        mh = (self.r - 1 + LUT_STEP - 1) // LUT_STEP + 1
        for j in range(mh):
            for i in range(mw):
                gx, gy = min(i * LUT_STEP, self.c - 1), min(j * LUT_STEP, self.r - 1)
                pos.append(self.box.to_proj(gx, gy))
                texc.append(((gx + 0.5) * self.sm, (gy + 0.5) * self.sm))
        idx = []
        for j in range(mh - 1):
            for i in range(mw - 1):
                a = j * mw + i
                idx += [a, a + 1, a + mw, a + 1, a + mw + 1, a + mw]
        return np.array(pos, np.float32), np.array(texc, np.float32), np.array(idx, np.uint32)

    # -- drawing
    def draw(self, prog, pos, texc, idx, w, h, sx=1.0, sy=1.0):
        mvp = A.ortho(w, h, sx, sy)
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
            GL.glDrawElements(GL.GL_TRIANGLES, len(idx), GL.GL_UNSIGNED_INT, idx)
            return
        GL.glUniformMatrix4fv(GL.glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL.GL_FALSE, mvp)
        b = GL.glGenBuffers(3)
        for k, (arr, loc) in enumerate(((pos, 0), (texc, 3))):
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, b[k])
            GL.glBufferData(GL.GL_ARRAY_BUFFER, arr.nbytes, arr, GL.GL_STREAM_DRAW)
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, b[2])
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, idx.nbytes, idx, GL.GL_STREAM_DRAW)
        GL.glDrawElements(GL.GL_TRIANGLES, len(idx), GL.GL_UNSIGNED_INT, None)
        GL.glDeleteBuffers(3, b)

    def quad(self, prog, t):
        q = np.array([[0, 0], [t.w, 0], [0, t.h], [t.w, t.h]], np.float32)
        self.draw(prog, q, q, np.array([0, 1, 2, 1, 3, 2], np.uint32), t.w, t.h)

    def use(self, name, textures, **u):
        p = self.p[name]
        GL.glUseProgram(p)
        for unit, (n, t) in enumerate(textures):
            GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
            GL.glBindTexture(R, t)
            GL.glUniform1i(GL.glGetUniformLocation(p, n), unit)
        for n, v in u.items():
            loc = GL.glGetUniformLocation(p, n)
            if isinstance(v, int) and not isinstance(v, bool):
                GL.glUniform1i(loc, v)
            elif isinstance(v, bool):
                GL.glUniform1i(loc, int(v))
            elif isinstance(v, (tuple, list)) and len(v) in (2, 3) and not isinstance(v[0], (list, tuple)):
                (GL.glUniform2f if len(v) == 2 else GL.glUniform3f)(loc, *[float(x) for x in v])
            elif isinstance(v, np.ndarray):
                if v.ndim == 2:
                    GL.glUniform3fv(loc, v.shape[0], v.astype(np.float32))
                else:
                    GL.glUniform1fv(loc, v.size, v.astype(np.float32))
            else:
                GL.glUniform1f(loc, float(v))
        return p

    # -- one frame
    def step(self, dt, noise_mm=0.0, freeze=None, dig=None, timing=None, live=None):
        """dig: optional (gx, gy, radius_cells, depth_mm) added to the mock
        height this frame (a hand pressing into the sand; display input
        only, as the Kinect would see it). live: (height mm, water RGBA:
        depth mm, vx, vy, d(depth)/dt) uploaded as SceneHeightInput's live
        path does, instead of the mock."""
        self.ctx.make_current(self.glctx)
        d = self.d
        freeze = d["FREEZE"] if freeze is None else freeze
        if not freeze:
            self.anim += dt
        self.frame += 1
        T = time.perf_counter
        tm = {}

        def mark(k, t0):
            if timing is not None:
                GL.glFinish()
                tm[k] = (T() - t0) * 1000

        t0 = T()
        if live is not None:
            hgt, wat = live
            a = np.zeros((self.r, self.c, 4), np.float32)
            a[..., 0] = hgt
            GL.glBindTexture(R, self.mock_raw.tex)
            GL.glTexSubImage2D(R, 0, 0, 0, self.c, self.r, GL.GL_RGBA, GL.GL_FLOAT, np.ascontiguousarray(a))
            GL.glBindTexture(R, self.mock_water.tex)
            GL.glTexSubImage2D(R, 0, 0, 0, self.c, self.r, GL.GL_RGBA, GL.GL_FLOAT, np.ascontiguousarray(wat, np.float32))
        else:
            for part, tgt in ((0, self.mock_raw), (1, self.mock_water)):
                tgt.begin()
                p = self.use("sceneMock", [], part=part, gridSize=(self.c, self.r), mmPerCell=self.mm, time=self.anim, dt=dt,
                             waterLevel=d["MOCK_WATER_LEVEL_MM"], noiseMM=noise_mm, frame=float(self.frame % 100000),
                             handOn=int(d["MOCK_HAND"]), handSpeed=d["MOCK_HAND_SPEED"], flatTerrain=int(d["MOCK_FLAT"]))
                self.quad(p, tgt)
        if dig is not None:
            self.apply_dig(*dig)
        alpha = 1.0 - math.exp(-dt / d["SMOOTH_TIME_SEC"]) if d["SMOOTH_TIME_SEC"] > 0 else 1.0
        nxt = 1 - self.cur
        self.ema[nxt].begin()
        p = self.use("sceneHeightEma", [("rawTex", self.mock_raw.tex), ("prevTex", self.ema[self.cur].tex)],
                     alpha=alpha, resetAll=int(self.reset))
        self.quad(p, self.ema[nxt])
        sig = max(0.0, d["SMOOTH_RADIUS_CELLS"])
        self.blurx.begin()
        p = self.use("sceneBlur", [("srcTex", self.ema[nxt].tex), ("prevTex", self.disp[self.cur].tex)],
                     dir=(1.0, 0.0), sigma=sig, rate=0, dt=dt, resetAll=int(self.reset))
        self.quad(p, self.blurx)
        self.disp[nxt].begin()
        p = self.use("sceneBlur", [("srcTex", self.blurx.tex), ("prevTex", self.disp[self.cur].tex)],
                     dir=(0.0, 1.0), sigma=sig, rate=1, dt=dt, resetAll=int(self.reset))
        self.quad(p, self.disp[nxt])
        self.cur = nxt
        mark("input", t0)

        t0 = T()
        # RippleField: Poisson drops over the grid area, at most 8, uniform positions.
        drops = np.full((8, 2), -100.0, np.float32)
        nd = 0
        if not freeze and d["DROP_RATE"] > 0:
            lam = d["DROP_RATE"] * dt * self.c * self.r * self.mm * self.mm / 1e5
            nd = min(8, int(self.rng.poisson(lam)))
            drops[:nd, 0] = self.rng.uniform(0, self.c * self.rm, nd)
            drops[:nd, 1] = self.rng.uniform(0, self.r * self.rm, nd)
        fdt = min(0.1, max(0.0, dt))
        steps = max(1, min(8, int(round(d["SUBSTEPS"] * fdt * 60.0))))
        damp = 1.0 - math.exp(-max(0.0, d["DAMPING_PER_SEC"]) * fdt / steps)
        for s in range(steps):
            nr = 1 - self.rcur
            self.rip[nr].begin()
            p = self.use("rippleStep", [("prevTex", self.rip[self.rcur].tex), ("heightTex", self.disp[self.cur].tex),
                                        ("waterTex", self.mock_water.tex)],
                         rippleMult=float(self.rm), waterThreshold=d["WATER_THRESHOLD_MM"], c2=min(0.5, d["WAVE_C2"]),
                         damping=damp, maxAmp=max(0.01, d["MAX_AMPLITUDE"]), heightRateThreshold=d["HEIGHT_RATE_THRESHOLD"],
                         waterRateThreshold=d["WATER_RATE_THRESHOLD"],
                         heightGain=d["HEIGHT_GAIN"] if d["SOURCE_HEIGHT"] else 0.0,
                         waterGain=d["WATER_GAIN"] if d["SOURCE_WATER"] else 0.0,
                         impulseCap=d["IMPULSE_CAP"], impulseScale=fdt if s == 0 else 0.0,
                         dropCount=float(nd) if s == 0 else 0.0, dropAmp=d["DROP_AMP"], resetAll=int(self.reset))
            GL.glUniform2fv(GL.glGetUniformLocation(p, "drops"), 8, drops)
            self.quad(p, self.rip[nr])
            self.rcur = nr
        self.reset = False
        mark("ripples", t0)

        t0 = T()
        az, el = math.radians(d["SUN_AZIMUTH_DEG"]), math.radians(min(90, max(1, d["SUN_ELEVATION_DEG"])))
        da, wa, ra = (math.radians(d[k]) for k in ("DAPPLE_DIRECTION_DEG", "WIND_DIRECTION_DEG", "REFLECT_DIRECTION_DEG"))
        self.scene.begin((0, 0, 0, 1))
        p = self.use("sceneRender", [("heightTex", self.disp[self.cur].tex), ("waterTex", self.mock_water.tex),
                                     ("rippleTex", self.rip[self.rcur].tex)],
                     sceneMult=float(self.sm), rippleMult=float(self.rm), gridSize=(self.c, self.r), mmPerCell=self.mm,
                     animTime=self.anim, heightLo=d["HEIGHT_LO_MM"], heightHi=d["HEIGHT_HI_MM"],
                     terrainLow=rgb(d["TERRAIN_LOW"]), terrainMid=rgb(d["TERRAIN_MID"]), terrainHigh=rgb(d["TERRAIN_HIGH"]),
                     terrainMidPos=min(0.99, max(0.01, d["TERRAIN_MID_POS"])), exaggeration=d["EXAGGERATION"],
                     sunDir=(math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)),
                     sunColor=rgb(d["SUN_COLOR"]), ambient=d["AMBIENT"], dappleScale=d["DAPPLE_SCALE_MM"],
                     dappleVelocity=(math.cos(da) * d["DAPPLED_LIGHT_SPEED"], math.sin(da) * d["DAPPLED_LIGHT_SPEED"]),
                     dappleContrast=min(1, max(0.01, d["DAPPLE_CONTRAST"])), dappleShade=d["DAPPLE_SHADE"],
                     bladeLength=d["BLADE_LENGTH_MM"], bladeWidth=d["BLADE_WIDTH_MM"],
                     bladesPerTuft=min(6.0, max(0.0, d["BLADES_PER_TUFT"])), grassMinHeight=d["GRASS_MIN_MM"],
                     grassMaxHeight=d["GRASS_MAX_MM"], grassMaxSlope=max(0.01, d["GRASS_MAX_SLOPE"]),
                     clumpScale=d["CLUMP_SCALE_MM"], clumpAmount=d["CLUMP_AMOUNT"], grassBase=rgb(d["GRASS_BASE"]),
                     grassTip=rgb(d["GRASS_TIP"]), tipBoost=d["TIP_BOOST"], windDir=(math.cos(wa), math.sin(wa)),
                     windStrength=d["WIND_STRENGTH"], windWaveLength=d["WIND_WAVELENGTH_MM"], windSpeed=d["WIND_SPEED"],
                     gustStrength=d["GUST_STRENGTH"], leanSpread=math.radians(d["LEAN_SPREAD_DEG"]),
                     waterThreshold=d["WATER_THRESHOLD_MM"], waterEdge=d["WATER_EDGE_MM"], waterDeepDepth=d["WATER_DEEP_MM"],
                     waterShallow=rgb(d["WATER_SHALLOW"]), waterDeep=rgb(d["WATER_DEEP"]),
                     rippleNormalGain=d["RIPPLE_NORMAL_GAIN"], specStrength=d["SPEC_STRENGTH"], shininess=d["SHININESS"],
                     rippleShimmer=d["RIPPLE_SHIMMER"], highlightMax=max(0.01, d["HIGHLIGHT_MAX"]), reflectStrength=d["REFLECT_STRENGTH"],
                     reflectOffset=(math.cos(ra) * d["REFLECT_DISTANCE_MM"], math.sin(ra) * d["REFLECT_DISTANCE_MM"]),
                     reflectAmp=d["REFLECT_AMP_MM"], reflectFreq=d["REFLECT_FREQ"], reflectSpeed=d["REFLECT_SPEED"],
                     blackPoint=d["BLACK_POINT"], blackSoftness=max(0.0, d["BLACK_SOFTNESS"]))
        self.quad(p, self.scene)
        mark("scene", t0)

        t0 = T()
        self.warped.begin((0, 0, 0, 1))
        p = self.use("sceneWarp", [("sceneTex", self.scene.tex)])
        self.draw(p, *self.mesh, self.W, self.H)
        mark("warp", t0)

        t0 = T()
        self.stylize()
        mark("style", t0)
        if timing is not None:
            timing.append(tm)

    def stylize(self, style=None):
        d = self.d
        style = d["STYLE"] if style is None else style
        if style in (1, 3):
            self.cells.begin()
            p = self.use("sceneCells", [("warpedTex", self.warped.tex)], cellSize=(self.cw, self.ch), projSize=(self.W, self.H))
            self.quad(p, self.cells)
        pal = PALETTES.get(int(d["PALETTE_PRESET"]), d["PALETTE"]) if d.get("_preset_palette") else d["PALETTE"]
        ramp = np.array([A.atlas_index(self.ramp[i]) if i < len(self.ramp) else 0 for i in range(32)], np.float32)
        self.out.begin((0, 0, 0, 1))
        p = self.use("sceneStylize", [("warpedTex", self.warped.tex), ("cellTex", self.cells.tex), ("atlasTex", self.atlas)],
                     style=int(style), cellSize=(self.cw, self.ch), rampGlyph=ramp, rampCount=float(max(2, len(self.ramp))),
                     luminanceGamma=max(0.05, d["LUMINANCE_GAMMA"]), blackPoint=d["BLACK_POINT"],
                     colourBoost=d["COLOUR_BOOST"], dotSize=float(max(1, min(8, int(d["DOT_SIZE"])))),
                     levels=float(max(2, min(16, int(d["LEVELS"])))), bayer8=int(d["BAYER8"]), paletteOn=int(d["PALETTE_ON"]),
                     paletteStops=float(2 if d["PALETTE_STOPS"] <= 2 else 3),
                     paletteColor=np.array([rgb(h) for h in pal], np.float32), bothThreshold=d["BOTH_THRESHOLD"])
        self.quad(p, self.out)

    def apply_dig(self, gx, gy, radius, depth):
        """Subtract a dimple from the mock height texture (CPU round trip;
        the preview's stand-in for a hand in the sand)."""
        a = self.mock_raw.read().copy()
        y, x = np.mgrid[0:self.r, 0:self.c].astype(np.float32)
        dd = np.hypot(x - gx, y - gy)
        a[..., 0] -= depth * np.exp(-0.5 * (dd / radius) ** 2)
        GL.glBindTexture(R, self.mock_raw.tex)
        GL.glTexSubImage2D(R, 0, 0, 0, self.c, self.r, GL.GL_RGBA, GL.GL_FLOAT, np.ascontiguousarray(a))

    def image(self):
        return self.out.read(GL.GL_UNSIGNED_BYTE)[..., :3].copy()

    def warped_image(self):
        w = self.warped.read()[..., :3]
        return (np.clip(w, 0, 1) * 255).astype(np.uint8)


# ------------------------------------------------------------ commands
STYLE_NAMES = {0: "off", 1: "ascii", 2: "dither", 3: "both"}


def save(img, name, zoom=None):
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(img).save(os.path.join(OUT, name + ".png"))
    if zoom:
        x0, y0, w, h = zoom
        Image.fromarray(img[y0:y0 + h, x0:x0 + w]).resize((w * 3, h * 3), Image.NEAREST).save(os.path.join(OUT, name + "_zoom.png"))


ZOOM = (560, 380, 300, 180)  # a crop across the basin's shore and the grass


def cmd_styles(args, ctx, d):
    box = A.Sandbox()
    p = Pipeline(ctx, args.gl2, box, dict(d, MOCK_TERRAIN=True, MOCK_HAND=True))
    for _ in range(90):
        p.step(1 / 30.0)
    print("cell %d x %d px (font %d px, %.1f mm at %.2f px/mm); ramp by ink: %r; scene %d x %d; ripples x%d"
          % (p.cw, p.ch, p.font, d["CELL_MM"], box.px_per_mm(), p.ramp, p.scene.w, p.scene.h, p.rm))
    for st in (0, 1, 2, 3):
        p.stylize(st)
        save(p.image(), "style_" + STYLE_NAMES[st], ZOOM)
    for pr, name in ((0, "gold"), (1, "night"), (2, "green")):
        p.d.update(PALETTE_ON=True, PALETTE_PRESET=pr, _preset_palette=True)
        p.stylize(2)
        save(p.image(), "style_dither_" + name, ZOOM)
    print("wrote", OUT)


def cmd_sequence(args, ctx, d):
    """Frames at 30 fps: wind and dapple (8 s), then the mock hand digging
    beside the water (a sudden press), then drops on still water."""
    box = A.Sandbox()
    p = Pipeline(ctx, args.gl2, box, dict(d, MOCK_TERRAIN=True, MOCK_HAND=False))
    seq = os.path.join(OUT, "sequence")
    os.makedirs(seq, exist_ok=True)
    frames, zooms = [], []
    n = int((args.seconds if args.seconds else 12.0) * 30)
    # Dig: a 20 mm press on the basin's rim at 4 s, held (a hand entering the sand).
    cx, cy = box.cols * 0.66 + 0.13 * box.rows * 1.05, box.rows * 0.68
    pressed = False
    for f in range(n):
        dig = None
        if f >= 120:
            dig = (cx, cy, 6.0, 20.0)   # held: the dimple stays while the "hand" is in
        p.step(1 / 30.0, dig=dig)
        if f % 3 == 0:
            p.stylize(int(args.style))
            img = p.image()
            frames.append(img)
            x0, y0, w, h = ZOOM
            zooms.append(img[y0:y0 + h, x0:x0 + w])
            if f % 30 == 0:
                Image.fromarray(img).save(os.path.join(seq, "frame_%04d.png" % f))
    # GIF of the zoomed crop (3x) and a strip of six frames.
    g = [Image.fromarray(z).resize((z.shape[1] * 2, z.shape[0] * 2), Image.NEAREST) for z in zooms]
    g[0].save(os.path.join(seq, "sequence_%s.gif" % STYLE_NAMES[int(args.style)]), save_all=True, append_images=g[1:],
              duration=100, loop=0)
    picks = [0, 20, 39, 41, 46, 55]
    strip = np.concatenate([np.pad(zooms[min(i, len(zooms) - 1)], ((2, 2), (2, 2), (0, 0)), constant_values=255) for i in picks], 1)
    Image.fromarray(strip).resize((strip.shape[1] * 2, strip.shape[0] * 2), Image.NEAREST).save(
        os.path.join(seq, "strip_%s.png" % STYLE_NAMES[int(args.style)]))
    # Motion between frames 0.1 s apart, before the dig: from the animation.
    diffs = [float(np.any(a != b, -1).mean()) for a, b in zip(frames[:39], frames[1:40])]
    print("before the dig: %.1f %% of pixels change per 0.1 s (wind, dapple, drops)" % (100 * np.mean(diffs)))
    print("wrote", seq)


def flat_run(ctx, gl2, d, seconds, noise, freeze, fps=60):
    """Flat mock terrain (no mound/ridge/basin: water level below it), noise
    +-noise mm per frame, 60 fps; fraction of ASCII cells / dither pixels
    changing per frame, after the first second."""
    box = A.Sandbox()
    dd = dict(d, MOCK_TERRAIN=True, MOCK_FLAT=True, MOCK_HAND=False, MOCK_WATER_LEVEL_MM=-100.0, FREEZE=freeze)
    p = Pipeline(ctx, gl2, box, dd)
    p._flat = True
    res = {1: [], 2: []}
    prev = {1: None, 2: None}
    n = int(seconds * fps)
    for f in range(n):
        p.step(1.0 / fps, noise_mm=noise)
        for st in (1, 2):
            p.stylize(st)
            img = p.image()
            if prev[st] is not None and f >= fps:
                ch = np.any(img != prev[st], -1)
                if st == 1:
                    # per cell: any pixel of the cell changed
                    cc = ch[:p.rows * p.ch, :p.cols * p.cw] if ch.shape[0] >= p.rows * p.ch else ch
                    hh, ww = cc.shape[0] // p.ch, cc.shape[1] // p.cw
                    cells = cc[:hh * p.ch, :ww * p.cw].reshape(hh, p.ch, ww, p.cw).any((1, 3))
                    res[1].append(float(cells.mean()))
                else:
                    res[2].append(float(ch.mean()))
            prev[st] = img
    return res


def cmd_noise(args, ctx, d):
    """Main run: Freeze on, +-1 mm, 60 s (args.seconds). Then 10 s each:
    Freeze on without noise, and animation on with and without noise."""
    secs = args.seconds or (10.0 if args.quick else 60.0)
    if args.scene_mult:
        d = dict(d, SCENE_MULT=args.scene_mult)
    print("Flat mock terrain (\"Mock flat\": sand at 9 mm, no water), 60 fps; scene x%d; "
          "smoothing radius %.1f cells, time %.2f s" % (int(d["SCENE_MULT"]), d["SMOOTH_RADIUS_CELLS"], d["SMOOTH_TIME_SEC"]))
    for freeze, noise, s in ((True, 1.0, secs), (True, 0.0, min(secs, 10.0)), (False, 1.0, min(secs, 10.0)),
                             (False, 0.0, min(secs, 10.0))):
        r = flat_run(ctx, args.gl2, d, s, noise, freeze)
        print("  %4.0f s  freeze %-5s noise +-%.0f mm: ASCII cells changing per frame mean %.5f %% (max %.5f %%, "
              "frames with any change %d of %d); dither pixels mean %.5f %% (max %.5f %%)"
              % (s, freeze, noise, 100 * np.mean(r[1]), 100 * np.max(r[1]), int(np.sum(np.array(r[1]) > 0)), len(r[1]),
                 100 * np.mean(r[2]), 100 * np.max(r[2])), flush=True)


def cmd_compare(args, ctx, d):
    box = A.Sandbox()
    res = {}
    if args.scene_mult:
        d = dict(d, SCENE_MULT=args.scene_mult)
    for gl2 in (False, True):
        p = Pipeline(ctx, gl2, box, dict(d, MOCK_TERRAIN=True, MOCK_HAND=True, DROP_RATE=0.0))
        for f in range(60):
            p.step(1 / 30.0, dig=(box.cols * 0.75, box.rows * 0.7, 6.0, 20.0) if f > 30 else None)
        imgs = []
        for st in (0, 1, 2, 3):
            p.stylize(st)
            imgs.append(p.image())
        res[gl2] = imgs
    for st, a, b in zip((0, 1, 2, 3), res[False], res[True]):
        diff = np.abs(a.astype(int) - b.astype(int))
        print("GL2 vs GL3, style %-6s identical %s (max channel difference %d, pixels differing %.4f %%)"
              % (STYLE_NAMES[st], np.array_equal(a, b), diff.max(), 100 * np.any(diff > 0, -1).mean()))


def cmd_timing(args, ctx, d):
    box = A.Sandbox()
    p = Pipeline(ctx, args.gl2, box, dict(d, MOCK_TERRAIN=True, MOCK_HAND=True))
    tm = []
    for _ in range(20):
        p.step(1 / 30.0, timing=tm)
    keys = list(tm[0].keys())
    print("software GL (Mesa llvmpipe), %d x %d projector, scene %d x %d, ripples %d x %d, mean of 15 frames:"
          % (box.W, box.H, p.scene.w, p.scene.h, p.rip[0].w, p.rip[0].h))
    for k in keys:
        print("  %-8s %8.1f ms" % (k, np.mean([t[k] for t in tm[5:]])))


def cmd_identity(args, ctx, d):
    """Succession and water outputs with the Scene pipeline reading them every
    frame (the live inputs: elevation, water depth and velocity) vs not."""
    sys.path.insert(0, os.path.join(HERE, "..", "ascii_preview"))
    lib = os.path.join(HERE, "libecosim.so")
    if not os.path.exists(lib):
        raise SystemExit("run tools/scene_preview/build.sh first")
    L = ctypes.CDLL(lib)
    P = ctypes.c_void_p
    L.eco_new.restype = P
    L.eco_new.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_float, ctypes.c_uint, ctypes.c_char_p, ctypes.c_float,
                          ctypes.c_float, ctypes.c_float]
    L.eco_frame.argtypes = [P, ctypes.c_float, ctypes.c_float, ctypes.c_int]
    for f in ("eco_bed", "eco_elevation", "eco_water_depth", "eco_water_state", "eco_vel_x", "eco_vel_y", "eco_soil", "eco_moisture"):
        getattr(L, f).restype = ctypes.POINTER(ctypes.c_float)
        getattr(L, f).argtypes = [P]
    L.eco_cover.restype = ctypes.POINTER(ctypes.c_float)
    L.eco_cover.argtypes = [P, ctypes.c_int]
    L.eco_range_mm.restype = ctypes.c_float
    L.eco_range_mm.argtypes = [P]
    box = A.Sandbox()
    W, H = box.cols, box.rows
    preset = os.path.join(REPO, "bin", "data", "settings", "ecologyParamsDesigner.xml").encode()
    v = open(os.path.join(REPO, "src", "Games", "VegetationField.cpp")).read()
    sp = [float(re.search(r"^float VegetationField::%s_GROWTH_CHANCE_PCT = ([0-9.]+)f;" % k, v, re.M).group(1)) / 100
          for k in ("FRUIT", "NUT", "SHRUB")]
    secs = 20.0 if args.quick else 120.0
    outs = []
    for reading in (False, True):
        h = L.eco_new(W, H, box.mm_per_cell, 33, preset, *sp)
        arr = lambda f, *a: np.ctypeslib.as_array(getattr(L, f)(h, *a), shape=(H, W))
        bed = arr("eco_bed")
        y, x = np.mgrid[0:H, 0:W].astype(np.float32)
        centre = H * 0.5 + 40.0 * np.sin(x / W * 2 * np.pi)
        t = np.clip((np.abs(y - centre) - 7) / 6, 0, 1)
        bed[:] = 9.0 * (t * t * (3 - 2 * t)) + (-20.0 - 8.0 * x / (W - 1)) * (1 - t * t * (3 - 2 * t))
        p = None
        if reading:
            p = Pipeline(ctx, args.gl2, box, dict(d, MOCK_TERRAIN=True, SCENE_MULT=args.scene_mult or d["SCENE_MULT"]))
            rng_mm = L.eco_range_mm(h)
            prev_depth = np.zeros((H, W), np.float32)
        for f in range(int(secs * 40)):
            L.eco_frame(h, 1 / 40.0, 1.0, 1)
            if reading:
                # SceneHeightInput's live reads (copies): elevation, depth x range, velocity.
                elev = arr("eco_elevation").copy()
                depth = np.maximum(arr("eco_water_depth"), 0).copy() * rng_mm
                wat = np.stack([depth, arr("eco_vel_x").copy(), arr("eco_vel_y").copy(),
                                (depth - prev_depth) * 40.0], -1)
                prev_depth = depth
                p.step(1 / 40.0, live=(elev, wat))
        names = ["eco_water_depth", "eco_water_state", "eco_vel_x", "eco_vel_y", "eco_soil", "eco_moisture"]
        outs.append([arr(n).copy() for n in names] + [arr("eco_cover", r).copy() for r in range(3)])
        L.eco_free.argtypes = [P]
        L.eco_free(h)
    labels = ["water depth", "water state", "vel x", "vel y", "soil", "moisture", "cover P", "cover I", "cover C"]
    same = [a.tobytes() == b.tobytes() for a, b in zip(*outs)]
    for n, s in zip(labels, same):
        print("  %-12s bit-identical: %s" % (n, s))
    print("%g s, Scene pipeline reading vs off: %s" % (secs, "PASS" if all(same) else "FAIL"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["styles", "sequence", "noise", "compare", "timing", "identity"])
    ap.add_argument("--gl2", action="store_true")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--style", type=int, default=1, help="sequence: 0 off, 1 ascii, 2 dither, 3 both")
    ap.add_argument("--seconds", type=float, default=0.0)
    ap.add_argument("--scene-mult", type=int, default=0, help="override SCENE_MULT (noise, compare, identity)")
    args = ap.parse_args()
    d = read_defaults()
    ctx = A.Context()
    return {"styles": cmd_styles, "sequence": cmd_sequence, "noise": cmd_noise, "compare": cmd_compare,
            "timing": cmd_timing, "identity": cmd_identity}[args.command](args, ctx, d) or 0


if __name__ == "__main__":
    sys.exit(main())
