# sandbox_dither_preview: headless checks for the Sandbox dither filter

Branch `ss/dither-baseline`. Renders a Sandbox frame with the app's own
height-map shader (`bin/data/shaders/shadersGL3|GL2/heightMapShader.*`)
through a stand-in calibration (pinhole Kinect, ROI 519 x 295, projector
1280 x 800 at ~1.17 px/mm, rotated 1.5 deg) on a synthetic terrain: a mound
through every land band, a basin under simulated water, and a grid of plant
patches (rows pioneer / intermediate / climax, columns cover 0.2 / 0.45 /
0.7 / 0.95, soil 0.7). Seeds and Dewitz particles are stand-ins with the
app's blend equations (colour only, as `glColorMask` in `ofApp`).

- Old = filter off: `plantDisplay` 1 (grain), as `ss/plant-grain`.
- New = filter on: `plantDisplay` 3 (flat, stage code in alpha), solid
  contour diagonals, then `bin/data/shaders/dither` (dots, pyramid, output)
  with the defaults parsed from `src/Common/DitherFilter.cpp`.

    python3 sandbox_dither_preview.py frames     # out/: old, new, sheets (zoomed)
    python3 sandbox_dither_preview.py stages     # dot size measured per patch
    python3 sandbox_dither_preview.py identity   # filter off = ss/plant-grain's height shader, bit for bit
    python3 sandbox_dither_preview.py lines      # line protection per pixel kind (--save, --fill-off)
    python3 sandbox_dither_preview.py colour     # patch colours, flat vs dots vs old grain
    python3 sandbox_dither_preview.py moire      # low-pass error in planted patches, dot 1..8
    python3 sandbox_dither_preview.py compare    # GL2 vs GL3
    python3 sandbox_dither_preview.py timing     # software GL: not the installation PC

Self-contained (EGL, Mesa llvmpipe). Needs numpy, Pillow, PyOpenGL and
`libegl1 libegl-mesa0 libgl1-mesa-dri`; `identity` needs git (reads
`origin/ss/plant-grain`).
