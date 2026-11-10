"""GIF from the harness's PNG frames (one per 2 s of simulation).

Usage: python3 make_gif.py <outdir> <prefix> [scale]   e.g. T1 or M
"""
import glob
import os
import sys

from PIL import Image

out, prefix = sys.argv[1], sys.argv[2]
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 2
files = sorted(glob.glob(os.path.join(out, f"{prefix}_[0-9][0-9][0-9][0-9].png")))
frames = [Image.open(f).convert("RGB") for f in files]
frames = [f.resize((f.width * scale, f.height * scale), Image.NEAREST) for f in frames]
pal = [f.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE) for f in frames]
pal[0].save(os.path.join(out, f"{prefix}.gif"), save_all=True, append_images=pal[1:], duration=100, loop=0)
print(f"wrote {prefix}.gif: {len(frames)} frames ({files[0]} .. {files[-1]})")
