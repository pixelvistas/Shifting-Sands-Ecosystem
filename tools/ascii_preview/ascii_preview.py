#!/usr/bin/env python3
"""Headless preview and noise test for Prototype 3 ("ASCII").

Runs the module's own shader files (bin/data/shaders/ascii/GL3, and GL2
with --gl2) in an offscreen OpenGL context on a synthetic heightmap, with
the same cell sizing, ramp, Bayer dithering, smoothing and hysteresis as
AsciiRenderer / AsciiGlyphRenderer. The defaults are read from those .cpp
files, so the tool cannot drift from them. The font atlas is built with
Pillow (the same FreeType rasteriser as ofTrueTypeFont) following
AsciiFontAtlas::build().

    python3 ascii_preview.py preview      # writes out/preview*.png
    python3 ascii_preview.py noise-test   # +-1 mm noise, flat sand, 60 s
    python3 ascii_preview.py compare      # GL2 and GL3 give the same glyphs

Needs numpy, Pillow and PyOpenGL, and an EGL driver that can create an
offscreen context (Mesa: EGL_PLATFORM=surfaceless, set below). Not part of
the openFrameworks build.
"""

import argparse
import ctypes
import math
import os
import re
import sys
import time

os.environ.setdefault("PYOPENGL_PLATFORM", "egl")
os.environ.setdefault("EGL_PLATFORM", "surfaceless")

import numpy as np
from PIL import Image, ImageFont
from OpenGL import EGL, GL

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SHADERS = os.path.join(REPO, "bin", "data", "shaders", "ascii")
FONT = os.path.join(REPO, "bin", "data", "ascii", "fonts", "JetBrainsMono-Bold.ttf")
SRC = os.path.join(REPO, "src", "Prototypes", "Ascii")
OUT = os.path.join(HERE, "out")

FIRST_CHAR, CHAR_COUNT, MAX_RAMP = 32, 95, 16
REFERENCE_SIZE = 64
LUT_STEP = 4


# ------------------------------------------------------------ defaults
def read_defaults():
    """The live defaults, parsed from the module's .cpp files."""
    g = open(os.path.join(SRC, "AsciiGlyphRenderer.cpp")).read()
    r = open(os.path.join(SRC, "AsciiRenderer.cpp")).read()

    def num(text, name):
        m = re.search(r"AsciiGlyphRenderer::%s = ([-0-9.]+)f;|AsciiRenderer::%s = ([-0-9.]+)f;" % (name, name), text)
        return float(m.group(1) or m.group(2))

    ramp = re.search(r'std::string AsciiGlyphRenderer::RAMP = "((?:[^"\\]|\\.)*)";', g).group(1)
    ramp = ramp.encode().decode("unicode_escape")
    dither = re.search(r"bool AsciiGlyphRenderer::DITHER = (true|false);", g).group(1) == "true"
    return dict(ramp=ramp, smoothing=num(g, "SMOOTHING_SEC"), hysteresis=num(g, "HYSTERESIS_STEPS"),
                dither=dither, brightness=num(g, "BRIGHTNESS"), glyph_mm=num(r, "GLYPH_MM"))


# ------------------------------------------------------------ atlas
def font_metrics(font):
    """AsciiFontAtlas::measure(): ink extent from the baseline (y down) and advance."""
    top, bottom = None, None
    for c in range(FIRST_CHAR + 1, FIRST_CHAR + CHAR_COUNT):
        l, t, rr, b = font.getbbox(chr(c), anchor="ls")
        if b - t <= 0:
            continue
        top = t if top is None else min(top, t)
        bottom = b if bottom is None else max(bottom, b)
    w10 = font.getbbox("M" * 10, anchor="ls")
    w1 = font.getbbox("M", anchor="ls")
    advance = ((w10[2] - w10[0]) - (w1[2] - w1[0])) / 9.0
    return top, bottom, advance


def build_atlas(cell_h):
    """AsciiFontAtlas::build(): returns (coverage uint8 [cell_h, cell_w*95], cell_w, font_size)."""
    cell_h = max(4, cell_h)
    t, b, _ = font_metrics(ImageFont.truetype(FONT, REFERENCE_SIZE))
    ink_ref = max(1.0, b - t)
    size = max(3, int(math.floor(REFERENCE_SIZE * cell_h / ink_ref)))
    font = ImageFont.truetype(FONT, size)
    t, b, adv = font_metrics(font)
    cell_w = max(2, int(round(adv)))
    baseline = round(-t + 0.5 * (cell_h - (b - t)))
    img = Image.new("L", (cell_w * CHAR_COUNT, cell_h), 0)
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    for i in range(1, CHAR_COUNT):
        d.text((i * cell_w, baseline), chr(FIRST_CHAR + i), fill=255, font=font, anchor="ls")
    return np.array(img), cell_w, size


def atlas_index(c):
    i = ord(c) - FIRST_CHAR
    return i if 0 <= i < CHAR_COUNT else 0


# ------------------------------------------------------------ GL context
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
        return c

    def make_current(self, c):
        EGL.eglMakeCurrent(self.display, self.surface, self.surface, c)


R = GL.GL_TEXTURE_RECTANGLE


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


def compile_program(gl2, frag):
    d = os.path.join(SHADERS, "GL2" if gl2 else "GL3")
    p = GL.glCreateProgram()
    for path, kind in ((os.path.join(d, "ascii.vert"), GL.GL_VERTEX_SHADER), (os.path.join(d, frag), GL.GL_FRAGMENT_SHADER)):
        s = GL.glCreateShader(kind)
        GL.glShaderSource(s, open(path).read())
        GL.glCompileShader(s)
        if not GL.glGetShaderiv(s, GL.GL_COMPILE_STATUS):
            raise RuntimeError("%s: %s" % (path, GL.glGetShaderInfoLog(s).decode()))
        GL.glAttachShader(p, s)
    if not gl2:  # openFrameworks' attribute locations
        GL.glBindAttribLocation(p, 0, "position")
        GL.glBindAttribLocation(p, 3, "texcoord")
    GL.glLinkProgram(p)
    if not GL.glGetProgramiv(p, GL.GL_LINK_STATUS):
        raise RuntimeError("link %s: %s" % (frag, GL.glGetProgramInfoLog(p).decode()))
    return p


def ortho(w, h, sx=1.0, sy=1.0):
    """Projection x scale, column-major: x, y (y down) -> texel row y (oF FBO convention)."""
    m = np.array([[2.0 * sx / w, 0, 0, -1], [0, 2.0 * sy / h, 0, -1], [0, 0, -1, 0], [0, 0, 0, 1]], np.float32)
    return m.T.copy()


class Pipeline:
    """AsciiGlyphRenderer's three passes, on one GL context."""

    def __init__(self, ctx, gl2, proj_w, proj_h, cell_h, settings):
        self.ctx, self.gl2 = ctx, gl2
        self.glctx = ctx.create(gl2)
        ctx.make_current(self.glctx)
        self.s = dict(settings)
        self.W, self.H = proj_w, proj_h
        atlas, self.cw, self.font_size = build_atlas(cell_h)
        self.ch = atlas.shape[0]
        self.cols, self.rows = int(math.ceil(proj_w / self.cw)), int(math.ceil(proj_h / self.ch))
        rgba = np.zeros(atlas.shape + (4,), np.uint8)
        rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = atlas
        rgba[..., 3] = 255
        self.atlas = rect_tex(atlas.shape[1], atlas.shape[0], GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, rgba)
        self.sample_tex = rect_tex(self.cols, self.rows, GL.GL_RGBA32F, GL.GL_RGBA, GL.GL_FLOAT)
        self.state_tex = [rect_tex(self.cols, self.rows, GL.GL_RGBA32F, GL.GL_RGBA, GL.GL_FLOAT) for _ in range(2)]
        self.out_tex = rect_tex(proj_w, proj_h, GL.GL_RGBA8, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        self.sample_fbo = fbo_for(self.sample_tex)
        self.state_fbo = [fbo_for(t) for t in self.state_tex]
        self.out_fbo = fbo_for(self.out_tex)
        for f in self.state_fbo:
            GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, f)
            GL.glClearColor(0, 0, 0, 0)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        self.p_sample = compile_program(gl2, "asciiSample.frag")
        self.p_state = compile_program(gl2, "asciiState.frag")
        self.p_glyph = compile_program(gl2, "asciiGlyph.frag")
        self.field_tex = None
        self.current = 0
        self.reset = True
        if not gl2:
            self.vao = GL.glGenVertexArrays(1)
            GL.glBindVertexArray(self.vao)
        GL.glDisable(GL.GL_BLEND)

    # -- geometry
    def _draw(self, pos, tex, idx, mvp, prog):
        pos = np.ascontiguousarray(pos, np.float32)
        tex = np.ascontiguousarray(tex, np.float32)
        idx = np.ascontiguousarray(idx, np.uint32)
        if self.gl2:
            GL.glMatrixMode(GL.GL_PROJECTION)
            GL.glLoadMatrixf(mvp)
            GL.glMatrixMode(GL.GL_MODELVIEW)
            GL.glLoadIdentity()
            GL.glEnableClientState(GL.GL_VERTEX_ARRAY)
            GL.glEnableClientState(GL.GL_TEXTURE_COORD_ARRAY)
            GL.glVertexPointer(2, GL.GL_FLOAT, 0, pos)
            GL.glTexCoordPointer(2, GL.GL_FLOAT, 0, tex)
            GL.glDrawElements(GL.GL_TRIANGLES, len(idx), GL.GL_UNSIGNED_INT, idx)
            return
        GL.glUniformMatrix4fv(GL.glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL.GL_FALSE, mvp)
        bufs = GL.glGenBuffers(3)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, bufs[0])
        GL.glBufferData(GL.GL_ARRAY_BUFFER, pos.nbytes, pos, GL.GL_STREAM_DRAW)
        GL.glEnableVertexAttribArray(0)
        GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
        GL.glBindBuffer(GL.GL_ARRAY_BUFFER, bufs[1])
        GL.glBufferData(GL.GL_ARRAY_BUFFER, tex.nbytes, tex, GL.GL_STREAM_DRAW)
        GL.glEnableVertexAttribArray(3)
        GL.glVertexAttribPointer(3, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, bufs[2])
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, idx.nbytes, idx, GL.GL_STREAM_DRAW)
        GL.glDrawElements(GL.GL_TRIANGLES, len(idx), GL.GL_UNSIGNED_INT, None)
        GL.glDeleteBuffers(3, bufs)

    @staticmethod
    def _quad(w, h):
        p = np.array([[0, 0], [w, 0], [0, h], [w, h]], np.float32)
        return p, p.copy(), np.array([0, 1, 2, 1, 3, 2], np.uint32)

    def _bind(self, prog, unit, name, tex):
        GL.glActiveTexture(GL.GL_TEXTURE0 + unit)
        GL.glBindTexture(R, tex)
        GL.glUniform1i(GL.glGetUniformLocation(prog, name), unit)

    # -- one frame
    def set_mesh(self, pos, tex, idx):
        self.mesh = (pos, tex, idx)

    def step(self, field, dt, draw_glyphs=True):
        """field: float32 [rows, cols], 0..1, < 0 = no data (AsciiRenderer::uploadField)."""
        self.ctx.make_current(self.glctx)
        s = self.s
        levels = max(2, min(MAX_RAMP, len(s["ramp"])))
        fr, fc = field.shape
        if self.field_tex is None:
            self.field_tex = rect_tex(fc, fr, GL.GL_R32F, GL.GL_RED, GL.GL_FLOAT)
        GL.glBindTexture(R, self.field_tex)
        GL.glTexSubImage2D(R, 0, 0, 0, fc, fr, GL.GL_RED, GL.GL_FLOAT, np.ascontiguousarray(field, np.float32))
        alpha = 1.0 - math.exp(-max(0.0, dt) / s["smoothing"]) if s["smoothing"] > 0 else 1.0

        # 1. sample
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.sample_fbo)
        GL.glViewport(0, 0, self.cols, self.rows)
        GL.glClearColor(0, 0, 0, 0)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        GL.glUseProgram(self.p_sample)
        self._bind(self.p_sample, 0, "fieldTex", self.field_tex)
        self._draw(*self.mesh, ortho(self.cols, self.rows, 1.0 / self.cw, 1.0 / self.ch), self.p_sample)

        # 2. state (ping-pong)
        nxt = 1 - self.current
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.state_fbo[nxt])
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        p = self.p_state
        GL.glUseProgram(p)
        self._bind(p, 0, "sampleTex", self.sample_tex)
        self._bind(p, 1, "prevTex", self.state_tex[self.current])
        GL.glUniform1f(GL.glGetUniformLocation(p, "alpha"), alpha)
        GL.glUniform1f(GL.glGetUniformLocation(p, "levels"), float(levels))
        GL.glUniform1f(GL.glGetUniformLocation(p, "hysteresis"), max(0.0, s["hysteresis"]))
        GL.glUniform1i(GL.glGetUniformLocation(p, "dither"), 1 if s["dither"] else 0)
        GL.glUniform1i(GL.glGetUniformLocation(p, "resetAll"), 1 if self.reset else 0)
        self._draw(*self._quad(self.cols, self.rows), ortho(self.cols, self.rows), p)
        self.current = nxt
        self.reset = False
        if not draw_glyphs:
            return

        # 3. glyphs
        ramp = [float(atlas_index(s["ramp"][i] if i < len(s["ramp"]) else " ")) for i in range(MAX_RAMP)]
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.out_fbo)
        GL.glViewport(0, 0, self.W, self.H)
        GL.glClearColor(0, 0, 0, 1)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)
        p = self.p_glyph
        GL.glUseProgram(p)
        self._bind(p, 0, "stateTex", self.state_tex[self.current])
        self._bind(p, 1, "atlasTex", self.atlas)
        GL.glUniform2f(GL.glGetUniformLocation(p, "cellSize"), float(self.cw), float(self.ch))
        GL.glUniform1fv(GL.glGetUniformLocation(p, "rampGlyph"), MAX_RAMP, np.array(ramp, np.float32))
        GL.glUniform1f(GL.glGetUniformLocation(p, "brightness"), min(1.0, max(0.0, s["brightness"])))
        self._draw(*self._quad(self.W, self.H), ortho(self.W, self.H), p)

    def read_state(self):
        self.ctx.make_current(self.glctx)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.state_fbo[self.current])
        a = GL.glReadPixels(0, 0, self.cols, self.rows, GL.GL_RGBA, GL.GL_FLOAT)
        return np.frombuffer(a, np.float32).reshape(self.rows, self.cols, 4)

    def read_out(self):
        self.ctx.make_current(self.glctx)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.out_fbo)
        a = GL.glReadPixels(0, 0, self.W, self.H, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
        return np.frombuffer(a, np.uint8).reshape(self.H, self.W, 4)[..., :3].copy()


# ------------------------------------------------------------ synthetic sandbox
class Sandbox:
    """A Kinect grid (as VegetationField's) placed on the projector through a
    mildly rotated, keystoned mapping (stand-in for kinectCoordToProjCoord)."""

    def __init__(self, cols=519, rows=295, area_mm=1000.0, proj=(1280, 800)):
        self.cols, self.rows = cols, rows
        self.mm_per_cell = area_mm / cols
        self.W, self.H = proj
        self.scale = 0.9 * min(self.W / cols, self.H / rows)
        self.angle = math.radians(1.5)

    def to_proj(self, gx, gy):
        x = (gx - self.cols / 2.0) * self.scale
        y = (gy - self.rows / 2.0) * self.scale
        k = 1.0 + 0.04 * (gy / self.rows - 0.5)  # slight keystone
        x *= k
        c, s = math.cos(self.angle), math.sin(self.angle)
        return self.W / 2.0 + c * x - s * y, self.H / 2.0 + s * x + c * y

    def px_per_mm(self):
        """AsciiRenderer::measureScale()."""
        w, h = self.cols, self.rows
        a = np.array(self.to_proj(0.25 * w, 0.5 * h)); b = np.array(self.to_proj(0.75 * w, 0.5 * h))
        c = np.array(self.to_proj(0.5 * w, 0.25 * h)); d = np.array(self.to_proj(0.5 * w, 0.75 * h))
        sx = np.linalg.norm(b - a) / (0.5 * w * self.mm_per_cell)
        sy = np.linalg.norm(d - c) / (0.5 * h * self.mm_per_cell)
        return 0.5 * (sx + sy)

    def mesh(self):
        """AsciiRenderer::rebuildMesh()."""
        mw = (self.cols - 1 + LUT_STEP - 1) // LUT_STEP + 1
        mh = (self.rows - 1 + LUT_STEP - 1) // LUT_STEP + 1
        pos, tex = [], []
        for j in range(mh):
            for i in range(mw):
                gx, gy = min(i * LUT_STEP, self.cols - 1), min(j * LUT_STEP, self.rows - 1)
                pos.append(self.to_proj(gx, gy))
                tex.append((gx + 0.5, gy + 0.5))
        idx = []
        for j in range(mh - 1):
            for i in range(mw - 1):
                a = j * mw + i; b = a + 1; c = a + mw; d = c + 1
                idx += [a, b, c, b, d, c]
        return np.array(pos, np.float32), np.array(tex, np.float32), np.array(idx, np.uint32)

    def heightmap(self):
        """Flat sand at 9 mm, a mound, a dug basin, and a long even slope."""
        gy, gx = np.mgrid[0:self.rows, 0:self.cols].astype(np.float32)
        e = np.full((self.rows, self.cols), 9.0, np.float32)
        e += 62.0 * np.exp(-((gx - 120) ** 2 + (gy - 150) ** 2) / (2 * 42.0 ** 2))
        e -= 45.0 * np.exp(-((gx - 290) ** 2 + (gy - 110) ** 2) / (2 * 38.0 ** 2))
        ramp = (gx >= 370) & (gx < 500)
        e[ramp] = (-40.0 + 110.0 * (gy[ramp] / (self.rows - 1))).astype(np.float32)
        return e


def normalize(e_mm, ceiling):
    """AsciiRenderer::uploadField(): over -/+ the calibrated ceiling."""
    lo, hi = -abs(ceiling), abs(ceiling)
    return np.clip((e_mm - lo) / max(1e-3, hi - lo), 0.0, 1.0).astype(np.float32)


def make_pipeline(ctx, gl2, box, settings):
    cell_h = int(round(settings["glyph_mm"] * box.px_per_mm()))
    p = Pipeline(ctx, gl2, box.W, box.H, cell_h, settings)
    p.set_mesh(*box.mesh())
    return p


# ------------------------------------------------------------ commands
def cmd_preview(args, ctx, settings):
    box = Sandbox()
    rng = np.random.default_rng(1)
    base = box.heightmap()
    os.makedirs(OUT, exist_ok=True)
    variants = [("preview", settings), ("preview_nodither", dict(settings, dither=False))]
    for name, s in variants:
        p = make_pipeline(ctx, args.gl2, box, s)
        for _ in range(90):  # 1.5 s of +-1 mm noise
            p.step(normalize(base + rng.uniform(-1, 1, base.shape).astype(np.float32), args.ceiling), 1 / 60.0)
        img = p.read_out()
        Image.fromarray(img).save(os.path.join(OUT, name + ".png"))
        if name == "preview":
            x0, y0, w, h = 120, 180, 420, 260
            crop = Image.fromarray(img[y0:y0 + h, x0:x0 + w]).resize((w * 3, h * 3), Image.NEAREST)
            crop.save(os.path.join(OUT, "preview_zoom.png"))
        print("%s: %.2f px/mm, %g mm glyphs -> cell %d x %d px (font %d px), grid %d x %d, ramp %r, dither %s"
              % (name, box.px_per_mm(), s["glyph_mm"], p.cw, p.ch, p.font_size, p.cols, p.rows, s["ramp"], s["dither"]))
    print("wrote", OUT)


def run_flat(p, box, height_mm, ceiling, seconds, mode, rng):
    """Flat sand with +-1 mm noise each frame; returns glyph changes after frame 0."""
    frames = int(round(seconds * 60))
    base = np.full((box.rows, box.cols), height_mm, np.float32)
    p.reset = True
    prev = None
    changes = 0
    cells_changed = np.zeros((p.rows, p.cols), bool)
    for f in range(frames):
        if mode == "independent":
            noise = rng.uniform(-1, 1, base.shape).astype(np.float32)
        else:  # the whole frame shifted by one +-1 mm offset (worst case: no spatial averaging)
            noise = np.float32(rng.uniform(-1, 1))
        p.step(normalize(base + noise, ceiling), 1 / 60.0, draw_glyphs=False)
        st = p.read_state()
        valid = st[..., 3] > 0.5
        g = np.where(valid, st[..., 1], -1)
        if prev is not None:
            diff = g != prev
            changes += int(diff.sum())
            cells_changed |= diff
        prev = g
    return changes, int(cells_changed.sum()), int(valid.sum())


def cmd_noise_test(args, ctx, settings):
    box = Sandbox()
    rng = np.random.default_rng(7)
    p = make_pipeline(ctx, args.gl2, box, settings)
    seconds = 60.0 if not args.quick else 10.0
    ok = True
    print("Flat sand, +-1 mm uniform noise per frame, %g s at 60 fps; glyph grid %d x %d (%d x %d px cells)"
          % (seconds, p.cols, p.rows, p.cw, p.ch))
    print("defaults: smoothing %.2f s, hysteresis %.2f steps, dither %s, ramp %r"
          % (settings["smoothing"], settings["hysteresis"], settings["dither"], settings["ramp"]))
    for ceiling in args.ceilings:
        step_mm = 2 * ceiling / (max(2, min(MAX_RAMP, len(settings["ramp"]))) - 1)
        # Heights: flat sand (9 mm) plus offsets that put cells within
        # fractions of a millimetre of a glyph boundary for every Bayer threshold.
        heights = [9.0] + [9.0 + step_mm * k / 128.0 for k in (1, 3, 5, 7)] + [-20.0, 40.0]
        if args.quick:
            heights = heights[:3]
        for mode in ("independent", "common"):
            for hmm in heights:
                t0 = time.time()
                ch, cells, valid = run_flat(p, box, hmm, ceiling, seconds, mode, rng)
                ok &= ch == 0
                print("  range +-%3.0f mm (step %5.2f mm, hysteresis %4.2f mm) %-11s flat %6.2f mm: "
                      "%d glyph changes (%d cells) of %d cells  [%.0f s]"
                      % (ceiling, step_mm, step_mm * settings["hysteresis"], mode, hmm, ch, cells, valid, time.time() - t0))
    # Sensitivity: the test must be able to fail.
    q = make_pipeline(ctx, args.gl2, box, dict(settings, smoothing=0.0, hysteresis=0.0))
    ch, cells, valid = run_flat(q, box, 9.0, args.ceilings[-1], 5.0, "common", rng)
    print("  control, no smoothing and no hysteresis, 5 s: %d glyph changes (%d cells) - expected > 0" % (ch, cells))
    ok &= ch > 0
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_compare(args, ctx, settings):
    """GL2 and GL3 variants on the preview scene: same held levels, same image."""
    box = Sandbox()
    base = box.heightmap()
    rng = np.random.default_rng(3)
    frames = [normalize(base + rng.uniform(-1, 1, base.shape).astype(np.float32), args.ceilings[-1]) for _ in range(30)]
    res = {}
    for gl2 in (False, True):
        p = make_pipeline(ctx, gl2, box, settings)
        for f in frames:
            p.step(f, 1 / 60.0)
        res[gl2] = (p.read_state(), p.read_out())
    s3, o3 = res[False]
    s2, o2 = res[True]
    lv = np.array_equal(s3[..., 1], s2[..., 1]) and np.array_equal(s3[..., 3], s2[..., 3])
    im = np.array_equal(o3, o2)
    print("GL2 vs GL3: held levels identical: %s; output images identical: %s" % (lv, im))
    return 0 if (lv and im) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["preview", "noise-test", "compare"])
    ap.add_argument("--gl2", action="store_true", help="run the GL2 shader variants (compatibility context)")
    ap.add_argument("--ceiling", type=float, default=80.0, help="calibrated ceiling (mm) for the preview")
    ap.add_argument("--ceilings", type=float, nargs="+", default=[80.0, 40.0],
                    help="calibrated ceilings (mm) the noise test runs (range -c..c)")
    ap.add_argument("--quick", action="store_true", help="10 s and fewer heights")
    args = ap.parse_args()
    settings = read_defaults()
    ctx = Context()
    if args.command == "preview":
        cmd_preview(args, ctx, settings)
        return 0
    if args.command == "noise-test":
        return cmd_noise_test(args, ctx, settings)
    return cmd_compare(args, ctx, settings)


if __name__ == "__main__":
    sys.exit(main())
