#!/usr/bin/env python3
"""Headless runs of the Lenia lab's own GLSL (bin/data/shaders/lenia/GL3|GL2),
branch prototype/6-lenia, in the pass order of src/Games/LeniaLab.cpp, on
the setups written by species_tool (the app's LeniaSpecies.cpp).

    ./build.sh && ./species_tool dump ../../bin/data/lenia/species.json out/setup
    python3 lenia_lab.py verify            # GPU vs NumPy references (Lenia FFT, Flow-Lenia)
    python3 lenia_lab.py presets           # 22 s per preset: speed, size, persistence, GIF
    python3 lenia_lab.py mass              # 60 s total mass per channel, both modes
    python3 lenia_lab.py edge              # organisms meeting the mask edge
    python3 lenia_lab.py gradient          # drift with the gradient on
    python3 lenia_lab.py two               # two species interacting
    python3 lenia_lab.py touch             # the brush (a hand) through an organism
    python3 lenia_lab.py compare           # GL2 vs GL3
    python3 lenia_lab.py timing            # software GL: not the installation PC

Self-contained (EGL, Mesa llvmpipe): numpy, scipy, Pillow, PyOpenGL.
"""

import argparse
import ctypes
import json
import math
import os
import sys
import time

os.environ.setdefault("PYOPENGL_PLATFORM", "egl")
os.environ.setdefault("EGL_PLATFORM", "surfaceless")

import numpy as np
from PIL import Image
from OpenGL import EGL, GL

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "out")
SETUP = os.path.join(OUT, "setup")
DOCS = os.path.join(REPO, "docs", "lenia_lab")
R = GL.GL_TEXTURE_RECTANGLE

# Lab defaults (src/Games/LeniaLab.cpp, makeDefaults()): keep in sync.
PROJ_W, PROJ_H = 1280, 800
PX_PER_MM = 1.17          # projector pixels per mm on the sand (stand-in; measure with the scale bar)
CELL_MM = 2.0             # world cell size
TIME_SCALE = 1.5          # simulated time units per second
COLOURS = [(1.0, 0.70, 0.28), (0.36, 0.80, 1.0), (0.80, 0.62, 1.0)]   # amber, sky blue, lilac
GAIN, GAMMA = 1.6, 0.8
GLOW, GLOW_R = 0.35, 4.0


def world_size(cell_mm=CELL_MM):
    return int(round(PROJ_W / PX_PER_MM / cell_mm)), int(round(PROJ_H / PX_PER_MM / cell_mm))


# ------------------------------------------------------------ GL plumbing
class Context:
    def __init__(self):
        self.display = EGL.eglGetDisplay(EGL.EGL_DEFAULT_DISPLAY)
        ma, mi = EGL.EGLint(), EGL.EGLint()
        if not EGL.eglInitialize(self.display, ctypes.pointer(ma), ctypes.pointer(mi)):
            raise RuntimeError("eglInitialize failed")
        cfg, n = EGL.EGLConfig(), EGL.EGLint()
        att = (EGL.EGLint * 5)(EGL.EGL_SURFACE_TYPE, EGL.EGL_PBUFFER_BIT, EGL.EGL_RENDERABLE_TYPE, EGL.EGL_OPENGL_BIT, EGL.EGL_NONE)
        if not EGL.eglChooseConfig(self.display, att, ctypes.pointer(cfg), 1, ctypes.pointer(n)) or n.value < 1:
            raise RuntimeError("no EGL config")
        self.config = cfg
        EGL.eglBindAPI(EGL.EGL_OPENGL_API)
        self.surface = EGL.eglCreatePbufferSurface(self.display, cfg, (EGL.EGLint * 5)(EGL.EGL_WIDTH, 16, EGL.EGL_HEIGHT, 16, EGL.EGL_NONE))

    def create(self, gl2):
        if gl2:
            ca = (EGL.EGLint * 3)(EGL.EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL.EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT, EGL.EGL_NONE)
        else:
            ca = (EGL.EGLint * 7)(EGL.EGL_CONTEXT_MAJOR_VERSION, 3, EGL.EGL_CONTEXT_MINOR_VERSION, 2,
                                  EGL.EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL.EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL.EGL_NONE)
        c = EGL.eglCreateContext(self.display, self.config, EGL.EGL_NO_CONTEXT, ca)
        if not c:
            raise RuntimeError("eglCreateContext failed")
        EGL.eglMakeCurrent(self.display, self.surface, self.surface, c)
        return c


_CTX = None


def context():
    global _CTX
    if _CTX is None:
        _CTX = Context()
    return _CTX


def rect_tex(w, h, internal=GL.GL_RGBA32F, data=None, typ=GL.GL_FLOAT):
    t = GL.glGenTextures(1)
    GL.glBindTexture(R, t)
    for p, v in ((GL.GL_TEXTURE_MIN_FILTER, GL.GL_NEAREST), (GL.GL_TEXTURE_MAG_FILTER, GL.GL_NEAREST),
                 (GL.GL_TEXTURE_WRAP_S, GL.GL_CLAMP_TO_EDGE), (GL.GL_TEXTURE_WRAP_T, GL.GL_CLAMP_TO_EDGE)):
        GL.glTexParameteri(R, p, v)
    GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
    if data is not None:
        data = np.ascontiguousarray(data, np.float32 if typ == GL.GL_FLOAT else np.uint8)
    GL.glTexImage2D(R, 0, internal, w, h, 0, GL.GL_RGBA, typ, data)
    return t


class Target:
    def __init__(self, w, h, internal=GL.GL_RGBA32F):
        self.w, self.h = w, h
        self.tex = rect_tex(w, h, internal, typ=GL.GL_FLOAT if internal != GL.GL_RGBA8 else GL.GL_UNSIGNED_BYTE)
        self.fbo = GL.glGenFramebuffers(1)
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0, R, self.tex, 0)
        assert GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER) == GL.GL_FRAMEBUFFER_COMPLETE

    def bind(self):
        GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, self.fbo)
        GL.glViewport(0, 0, self.w, self.h)

    def clear(self, c=(0, 0, 0, 0)):
        self.bind()
        GL.glClearColor(*c)
        GL.glClear(GL.GL_COLOR_BUFFER_BIT)

    def read(self):
        self.bind()
        a = GL.glReadPixels(0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_FLOAT)
        return np.frombuffer(a, np.float32).reshape(self.h, self.w, 4).copy()

    def upload(self, arr):
        GL.glBindTexture(R, self.tex)
        GL.glTexSubImage2D(R, 0, 0, 0, self.w, self.h, GL.GL_RGBA, GL.GL_FLOAT, np.ascontiguousarray(arr, np.float32))


def ortho(w, h):
    m = np.array([[2.0 / w, 0, 0, -1], [0, 2.0 / h, 0, -1], [0, 0, -1, 0], [0, 0, 0, 1]], np.float32)
    return m.T.copy()


class Shaders:
    NAMES = ["leniaConv", "leniaFlow", "leniaMove", "leniaEdit", "leniaReduce", "leniaRender", "leniaTrail"]

    def __init__(self, gl2):
        self.gl2 = gl2
        context().create(gl2)
        d = os.path.join(REPO, "bin", "data", "shaders", "lenia", "GL2" if gl2 else "GL3")
        self.p = {n: self.compile(os.path.join(d, "lenia.vert"), os.path.join(d, n + ".frag")) for n in self.NAMES}
        if not gl2:
            GL.glBindVertexArray(GL.glGenVertexArrays(1))
        GL.glDisable(GL.GL_BLEND)
        self.cur = None

    def compile(self, vert, frag):
        p = GL.glCreateProgram()
        for path, kind in ((vert, GL.GL_VERTEX_SHADER), (frag, GL.GL_FRAGMENT_SHADER)):
            s = GL.glCreateShader(kind)
            GL.glShaderSource(s, open(path).read())
            GL.glCompileShader(s)
            if not GL.glGetShaderiv(s, GL.GL_COMPILE_STATUS):
                raise RuntimeError("%s: %s" % (path, GL.glGetShaderInfoLog(s).decode()))
            log = GL.glGetShaderInfoLog(s)
            if log and log.strip():
                print("  compile log %s: %s" % (os.path.relpath(path, REPO), log.decode().strip()))
            GL.glAttachShader(p, s)
        if not self.gl2:
            GL.glBindAttribLocation(p, 0, "position")
            GL.glBindAttribLocation(p, 3, "texcoord")
        GL.glLinkProgram(p)
        if not GL.glGetProgramiv(p, GL.GL_LINK_STATUS):
            raise RuntimeError("link %s: %s" % (frag, GL.glGetProgramInfoLog(p).decode()))
        return p

    def use(self, name):
        self.cur = self.p[name]
        GL.glUseProgram(self.cur)
        self.units = 0

    def u(self, name, *v, i=False):
        loc = GL.glGetUniformLocation(self.cur, name)
        if loc < 0:
            return
        if i:
            GL.glUniform1i(loc, int(v[0]))
        else:
            {1: GL.glUniform1f, 2: GL.glUniform2f, 3: GL.glUniform3f, 4: GL.glUniform4f}[len(v)](loc, *[float(x) for x in v])

    def u4v(self, name, arr):
        loc = GL.glGetUniformLocation(self.cur, name)
        if loc >= 0:
            GL.glUniform4fv(loc, len(arr) // 4, np.asarray(arr, np.float32))

    def mat(self, name, m16):
        loc = GL.glGetUniformLocation(self.cur, name)
        if loc >= 0:
            GL.glUniformMatrix4fv(loc, 1, GL.GL_FALSE, np.asarray(m16, np.float32))

    def tex(self, name, t):
        loc = GL.glGetUniformLocation(self.cur, name)
        if loc < 0:
            return
        GL.glActiveTexture(GL.GL_TEXTURE0 + self.units)
        GL.glBindTexture(R, t)
        GL.glUniform1i(loc, self.units)
        self.units += 1

    def quad(self, target, tw=None, th=None):
        """Full-target quad; texcoords span (0..tw, 0..th) (default: the target's texels)."""
        target.bind()
        w, h = target.w, target.h
        tw = w if tw is None else tw
        th = h if th is None else th
        pos = np.array([[0, 0], [w, 0], [0, h], [w, h]], np.float32)
        tc = np.array([[0, 0], [tw, 0], [0, th], [tw, th]], np.float32)
        idx = np.array([0, 1, 2, 1, 3, 2], np.uint32)
        mvp = ortho(w, h)
        if self.gl2:
            GL.glMatrixMode(GL.GL_PROJECTION)
            GL.glLoadMatrixf(mvp)
            GL.glMatrixMode(GL.GL_MODELVIEW)
            GL.glLoadIdentity()
            GL.glEnableClientState(GL.GL_VERTEX_ARRAY)
            GL.glEnableClientState(GL.GL_TEXTURE_COORD_ARRAY)
            GL.glVertexPointer(2, GL.GL_FLOAT, 0, pos)
            GL.glTexCoordPointer(2, GL.GL_FLOAT, 0, tc)
            GL.glDrawElements(GL.GL_TRIANGLES, 6, GL.GL_UNSIGNED_INT, idx)
            return
        GL.glUniformMatrix4fv(GL.glGetUniformLocation(self.cur, "modelViewProjectionMatrix"), 1, GL.GL_FALSE, mvp)
        bufs = GL.glGenBuffers(3)
        for b, arr, loc in ((bufs[0], pos, 0), (bufs[1], tc, 3)):
            GL.glBindBuffer(GL.GL_ARRAY_BUFFER, b)
            GL.glBufferData(GL.GL_ARRAY_BUFFER, arr.nbytes, arr, GL.GL_STREAM_DRAW)
            GL.glEnableVertexAttribArray(loc)
            GL.glVertexAttribPointer(loc, 2, GL.GL_FLOAT, GL.GL_FALSE, 0, None)
        GL.glBindBuffer(GL.GL_ELEMENT_ARRAY_BUFFER, bufs[2])
        GL.glBufferData(GL.GL_ELEMENT_ARRAY_BUFFER, idx.nbytes, idx, GL.GL_STREAM_DRAW)
        GL.glDrawElements(GL.GL_TRIANGLES, 6, GL.GL_UNSIGNED_INT, None)
        GL.glDisableVertexAttribArray(0)
        GL.glDisableVertexAttribArray(3)
        GL.glDeleteBuffers(3, bufs)


# ------------------------------------------------------------ setups
def load_setup(name):
    return json.load(open(os.path.join(SETUP, name + ".json")))


def transform_grid(g, scale, deg):
    """LeniaSpecies.cpp transformGrid (bilinear, rotate about the centre, then scale)."""
    h, w = g.shape
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    hw, hh = w * 0.5 * scale, h * 0.5 * scale
    ext = math.ceil(math.sqrt(hw * hw + hh * hh)) + 1
    n = int(2 * ext)
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    px, py = x + 0.5 - ext, y + 0.5 - ext
    ux = (c * px + s * py) / scale + w * 0.5 - 0.5
    uy = (-s * px + c * py) / scale + h * 0.5 - 0.5
    x0, y0 = np.floor(ux).astype(int), np.floor(uy).astype(int)
    fx, fy = ux - x0, uy - y0

    def at(xx, yy):
        ok = (xx >= 0) & (yy >= 0) & (xx < w) & (yy < h)
        return np.where(ok, g[np.clip(yy, 0, h - 1), np.clip(xx, 0, w - 1)], 0.0)
    return ((1 - fx) * (1 - fy) * at(x0, y0) + fx * (1 - fy) * at(x0 + 1, y0)
            + (1 - fx) * fy * at(x0, y0 + 1) + fx * fy * at(x0 + 1, y0 + 1))


# ------------------------------------------------------------ the lab (GPU)
class Lab:
    def __init__(self, sh, setup, W, H, wrap=False):
        self.sh, self.S, self.W, self.H, self.wrap = sh, setup, W, H, wrap
        self.flow = setup["model"] == 1
        self.embed = bool(setup["embed"])
        self.state = [Target(W, H), Target(W, H)]
        self.param = [Target(W, H), Target(W, H)]
        self.aff = Target(W, H)
        self.disp = [Target(W, H), Target(W, H)]
        self.env = Target(W, H)
        for t in self.state + self.param + [self.aff, self.env] + self.disp:
            t.clear()
        self.cur = 0
        n = setup["nTaps"]
        tap = np.zeros((4, n, 4), np.float32)
        tap[0, :, 0] = setup["tapDx"]
        tap[0, :, 1] = setup["tapDy"]
        w = np.asarray(setup["tapW"], np.float32).reshape(n, 12)
        for row in range(3):
            tap[1 + row] = w[:, 4 * row:4 * row + 4]
        self.tapTex = rect_tex(n, 4, data=tap)
        self.stampTex = None
        self.dt = setup["dt"]
        self.steps = 0
        # environment / lab parameters (LeniaLab defaults)
        self.mask = dict(mode=0, centre=(W / 2, H / 2), radius=min(W, H) * 0.4, edge=4.0, outside=0.0, repel=0.0)
        self.grad = dict(mode=0, dir=(1.0, 0.0), strength=0.0, target=0)
        self.mix_rule = 1

    # -- passes
    def env_uniforms(self, flow_pass=False):
        sh, m, g = self.sh, self.mask, self.grad
        sh.u("maskMode", m["mode"], i=True)
        sh.u("maskCentre", *m["centre"])
        sh.u("maskRadius", m["radius"])
        sh.u("maskEdge", m["edge"])
        sh.u("outsideGrowth", m["outside"])
        sh.u("edgeRepel", m["repel"])
        sh.u("gradMode", g["mode"], i=True)
        sh.u("gradDir", *g["dir"])
        sh.u("gradStrength", g["strength"])
        sh.u("gradTarget", g["target"], i=True)

    def conv(self, out, mode):
        sh, S = self.sh, self.S
        sh.use("leniaConv")
        sh.tex("stateTex", self.state[self.cur].tex)
        sh.tex("tapTex", self.tapTex)
        sh.tex("paramTex", self.param[self.cur].tex)
        sh.tex("envTex", self.env.tex)
        sh.u("worldSize", self.W, self.H)
        sh.u("wrapWorld", 1 if self.wrap else 0, i=True)
        sh.u("nTaps", S["nTaps"], i=True)
        sh.u("nKernels", S["nKernels"], i=True)
        for g in range(3):
            sh.mat("srcSel%d" % g, S["srcSel%d" % g])
            sh.mat("dstSel%d" % g, S["dstSel%d" % g])
            sh.mat("slotSel%d" % g, S["slotSel%d" % g])
        sh.u4v("kM", S["m"])
        sh.u4v("kS", S["s"])
        sh.u4v("kH", S["h"])
        sh.u4v("kGn", S["gn"])
        sh.u4v("kRel", S["rel"])
        sh.u("mode", mode, i=True)
        sh.u("embed", 1 if self.embed else 0, i=True)
        sh.u("dt", self.dt)
        sh.u("channelOn", *S["channelOn"])
        self.env_uniforms()
        sh.quad(out)

    def step(self):
        S, sh = self.S, self.sh
        nxt = 1 - self.cur
        if not self.flow:
            self.conv(self.state[nxt], 0)
        else:
            self.conv(self.aff, 1)
            for pair in (0, 1):
                if pair == 1 and S["channelOn"][2] == 0:
                    continue
                sh.use("leniaFlow")
                sh.tex("stateTex", self.state[self.cur].tex)
                sh.tex("affTex", self.aff.tex)
                sh.tex("envTex", self.env.tex)
                sh.u("worldSize", self.W, self.H)
                sh.u("wrapWorld", 1 if self.wrap else 0, i=True)
                sh.u("pair", pair, i=True)
                sh.u("dt", self.dt)
                sh.u("dd", S["dd"])
                sh.u("sigma", S["sigma"])
                sh.u("theta", S["theta"])
                self.env_uniforms()
                sh.quad(self.disp[pair])
            for target in ((1, 0) if self.embed else (0,)):
                sh.use("leniaMove")
                sh.tex("stateTex", self.state[self.cur].tex)
                sh.tex("dispTex0", self.disp[0].tex)
                sh.tex("dispTex1", self.disp[1].tex)
                sh.tex("paramTex", self.param[self.cur].tex)
                sh.u("worldSize", self.W, self.H)
                sh.u("wrapWorld", 1 if self.wrap else 0, i=True)
                sh.u("dd", S["dd"], i=True)
                sh.u("sigma", S["sigma"])
                sh.u("target", target, i=True)
                sh.u("mixRule", self.mix_rule, i=True)
                sh.u("stepIndex", float(self.steps % 4096))
                sh.quad(self.param[nxt] if target == 1 else self.state[nxt])
        if not self.embed:
            pass
        self.cur = nxt
        if self.embed is False and self.flow:
            pass
        self.steps += 1

    def edit(self, op, target=0, **kw):
        sh = self.sh
        nxt = 1 - self.cur
        sh.use("leniaEdit")
        src = self.env if op == 3 else self.state[self.cur]
        sh.tex("stateTex", src.tex)
        if self.stampTex is not None:
            sh.tex("stampTex", self.stampTex)
        sh.tex("paramTex", self.param[self.cur].tex)
        sh.u("worldSize", self.W, self.H)
        sh.u("op", op, i=True)
        sh.u("target", target, i=True)
        sh.u("stampOrigin", *kw.get("origin", (0, 0)))
        sh.u("stampSize", *kw.get("size", (0, 0)))
        sh.u("stampChannels", *kw.get("channels", (1, 1, 1)))
        sh.u("brushCentre", *kw.get("centre", (0, 0)))
        sh.u("brushRadius", kw.get("radius", 10.0))
        sh.u("brushAmount", kw.get("amount", 1.0))
        sh.u("brushChannels", *kw.get("channels", (1, 0, 0)))
        sh.u("envChannel", kw.get("envChannel", 0), i=True)
        sh.u("envValue", kw.get("envValue", 1.0))
        sh.u("clampOne", 0 if self.flow else 1, i=True)
        sh.u("slotVec", *kw.get("slotVec", (1, 0, 0, 0)))
        if op == 3:
            tmp = Target(self.W, self.H)
            sh.quad(tmp)
            GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, tmp.fbo)
            GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, self.env.fbo)
            GL.glBlitFramebuffer(0, 0, self.W, self.H, 0, 0, self.W, self.H, GL.GL_COLOR_BUFFER_BIT, GL.GL_NEAREST)
            return
        sh.quad(self.param[nxt] if target == 1 else self.state[nxt])

    def apply_edit(self, op, **kw):
        """A edit on A (and P with embedding), then swap (LeniaLab::applyEdit)."""
        if self.embed:
            self.edit(op, target=1, **kw)
        self.edit(op, target=0, **kw)
        if not self.embed:
            # P untouched: keep both buffers equal
            pass
        self.cur = 1 - self.cur
        if not self.embed:
            self.copy_param()

    def copy_param(self):
        GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, self.param[1 - self.cur].fbo)
        GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, self.param[self.cur].fbo)
        GL.glBlitFramebuffer(0, 0, self.W, self.H, 0, 0, self.W, self.H, GL.GL_COLOR_BUFFER_BIT, GL.GL_NEAREST)

    def stamp(self, grids, centre, deg=0.0, scale=1.0, slot=0, channels=None):
        """grids: list of 2-D arrays (one per channel from `base`)."""
        ts = [transform_grid(np.asarray(g, np.float64), scale, deg) for g in grids]
        n = ts[0].shape[0]
        arr = np.zeros((n, n, 4), np.float32)
        for c, t in enumerate(ts):
            ch = (channels[c] if channels else c)
            arr[..., ch] = t
        if self.stampTex is not None:
            GL.glDeleteTextures([self.stampTex])
        self.stampTex = rect_tex(n, n, data=arr)
        origin = (round(centre[0] - n / 2), round(centre[1] - n / 2))
        sv = [0, 0, 0, 0]
        sv[slot] = 1
        self.apply_edit(0, origin=origin, size=(n, n), channels=(1, 1, 1), slotVec=sv)

    def noise(self, centre, side, rng, channels=(0,), slot=0):
        arr = np.zeros((side, side, 4), np.float32)
        for c in channels:
            arr[..., c] = rng.uniform(size=(side, side))
        if self.stampTex is not None:
            GL.glDeleteTextures([self.stampTex])
        self.stampTex = rect_tex(side, side, data=arr)
        origin = (round(centre[0] - side / 2), round(centre[1] - side / 2))
        sv = [0, 0, 0, 0]
        sv[slot] = 1
        self.apply_edit(0, origin=origin, size=(side, side), channels=(1, 1, 1), slotVec=sv)

    def read(self):
        return self.state[self.cur].read()

    def read_param(self):
        return self.param[self.cur].read()

    def upload_state(self, A):
        arr = np.zeros((self.H, self.W, 4), np.float32)
        arr[..., :A.shape[2]] = A
        self.state[self.cur].upload(arr)

    def reduce(self, block=4):
        bw, bh = (self.W + block - 1) // block, (self.H + block - 1) // block
        t = Target(bw, bh)
        sh = self.sh
        sh.use("leniaReduce")
        sh.tex("stateTex", self.state[self.cur].tex)
        sh.u("worldSize", self.W, self.H)
        sh.u("block", float(block))
        sh.quad(t)
        r = t.read()
        GL.glDeleteFramebuffers(1, [t.fbo])
        GL.glDeleteTextures([t.tex])
        return r

    # -- drawing (LeniaLab::render): world scaled onto out_w x out_h
    def render(self, out, prev=None, frac=1.0, colours=COLOURS, gain=GAIN, gamma=GAMMA, glow=GLOW, glow_r=GLOW_R,
               mask_show=0.25, grad_show=0.0):
        sh = self.sh
        sh.use("leniaRender")
        sh.tex("stateTex", self.state[self.cur].tex)
        sh.tex("prevTex", (prev if prev is not None else self.state[self.cur]).tex)
        sh.tex("paramTex", self.param[self.cur].tex)
        sh.tex("envTex", self.env.tex)
        sh.u("worldSize", self.W, self.H)
        sh.u("outSize", out.w, out.h)
        sh.u("frac", frac)
        sh.u("embed", 1 if self.embed else 0, i=True)
        for i in range(3):
            sh.u("colour%d" % i, *colours[i])
        sh.u("channelVisible", 1, 1, 1)
        sh.u("gain", gain)
        sh.u("gammaExp", gamma)
        sh.u("glowAmount", glow)
        sh.u("glowRadius", glow_r)
        sh.u("maskShow", mask_show)
        sh.u("gradShow", grad_show)
        self.env_uniforms()
        sh.quad(out)

    def trail(self, colour, old, new, decay):
        sh = self.sh
        sh.use("leniaTrail")
        sh.tex("colourTex", colour.tex)
        sh.tex("trailTex", old.tex)
        sh.u("decay", decay)
        sh.quad(new)


def to8(img):
    return (np.clip(img[..., :3], 0, 1) * 255 + 0.5).astype(np.uint8)


# ------------------------------------------------------------ measurement
def com_wall(a):
    m = a.sum()
    if m <= 0:
        return np.array([np.nan, np.nan])
    y, x = np.indices(a.shape)
    return np.array([(a * x).sum() / m, (a * y).sum() / m])


def components(a, thr=0.1, min_cells=12):
    from scipy.ndimage import label
    lab, n = label(a > thr, structure=np.ones((3, 3)))
    if n == 0:
        return lab, []
    sizes = np.bincount(lab.ravel())[1:]
    keep = [i + 1 for i, s in enumerate(sizes) if s >= min_cells]
    return lab, keep


def blob_count_blocks(red, thr=0.05):
    """LeniaLab's creature count: 8-connected blocks of the 4x4 block means above thr."""
    from scipy.ndimage import label
    mean = red[..., :3].sum(-1) / np.maximum(red[..., 3], 1)
    lab, n = label(mean > thr, structure=np.ones((3, 3)))
    return n


def diameter_mm(a, cell_mm, thr=0.1):
    """Equivalent-circle diameter of the cells above thr, and the bbox long side."""
    mask = a > thr
    n = mask.sum()
    if n == 0:
        return 0.0, 0.0
    ys, xs = np.nonzero(mask)
    return 2 * math.sqrt(n / math.pi) * cell_mm, max(np.ptp(xs) + 1, np.ptp(ys) + 1) * cell_mm


# ------------------------------------------------------------ commands
def ensure_setups():
    if not os.path.isdir(SETUP) or not os.listdir(SETUP):
        raise SystemExit("run ./build.sh && ./species_tool dump ../../bin/data/lenia/species.json out/setup first")


def cmd_verify(args):
    """GPU vs NumPy: Lenia (Chan's FFT update, torus) and Flow-Lenia (Plantec's update, walls)."""
    ensure_setups()
    sys.path.insert(0, HERE)
    import reference as ref
    sh = Shaders(args.gl2)
    # Lenia: Orbium on a 128 torus
    S = load_setup("orbium")
    cells = np.asarray(S["cells"][0]["v"], np.float64).reshape(S["cells"][0]["h"], S["cells"][0]["w"])
    N = 128
    A = np.zeros((N, N))
    A[50:50 + cells.shape[0], 50:50 + cells.shape[1]] = cells
    lab = Lab(sh, S, N, N, wrap=True)
    lab.upload_state(A[..., None])
    fK = ref.shell_kernel(N, 13, [1.0], 1)
    Ar = A.copy()
    errs = []
    for i in range(200):
        lab.step()
        Ar = ref.lenia_step(Ar, fK, 0.15, 0.015, 1, 0.1)
        if i in (0, 9, 49, 199):
            errs.append((i + 1, float(np.abs(lab.read()[..., 0] - Ar).max())))
    print("Lenia Orbium vs NumPy FFT (torus 128):", ", ".join("step %d max |diff| %.2e" % e for e in errs))
    # Flow-Lenia: the 3-channel example rule, walls, 64 x 64
    S = load_setup("flow_example_1111")
    p = json.load(open(os.path.join(HERE, "flowlenia_seeds.json")))["example_fl_seed1111"]
    N = 64
    rng = np.random.default_rng(3)
    A = np.zeros((N, N, 3))
    A[22:42, 22:42, :] = rng.uniform(size=(20, 20, 3))
    lab = Lab(sh, S, N, N, wrap=False)
    lab.upload_state(A)
    fl = ref.FL(p, N, 3, theta=3.0, border="wall")
    Ar = A.copy()
    errs = []
    m0 = A.sum(axis=(0, 1))
    for i in range(100):
        lab.step()
        Ar = fl.step(Ar)
        if i in (0, 9, 99):
            g = lab.read()[..., :3]
            errs.append((i + 1, float(np.abs(g - Ar).max()), float(np.abs(g).max())))
    g = lab.read()[..., :3]
    print("Flow-Lenia example rule vs NumPy (walls 64):", ", ".join("step %d max |diff| %.2e (max A %.2f)" % e for e in errs))
    print("  mass per channel: start", np.round(m0, 4), "GPU", np.round(g.sum(axis=(0, 1)), 4), "NumPy", np.round(Ar.sum(axis=(0, 1)), 4))


def single_run(sh, name, seconds, cell_mm=CELL_MM, fps=12, gif=None, W=None, H=None, deg=0.0, wrap=False,
               track=True, gif_size=(480, 300), lab_hook=None, start=(0.3, 0.5)):
    S = load_setup(name)
    if W is None:
        W, H = world_size(cell_mm)
    lab = Lab(sh, S, W, H, wrap=wrap)
    nch = len(S["cells"])
    if nch:
        grids = [np.asarray(c["v"], np.float64).reshape(c["h"], c["w"]) for c in S["cells"]]
        lab.stamp(grids, (W * start[0], H * start[1]), deg=deg)
    else:
        rng = np.random.default_rng(1)
        lab.noise((W * start[0], H * start[1]), S["noisePatch"], rng, channels=[c for c in range(3) if S["channelOn"][c]])
    if lab_hook:
        lab_hook(lab)
    steps_per_s = TIME_SCALE / S["dt"]
    frames, track_pts, masses = [], [], []
    acc = 0.0
    out = Target(gif_size[0], gif_size[1], GL.GL_RGBA32F) if gif else None
    prev = Target(W, H)
    total_steps = 0
    for f in range(int(seconds * fps)):
        acc += steps_per_s / fps
        n = int(acc)
        acc -= n
        for _ in range(n):
            GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, lab.state[lab.cur].fbo)
            GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, prev.fbo)
            GL.glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL.GL_COLOR_BUFFER_BIT, GL.GL_NEAREST)
            lab.step()
            total_steps += 1
        if track and f % fps == 0:
            a = lab.read()[..., :3].sum(-1)
            track_pts.append((f / fps, com_wall(a), a.sum(), a))
        if gif:
            lab.render(out, prev=prev, frac=acc if n else 1.0)
            frames.append(Image.fromarray(to8(out.read())[::-1]))
    a = lab.read()[..., :3].sum(-1)
    track_pts.append((seconds, com_wall(a), a.sum(), a))
    if gif:
        frames[0].save(gif, save_all=True, append_images=frames[1:], duration=int(1000 / fps), loop=0, optimize=True)
    return lab, track_pts, total_steps


def cmd_presets(args):
    ensure_setups()
    sh = Shaders(args.gl2)
    os.makedirs(DOCS, exist_ok=True)
    names = args.only.split(",") if args.only else [f[:-5] for f in sorted(os.listdir(SETUP)) if not f.startswith("world_")]
    W, H = world_size()
    print("defaults: projector %dx%d at %.2f px/mm, cell %.1f mm -> world %dx%d cells (%.0f x %.0f mm), time scale %.2f units/s"
          % (PROJ_W, PROJ_H, PX_PER_MM, CELL_MM, W, H, W * CELL_MM, H * CELL_MM, TIME_SCALE))
    for name in names:
        t0 = time.time()
        gif = os.path.join(DOCS, "preset_%s.gif" % name) if args.gif else None
        lab, pts, steps = single_run(sh, name, args.seconds, gif=gif)
        m0, m1 = pts[0][2], pts[-1][2]
        # speed: centre-of-mass path after the first 2 s (settling), per second
        sp = [np.linalg.norm(pts[i + 1][1] - pts[i][1]) for i in range(2, len(pts) - 1)]
        speed = float(np.nanmean(sp)) * CELL_MM if sp else float("nan")
        d_eq, d_box = diameter_mm(pts[-1][3], CELL_MM)
        _, keep = components(pts[-1][3])
        print("%-20s mass %7.1f -> %7.1f  speed %5.1f mm/s  diameter %5.1f mm (bbox %5.1f)  blobs %d  steps %d  (%.0f s)"
              % (name, m0, m1, speed, d_eq, d_box, len(keep), steps, time.time() - t0))



# ------------------------------------------------------------ scenes (sub-worlds at the default cell size)
class Recorder:
    """Steps a Lab at the default time scale and renders frames as LeniaLab::update
    (interpolation between steps, glow, trail 0.5 s), into a GIF."""
    def __init__(self, lab, fps=12, size=None, trail=0.5, time_scale=TIME_SCALE):
        self.lab, self.fps = lab, fps
        W, H = lab.W, lab.H
        self.size = size or (int(W * CELL_MM * 1.0), int(H * CELL_MM * 1.0))
        self.out = Target(*self.size)
        self.tr = [Target(*self.size), Target(*self.size)]
        for t in self.tr:
            t.clear()
        self.tcur = 0
        self.trail = trail
        self.prev = Target(W, H)
        self.acc = 0.0
        self.frames = []
        self.steps_per_frame = time_scale / lab.dt / fps
        self.t = 0.0

    def frame(self, record=True, **render_kw):
        lab = self.lab
        self.acc += self.steps_per_frame
        n = int(self.acc)
        self.acc -= n
        for _ in range(n):
            GL.glBindFramebuffer(GL.GL_READ_FRAMEBUFFER, lab.state[lab.cur].fbo)
            GL.glBindFramebuffer(GL.GL_DRAW_FRAMEBUFFER, self.prev.fbo)
            GL.glBlitFramebuffer(0, 0, lab.W, lab.H, 0, 0, lab.W, lab.H, GL.GL_COLOR_BUFFER_BIT, GL.GL_NEAREST)
            lab.step()
        self.t += 1.0 / self.fps
        if not record:
            return
        lab.render(self.out, prev=self.prev, frac=self.acc, **render_kw)
        if self.trail:
            nt = 1 - self.tcur
            lab.trail(self.out, self.tr[self.tcur], self.tr[nt], math.exp(-1.0 / self.fps / self.trail))
            self.tcur = nt
            img = self.tr[nt].read()
        else:
            img = self.out.read()
        self.frames.append(Image.fromarray(to8(img)))

    def save(self, path):
        self.frames[0].save(path, save_all=True, append_images=self.frames[1:], duration=int(1000 / self.fps), loop=0, optimize=True)
        print("  wrote", os.path.relpath(path, REPO), "(%d frames, %.0f kB)" % (len(self.frames), os.path.getsize(path) / 1024))


def cells_of(S, i=0):
    c = S["cells"][i]
    return np.asarray(c["v"], np.float64).reshape(c["h"], c["w"])


_HEADINGS = {}


def heading_deg(sh, name):
    """Direction of travel (deg, x right, y down) of the unrotated pattern, measured."""
    if name in _HEADINGS:
        return _HEADINGS[name]
    S = load_setup(name)
    lab = Lab(sh, S, 128, 128, wrap=True)
    lab.stamp([cells_of(S)], (64, 64))
    for _ in range(20):
        lab.step()
    c0 = com_wall(lab.read()[..., 0])
    for _ in range(30):
        lab.step()
    c1 = com_wall(lab.read()[..., 0])
    d = c1 - c0
    _HEADINGS[name] = math.degrees(math.atan2(d[1], d[0]))
    return _HEADINGS[name]


def aim(sh, name, want):
    """Rotation for stamp() so the organism travels towards `want` degrees."""
    h = heading_deg(sh, name)
    # transform_grid rotates the pattern by +deg in this (y down) frame
    return want - h


def track(lab, ch=0):
    a = lab.read()[..., ch]
    return com_wall(a), a.sum()


def cmd_mass(args):
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 256, 160
    rng = np.random.default_rng(7)
    print("60 s at the default time scale (cell %.1f mm, %.2f units/s), world %dx%d cells, walls" % (CELL_MM, TIME_SCALE, W, H))
    # Lenia: six Orbium
    S = load_setup("orbium")
    lab = Lab(sh, S, W, H)
    for k in range(6):
        lab.stamp([cells_of(S)], (rng.uniform(30, W - 30), rng.uniform(30, H - 30)), deg=rng.uniform(0, 360))
    rec = Recorder(lab)
    m = []
    for f in range(60 * rec.fps):
        rec.frame(record=False)
        if f % rec.fps == 0:
            m.append(lab.read()[..., 0].sum())
    m.append(lab.read()[..., 0].sum())
    m = np.array(m)
    red = lab.reduce()
    print("Lenia, 6 x Orbium: mass %.2f -> %.2f (%+.2f %%), min %.2f max %.2f; blobs (block count) %d"
          % (m[0], m[-1], 100 * (m[-1] / m[0] - 1), m.min(), m.max(), blob_count_blocks(red)))
    np.save(os.path.join(OUT, "mass_lenia.npy"), m)
    # Flow-Lenia: the example rule (3 channels)
    S = load_setup("flow_example_1111")
    lab = Lab(sh, S, W, H)
    lab.noise((W / 2, H / 2), 40, rng, channels=[0, 1, 2])
    rec = Recorder(lab)
    m = []
    for f in range(60 * rec.fps):
        rec.frame(record=False)
        if f % rec.fps == 0:
            m.append(lab.read()[..., :3].sum(axis=(0, 1)))
    m.append(lab.read()[..., :3].sum(axis=(0, 1)))
    m = np.array(m, np.float64)
    rel = np.abs(m / m[0] - 1).max(axis=0)
    print("Flow-Lenia example rule: mass per channel %s -> %s; max relative change %s (float32 sums)"
          % (np.round(m[0], 3), np.round(m[-1], 3), ["%.1e" % r for r in rel]))
    np.save(os.path.join(OUT, "mass_flow.npy"), m)
    if os.path.exists(os.path.join(SETUP, "world_flow_embed.json")):
        S = load_setup("world_flow_embed")
        lab = Lab(sh, S, W, H)
        lab.noise((W * 0.35, H / 2), 30, rng, channels=[0], slot=0)
        lab.noise((W * 0.65, H / 2), 30, rng, channels=[0], slot=1)
        rec = Recorder(lab)
        m0 = lab.read()[..., 0].sum()
        for f in range(60 * rec.fps):
            rec.frame(record=False)
        m1 = lab.read()[..., 0].sum()
        print("Flow-Lenia embedding world: mass %.3f -> %.3f (%.1e)" % (m0, m1, abs(m1 / m0 - 1)))


def cmd_edge(args):
    """Orbium meeting a circular mask; Flow-Lenia matter at the edge with and without repulsion."""
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 240, 160
    S = load_setup(args.only or "orbium")
    deg = aim(sh, S["id"], 0.0)
    rows = []
    for tag, outside in (("growth zero outside (default)", 0.0), ("outside growth -0.3", -0.3), ("outside growth -1", -1.0)):
        lab = Lab(sh, S, W, H)
        lab.mask.update(mode=1, centre=(W / 2, H / 2), radius=70.0, edge=5.0, outside=outside)
        lab.stamp([cells_of(S)], (W / 2 - 20, H / 2), deg=deg)
        rec = Recorder(lab)
        inside = []
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=args.gif, mask_show=0.25)
            if f % rec.fps == 0:
                a = lab.read()[..., 0]
                y, x = np.indices(a.shape)
                r = np.hypot(x + 0.5 - W / 2, y + 0.5 - H / 2)
                inside.append((a[r < 65].sum(), a[(r >= 65) & (r < 70)].sum(), a[r >= 70].sum(), com_wall(a)))
        if args.gif:
            rec.save(os.path.join(DOCS, "edge_lenia_%s.gif" % ("zero" if outside == 0 else "m%02d" % int(-outside * 10))))
        a = lab.read()[..., 0]
        _, keep = components(a)
        print("Lenia %-30s inside/edge/outside mass at 0, 5, 10, 15, 20 s: %s; blobs at end %d"
              % (tag, " | ".join("%.0f/%.0f/%.0f" % q[:3] for q in inside[::5]), len(keep)))
    # Flow-Lenia: the example rule seeded at the edge
    S = load_setup("flow_example_1111")
    for repel in (0.0, 3.0):
        lab = Lab(sh, S, W, H)
        lab.mask.update(mode=1, centre=(W / 2, H / 2), radius=70.0, edge=5.0, repel=repel)
        rng = np.random.default_rng(2)
        lab.noise((W / 2 + 55, H / 2), 30, rng, channels=[0, 1, 2])
        rec = Recorder(lab)
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=args.gif, mask_show=0.25)
        a = lab.read()[..., :3].sum(-1)
        y, x = np.indices(a.shape)
        r = np.hypot(x + 0.5 - W / 2, y + 0.5 - H / 2)
        if args.gif:
            rec.save(os.path.join(DOCS, "edge_flow_repel%d.gif" % int(repel)))
        print("Flow-Lenia edge repulsion %.1f: mass outside the mask (r > 70 cells) %.1f %% of %.0f after %.0f s"
              % (repel, 100 * a[r > 70].sum() / a.sum(), a.sum(), args.seconds))


def cmd_gradient(args):
    """Orbium heading +x across a gradient that rises towards +y (down)."""
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 300, 160
    S = load_setup(args.only or "orbium")
    deg = aim(sh, S["id"], 0.0)
    cases = [("off", 0, 0, 0.0)]
    for st in (0.02, 0.05, 0.1, 0.2):
        cases.append(("growth bias %.2f" % st, 1, 0, st))
    for st in (0.5, 1.0, 2.0):
        cases.append(("drift %.1f cells/unit" % st, 1, 1, st))
    for tag, mode, target, st in cases:
        lab = Lab(sh, S, W, H, wrap=True)
        lab.grad.update(mode=mode, dir=(0.0, 1.0), strength=st, target=target)
        lab.stamp([cells_of(S)], (40, H / 2), deg=deg)
        rec = Recorder(lab)
        p0 = None
        path = []
        prev_c = None
        disp = np.zeros(2)
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=args.gif and tag in ("off", "growth bias 0.05", "drift 1.0 cells/unit"), grad_show=0.15)
            if f % 3 == 0:
                a = lab.read()[..., 0]
                c = com_on_torus(a)
                if prev_c is not None:
                    d = c - prev_c
                    d = (d + np.array([W, H]) / 2) % np.array([W, H]) - np.array([W, H]) / 2
                    disp += d
                prev_c = c
        a = lab.read()[..., 0]
        _, keep = components(a)
        if args.gif and tag in ("off", "growth bias 0.05", "drift 1.0 cells/unit"):
            rec.save(os.path.join(DOCS, "gradient_%s.gif" % tag.split()[0] + ("" if tag == "off" else "_" + tag.split()[-1].replace("/", ""))))
        print("%-26s after %.0f s: along +x %6.1f mm, up the gradient (+y) %6.1f mm, mass %.1f, blobs %d"
              % (tag, args.seconds, disp[0] * CELL_MM, disp[1] * CELL_MM, a.sum(), len(keep)))
    # Flow-Lenia: the example rule, affinity bias and drift
    S = load_setup("flow_example_1111")
    for tag, target, st in (("off", 0, 0.0), ("affinity bias 0.05", 0, 0.05), ("affinity bias 0.2", 0, 0.2), ("drift 1.0", 1, 1.0)):
        lab = Lab(sh, S, W, H)
        lab.grad.update(mode=1 if st else 0, dir=(0.0, 1.0), strength=st, target=target)
        rng = np.random.default_rng(2)
        lab.noise((W / 2, H / 2), 40, rng, channels=[0, 1, 2])
        c0 = com_wall(lab.read()[..., :3].sum(-1))
        rec = Recorder(lab)
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=False)
        c1 = com_wall(lab.read()[..., :3].sum(-1))
        print("Flow-Lenia %-20s centre of mass moved %+6.1f mm along +x, %+6.1f mm up the gradient (+y)"
              % (tag, (c1 - c0)[0] * CELL_MM, (c1 - c0)[1] * CELL_MM))


def com_on_torus(a):
    out = []
    for ax, n in ((1, a.shape[1]), (0, a.shape[0])):
        th = (np.arange(n) + 0.5) / n * 2 * np.pi
        w = a.sum(axis=1 - ax)
        c = math.atan2((w * np.sin(th)).sum(), (w * np.cos(th)).sum())
        out.append((c % (2 * np.pi)) / (2 * np.pi) * n - 0.5)
    return np.array(out)


def cmd_two(args):
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 260, 140
    for name in ("two_orbium_repel", "two_orbium_prey"):
        S = load_setup("world_" + name)
        lab = Lab(sh, S, W, H)
        a_deg = aim(sh, "orbium", 0.0)
        b_deg = aim(sh, "orbium", 180.0)
        cA = cells_of(S, 0)
        cB = cells_of(S, 1)
        lab.stamp([cA], (60, H / 2), deg=a_deg, channels=[0])
        lab.stamp([cB], (W - 60, H / 2 + 6), deg=b_deg, channels=[1])
        rec = Recorder(lab)
        masses = []
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=args.gif)
            if f % (2 * rec.fps) == 0:
                a = lab.read()
                masses.append((a[..., 0].sum(), a[..., 1].sum()))
        if args.gif:
            rec.save(os.path.join(DOCS, "two_species_%s.gif" % name.split("_")[-1]))
        print("%-18s mass (ch0, ch1) every 2 s: %s" % (name, " ".join("%.0f/%.0f" % m for m in masses)))


def cmd_touch(args):
    """The erase brush (a hand) on a moving Orbium: centre (25 mm brush) and a graze (8 mm, rear)."""
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 260, 140
    S = load_setup("orbium")
    deg = aim(sh, "orbium", 0.0)
    for tag, radius_mm, offset in (("hand on it (25 mm brush, centre)", 25.0, 0.0), ("graze (8 mm brush, 6 mm behind centre)", 8.0, -3.0),
                                   ("graze (8 mm brush, 6 mm to the side)", 8.0, None)):
        lab = Lab(sh, S, W, H)
        lab.stamp([cells_of(S)], (50, H / 2), deg=deg)
        rec = Recorder(lab)
        log = []
        for f in range(int(args.seconds * rec.fps)):
            if f == 4 * rec.fps:
                c, _ = track(lab)
                centre = (c[0] + offset, c[1]) if offset is not None else (c[0], c[1] + 3.0)
                lab.apply_edit(2, centre=centre, radius=radius_mm / CELL_MM, amount=1.0, channels=(1, 0, 0))
            rec.frame(record=args.gif and offset == 0.0)
            if f % rec.fps == 0:
                c, m = track(lab)
                log.append(m)
        a = lab.read()[..., 0]
        _, keep = components(a)
        if args.gif and offset == 0.0:
            rec.save(os.path.join(DOCS, "touch_erase.gif"))
        print("%-42s mass at 0..%.0f s (1 s): %s; blobs at end %d" % (tag, args.seconds, " ".join("%.0f" % m for m in log), len(keep)))


def cmd_compare(args):
    ensure_setups()
    res = {}
    for gl2 in (False, True):
        sh = Shaders(gl2)
        out = {}
        S = load_setup("orbium")
        lab = Lab(sh, S, 128, 96)
        lab.mask.update(mode=1, centre=(64, 48), radius=40, edge=4, outside=-0.2)
        lab.grad.update(mode=1, dir=(0.6, 0.8), strength=0.05, target=0)
        lab.stamp([cells_of(S)], (60, 48), deg=30)
        for _ in range(40):
            lab.step()
        lab.apply_edit(2, centre=(64, 48), radius=4, amount=0.5, channels=(1, 0, 0))
        out["lenia"] = lab.read()
        o = Target(256, 192)
        lab.render(o, mask_show=0.3, grad_show=0.2)
        out["render"] = o.read()
        S = load_setup("flow_example_1111")
        lab = Lab(sh, S, 96, 96)
        lab.mask.update(mode=1, centre=(48, 48), radius=40, edge=4, repel=2.0)
        lab.noise((48, 48), 30, np.random.default_rng(1), channels=[0, 1, 2])
        for _ in range(15):
            lab.step()
        out["flow"] = lab.read()
        res[gl2] = out
        EGL.eglMakeCurrent(context().display, EGL.EGL_NO_SURFACE, EGL.EGL_NO_SURFACE, EGL.EGL_NO_CONTEXT)
    for k in res[False]:
        d = np.abs(res[False][k] - res[True][k]).max()
        print("GL2 vs GL3 %-7s identical %s (max difference %.3g)" % (k, d == 0, d))


def cmd_timing(args):
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = world_size()
    print("software GL (llvmpipe, %d cores): not the installation PC" % os.cpu_count())
    for name in ("orbium", "kronium", "flow_example_1111"):
        S = load_setup(name)
        lab = Lab(sh, S, W, H)
        if S["cells"]:
            lab.stamp([cells_of(S)], (W / 2, H / 2))
        else:
            lab.noise((W / 2, H / 2), 40, np.random.default_rng(1), channels=[0, 1, 2])
        GL.glFinish()
        t0 = time.time()
        n = 5
        for _ in range(n):
            lab.step()
        GL.glFinish()
        dt = (time.time() - t0) / n
        print("%-18s %dx%d cells, %4d taps: %.0f ms per step; at %.1f steps/s that is %.0f ms of every second"
              % (name, W, H, S["nTaps"], dt * 1000, TIME_SCALE / S["dt"], dt * 1000 * TIME_SCALE / S["dt"]))
    out = Target(PROJ_W, PROJ_H)
    tr = [Target(PROJ_W, PROJ_H), Target(PROJ_W, PROJ_H)]
    GL.glFinish()
    t0 = time.time()
    for i in range(3):
        lab.render(out)
        lab.trail(out, tr[i % 2], tr[1 - i % 2], 0.9)
    GL.glFinish()
    print("render + trail at %dx%d: %.0f ms per frame" % (PROJ_W, PROJ_H, (time.time() - t0) / 3 * 1000))



def cmd_embed(args):
    """Flow-Lenia with parameter embedding: two found rules in one matter channel."""
    ensure_setups()
    sh = Shaders(args.gl2)
    W, H = 200, 130
    S = load_setup("world_flow_embed")
    for rule, tag in ((1, "stochastic"), (0, "average")):
        lab = Lab(sh, S, W, H)
        lab.mix_rule = rule
        rng = np.random.default_rng(4)
        lab.noise((W * 0.36, H / 2), 30, rng, channels=[0], slot=0)
        lab.noise((W * 0.64, H / 2), 30, rng, channels=[0], slot=1)
        m0 = lab.read()[..., 0].sum()
        rec = Recorder(lab)
        for f in range(int(args.seconds * rec.fps)):
            rec.frame(record=args.gif and rule == 1)
        a = lab.read()[..., 0]
        P = lab.read_param()
        occ = a > 0.05
        w0 = (a * P[..., 0])[occ].sum() / a[occ].sum()
        mixed = ((P[..., :2].max(-1) < 0.9) & occ).sum() / max(1, occ.sum())
        if args.gif and rule == 1:
            rec.save(os.path.join(DOCS, "embed_two_rules.gif"))
        print("embedding, %-10s mixing: mass %.3f -> %.3f (%.1e); matter in slot 0 %.2f, slot 1 %.2f; cells with mixed P %.1f %%"
              % (tag, m0, a.sum(), abs(a.sum() / m0 - 1), w0, 1 - w0, 100 * mixed))



def cmd_netgif(args):
    """Net displacement of each preset from its GIF (brightness-weighted centroid,
    first vs last second): separates travel from circling. Box = 480 px wide."""
    W, _ = world_size()
    mm_per_px = W * CELL_MM / 480.0
    for f in sorted(os.listdir(DOCS)):
        if not f.startswith("preset_"):
            continue
        im = Image.open(os.path.join(DOCS, f))
        n = im.n_frames
        def cen(idx):
            cs = []
            for i in idx:
                im.seek(i)
                a = np.asarray(im.convert("L"), np.float64)
                a = np.where(a > 40, a, 0)
                cs.append(com_wall(a))
            return np.nanmean(cs, axis=0)
        fps = 12
        c0, c1 = cen(range(2 * fps, 3 * fps)), cen(range(n - fps, n))
        dur = (n - fps - 2 * fps) / fps
        d = np.linalg.norm(c1 - c0) * mm_per_px
        print("%-28s net %6.1f mm in %.0f s = %5.1f mm/s" % (f[7:-4], d, dur, d / dur))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("--gl2", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--seconds", type=float, default=22.0)
    ap.add_argument("--gif", action="store_true")
    args = ap.parse_args()
    globals()["cmd_" + args.cmd](args)


if __name__ == "__main__":
    main()
