"""Independent NumPy references for tools/lenia_lab (branch prototype/6-lenia),
written from the published code as read (Chan: Python/LeniaND.py, Board.rle2arr;
Plantec: flowlenia/*.py). FFT convolution on a torus (Lenia) and the published
Flow-Lenia step; used by lenia_lab.py verify to check the GLSL.
"""
import json, sys, math
import numpy as np

def rle2arr(st):
    """2-D version of Board.rle2arr (LeniaND.py): '.'/'b' 0, 'o' 255, 'A'..'X' 1..24,
    'p'..'y' + 'A'..'X' 25..255; digits = run count; '$' ends a row; '!' ends."""
    rows, row = [], []
    last, count = '', ''
    st = st.rstrip('!') + '$'
    for ch in st:
        if ch.isdigit():
            count += ch
        elif ch in 'pqrstuvwxy@':
            last = ch
        else:
            tok = last + ch
            n = int(count) if count else 1
            if tok == '$':
                rows.append(row)
                for _ in range(n - 1):
                    rows.append([])
                row = []
            else:
                if tok in '.b':
                    v = 0
                elif tok == 'o':
                    v = 255
                elif len(tok) == 1:
                    v = ord(tok) - ord('A') + 1
                else:
                    v = (ord(tok[0]) - ord('p')) * 24 + (ord(tok[1]) - ord('A') + 25)
                row += [v / 255.0] * n
            last, count = '', ''
    w = max(len(r) for r in rows)
    return np.array([r + [0.0] * (w - len(r)) for r in rows])

CORE = {1: lambda r: (4 * r * (1 - r)) ** 4,
        2: lambda r: np.exp(4 - 1 / np.maximum(r * (1 - r), 1e-12)),
        3: lambda r: ((r >= .25) & (r <= .75)).astype(float)}
GROW = {1: lambda u, m, s: np.maximum(0, 1 - (u - m) ** 2 / (9 * s * s)) ** 4 * 2 - 1,
        2: lambda u, m, s: np.exp(-(u - m) ** 2 / (2 * s * s)) * 2 - 1,
        3: lambda u, m, s: (np.abs(u - m) <= s) * 2.0 - 1}

def shell_kernel(N, R, b, kn, r=1.0):
    y, x = np.mgrid[0:N, 0:N]
    D = np.hypot(x - N // 2, y - N // 2) / R
    B = len(b)
    Br = B * D / r
    bb = np.asarray(b, float)[np.minimum(np.floor(Br).astype(int), B - 1)]
    K = (D < r) * CORE[kn](np.minimum(Br % 1, 1)) * bb
    K = K / K.sum()
    return np.fft.fft2(np.fft.fftshift(K))

def lenia_step(A, fK, m, s, gn, dt):
    U = np.real(np.fft.ifft2(fK * np.fft.fft2(A)))
    return np.clip(A + dt * GROW[gn](U, m, s), 0, 1)

def com(A):
    """centre of mass on a torus (circular mean)"""
    N = A.shape
    out = []
    for ax in (1, 0):
        th = np.arange(N[ax]) / N[ax] * 2 * np.pi
        w = A.sum(axis=1 - ax)
        c = np.arctan2((w * np.sin(th)).sum(), (w * np.cos(th)).sum())
        out.append((c % (2 * np.pi)) / (2 * np.pi) * N[ax])
    return np.array(out)

def unwrap(p, q, N):
    d = q - p
    return (d + N / 2) % N - N / 2


# ---- Flow-Lenia, written from erwanplantec/FlowLenia (main) as read, not run:
# utils.get_kernels_fft, utils.growth, utils.sobel, flowlenia.FlowLenia.__call__,
# reintegration_tracking._apply_without_hidden.
from scipy.signal import convolve2d

def sigmoid(x):
    return 0.5 * (np.tanh(x / 2) + 1)

def kernels_fft(N, k, R, r, a, w, b):
    mid = N // 2
    yy, xx = np.mgrid[-mid:mid, -mid:mid]
    fK = []
    for i in range(k):
        D = np.hypot(yy, xx) / ((R + 15) * r[i])
        K = sigmoid(-(D - 1) * 10) * (np.asarray(b[i]) * np.exp(-(D[..., None] - np.asarray(a[i])) ** 2 / np.asarray(w[i]))).sum(-1)
        K = K / K.sum()
        fK.append(np.fft.fft2(np.fft.fftshift(K)))
    return np.stack(fK, -1)

def growth(U, m, s):
    return np.exp(-((U - m) / s) ** 2 / 2) * 2 - 1

KX = np.array([[1., 0., -1.], [2., 0., -2.], [1., 0., -1.]])
def sobel(A):  # (x,y,c) -> (x,y,2,c): [d/daxis0, d/daxis1]
    sx = np.dstack([convolve2d(A[:, :, c], KX, mode='same') for c in range(A.shape[-1])])
    sy = np.dstack([convolve2d(A[:, :, c], KX.T, mode='same') for c in range(A.shape[-1])])
    return np.concatenate((sy[:, :, None, :], sx[:, :, None, :]), axis=2)

def reintegrate(A, F, dt, dd, sigma, border='wall'):
    SX, SY = A.shape[:2]
    X, Y = np.meshgrid(np.arange(SX), np.arange(SY))
    pos = np.dstack((Y, X)) + .5
    ma = dd - sigma
    mu = pos[..., None] + np.clip(dt * F, -ma, ma)
    if border == 'wall':
        mu = np.clip(mu, sigma, SX - sigma)
    nA = np.zeros_like(A)
    for dx in range(-dd, dd + 1):
        for dy in range(-dd, dd + 1):
            Ar = np.roll(A, (dx, dy), axis=(0, 1))
            mur = np.roll(mu, (dx, dy), axis=(0, 1))
            dpmu = np.abs(pos[..., None] - mur)
            sz = .5 - dpmu + sigma
            area = np.prod(np.clip(sz, 0, min(1, 2 * sigma)), axis=2) / (4 * sigma ** 2)
            nA += Ar * area
    return nA

class FL:
    def __init__(self, p, N, C, dt=0.2, dd=5, sigma=0.65, theta=None, border='wall'):
        self.p = p; self.N = N; self.C = C; self.dt = dt; self.dd = dd; self.sigma = sigma
        self.theta = C if theta is None else theta; self.border = border
        k = len(p['r'])
        self.fK = kernels_fft(N, k, p['R'], p['r'], p['a'], p['w'], p['b'])
        self.c0 = p['c0']; self.c1 = p['c1']
        self.m = np.asarray(p['m']); self.s = np.asarray(p['s']); self.h = np.asarray(p['h'])

    def step(self, A):
        fA = np.fft.fft2(A, axes=(0, 1))
        U = np.real(np.fft.ifft2(self.fK * fA[:, :, self.c0], axes=(0, 1)))
        U = growth(U, self.m, self.s) * self.h
        U = np.dstack([U[:, :, self.c1[c]].sum(axis=-1) for c in range(self.C)])
        nU = sobel(U)
        nA = sobel(A.sum(axis=-1, keepdims=True))
        alpha = np.clip((A[:, :, None, :] / self.theta) ** 2, 0, 1)
        F = nU * (1 - alpha) - nA * alpha
        return reintegrate(A, F, self.dt, self.dd, self.sigma, self.border)

