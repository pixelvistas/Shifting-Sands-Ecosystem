"""Writes tools/dewitz_proto.py's synthetic terrain (identical code and seed,
lines 15-28 of that file) as raw float32, row-major 240x150, so the C++
harness can run DewitzFlow on exactly the prototype's heightmap.
Usage: python3 export_proto_terrain.py <out.f32>"""
import sys
import numpy as np
from scipy.ndimage import gaussian_filter

NX, NY = 240, 150
rng = np.random.default_rng(3)
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
h.astype(np.float32).tofile(sys.argv[1])
print("wrote", sys.argv[1], h.shape, "min %.1f max %.1f" % (h.min(), h.max()))
