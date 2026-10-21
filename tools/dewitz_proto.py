"""Prototype of the Dewitz et al. (2018) particle scheme on synthetic terrain.

D2Q9 lattice-Boltzmann (BGK) flow across the WHOLE box, left inlet -> right
outlet, with hills and steep edges as obstacles; particles move by a weighted
sum of the LBM velocity and the downhill terrain gradient. Renders particle
density the way a GPU particle texture accumulates.
"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy.ndimage import gaussian_filter, map_coordinates

NX, NY = 240, 150
rng = np.random.default_rng(3)

# ---------- synthetic sand: two mounds, a ridge with a gap, a dug pit ----------
y, x = np.mgrid[0:NY, 0:NX].astype(float)
def bump(cx, cy, r, h):
    return h * np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / (2 * r ** 2))
h = np.zeros((NY, NX))
h += bump(70, 45, 14, 40) + bump(150, 105, 18, 45)
ridge = 35 * np.exp(-((x - 115) ** 2) / (2 * 5 ** 2)) * ((y < 60) | (y > 85))
h += ridge
h += bump(195, 50, 16, -25)                     # dug pit
h += gaussian_filter(rng.normal(0, 1.5, h.shape), 2)  # sensor-ish noise
h = gaussian_filter(h, 1.0)

gy, gx = np.gradient(h)
slope = np.hypot(gx, gy)
OBST = (h > 18) | (slope > 4.0)                 # hills and steep edges = walls
OBST[0, :] = OBST[-1, :] = True

# ---------- D2Q9 ----------
CX = np.array([0, 1, 0, -1, 0, 1, -1, -1, 1])
CY = np.array([0, 0, 1, 0, -1, 1, 1, -1, -1])
W = np.array([4/9] + [1/9]*4 + [1/36]*4)
OPP = np.array([0, 3, 4, 1, 2, 7, 8, 5, 6])

def feq(rho, ux, uy):
    cu = CX[:, None, None] * ux + CY[:, None, None] * uy
    uu = ux ** 2 + uy ** 2
    return W[:, None, None] * rho * (1 + 3 * cu + 4.5 * cu ** 2 - 1.5 * uu)

def run_lbm(tau, u0=0.08, steps=5000):
    f = feq(np.ones((NY, NX)), np.full((NY, NX), u0), np.zeros((NY, NX)))
    for s in range(steps):
        rho = f.sum(0)
        ux = (f * CX[:, None, None]).sum(0) / rho
        uy = (f * CY[:, None, None]).sum(0) / rho
        # inlet (left) and pressure outlet (right) as equilibria at rho = 1
        ux[:, 0], uy[:, 0], rho[:, 0] = u0, 0, 1
        ux[:, -1], uy[:, -1], rho[:, -1] = ux[:, -2], uy[:, -2], 1
        fe = feq(rho, ux, uy)
        f = f + (fe - f) / tau
        f[:, :, 0] = fe[:, :, 0]
        f[:, :, -1] = fe[:, :, -1]
        # bounce-back on obstacles (full-way, pre-streaming)
        f[:, OBST] = f[OPP][:, OBST]
        for i in range(9):
            f[i] = np.roll(np.roll(f[i], CX[i], axis=1), CY[i], axis=0)
        if not np.isfinite(f).all():
            return None, None, s
    rho = f.sum(0)
    ux = (f * CX[:, None, None]).sum(0) / rho
    uy = (f * CY[:, None, None]).sum(0) / rho
    ux[OBST] = uy[OBST] = 0
    return ux, uy, steps

def particles(ux, uy, w_flow, w_grad, n=40000, frames=900, speed=1.2, fade=0.985, rain=False):
    img = np.zeros((NY * 3, NX * 3))
    px = rng.uniform(0, NX - 2, n); py = rng.uniform(1, NY - 2, n)
    age = rng.uniform(0, 600, n)
    # downhill direction, normalised so the weight means the same everywhere
    dgx, dgy = -gx, -gy
    norm = np.hypot(dgx, dgy) + 1e-6
    dgx, dgy = dgx / norm * np.minimum(norm / 2, 1), dgy / norm * np.minimum(norm / 2, 1)
    umax = max(np.hypot(ux, uy).max(), 1e-6) if ux is not None else 1
    for t in range(frames):
        coords = np.vstack([py, px])
        fx = fy = 0
        if ux is not None and w_flow > 0:
            fx = w_flow * map_coordinates(ux, coords, order=1) / umax
            fy = w_flow * map_coordinates(uy, coords, order=1) / umax
        fx = fx + w_grad * map_coordinates(dgx, coords, order=1)
        fy = fy + w_grad * map_coordinates(dgy, coords, order=1)
        px += speed * fx; py += speed * fy
        age += 1
        dead = (px < 0) | (px >= NX - 1) | (py < 0) | (py >= NY - 1) | (age > 600) \
            | OBST[np.clip(py, 0, NY - 1).astype(int), np.clip(px, 0, NX - 1).astype(int)]
        k = dead.sum()
        px[dead] = rng.uniform(0, NX - 2 if rain else 2, k); py[dead] = rng.uniform(1, NY - 2, k); age[dead] = 0
        img *= fade
        np.add.at(img, ((py * 3).astype(int), (px * 3).astype(int)), 1.0)
    return img

def panel(ax, img, title):
    hh = np.kron(h, np.ones((3, 3)))
    ax.imshow(hh, cmap="BuPu_r", alpha=1.0)
    glow = np.clip(np.log1p(img) / np.log1p(np.percentile(img[img > 0], 97) + 1e-6), 0, 1)
    rgba = np.zeros(glow.shape + (4,)); rgba[..., 0] = 1; rgba[..., 1] = 0.8; rgba[..., 2] = 0.95
    rgba[..., 3] = glow
    ax.imshow(rgba)
    ax.set_title(title, fontsize=10); ax.axis("off")

out = sys.argv[1]
fig, axes = plt.subplots(1, 3, figsize=(16, 4.2))
import os
if os.path.exists("lbm198.npz"):
    d = np.load("lbm198.npz"); ux, uy = d["ux"], d["uy"]
else:
    ux, uy, s = run_lbm(1.98); np.savez("lbm198.npz", ux=ux, uy=uy)
panel(axes[0], particles(ux, uy, 1.0, 0.5), "Dewitz-style: τ = 1.98, whole-box flow + downhill pull")
ux2, uy2, s2 = (None, None, 1631) if os.path.exists("lbm198.npz") else run_lbm(0.55)
if ux2 is None:
    axes[1].text(0.5, 0.5, f"τ = 0.55 (current repo default):\nsolver blew up at step {s2}\non the same terrain", ha="center", va="center", fontsize=11)
    axes[1].axis("off")
else:
    panel(axes[1], particles(ux2, uy2, 1.0, 0.0), "τ = 0.55, LBM velocity only (current repo defaults)")
panel(axes[2], particles(None, None, 0.0, 1.0, rain=True), "No LBM: downhill pull only, particles dropped anywhere")
plt.tight_layout(); plt.savefig(out, dpi=110)
print("tau 1.98 max u", np.hypot(ux, uy).max(), "| tau 0.55:", "unstable" if ux2 is None else np.hypot(ux2, uy2).max())
