"""GIF from the harness's story frames, and the still next to the reference.

Usage:
  python3 make_gif.py <outdir> [scale]                 -> <outdir>/story.gif
  python3 make_gif.py <outdir> --compare <ref.jpg>...  -> <outdir>/still_vs_reference.png
Frames are 1 s apart for the first 200 s after the first touch and 10 s
apart after that (the harness prints the timeline); the pre-touch frames
are 30 s apart and black.
"""
import glob
import os
import sys

from PIL import Image

out = sys.argv[1]
if len(sys.argv) > 2 and sys.argv[2] == "--compare":
    still = Image.open(os.path.join(out, "still.png")).convert("RGB")
    refs = [Image.open(p).convert("RGB") for p in sys.argv[3:]]
    h = 480
    tiles = [im.resize((round(im.width * h / im.height), h), Image.LANCZOS) for im in refs + [still]]
    sheet = Image.new("RGB", (sum(t.width for t in tiles) + 10 * (len(tiles) - 1), h), (40, 40, 40))
    x = 0
    for t in tiles:
        sheet.paste(t, (x, 0))
        x += t.width + 10
    sheet.save(os.path.join(out, "still_vs_reference.png"))
    print("wrote still_vs_reference.png")
    sys.exit(0)

scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
files = sorted(glob.glob(os.path.join(out, "story_[0-9][0-9][0-9][0-9].png")))
frames = [Image.open(f).convert("RGB") for f in files]
if scale != 1.0:
    frames = [f.resize((round(f.width * scale), round(f.height * scale)), Image.LANCZOS) for f in frames]
pal = [f.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
pal[0].save(os.path.join(out, "story.gif"), save_all=True, append_images=pal[1:], duration=100, loop=0)
print(f"wrote story.gif: {len(frames)} frames")
