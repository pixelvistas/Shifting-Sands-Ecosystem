# sandbox_dither_preview: headless checks for the Sandbox dither filter

Renders one Sandbox frame with the app's own height-map shader
(`bin/data/shaders/shadersGL3|GL2/heightMapShader.*`, unchanged) through a
stand-in calibration (pinhole Kinect, ROI 519 x 295, projector 1280 x 800 at
~1.17 px/mm, rotated 1.5 deg), on `tools/ascii_preview`'s synthetic terrain,
with simulated water below the table, plant cover (grain), soil, and the
SandSurfaceRenderer defaults. Seeds and Dewitz particles are stand-ins
composited with the app's blend equations; critters (default 0) are not
drawn. The frame then goes through `bin/data/shaders/dither` with the
defaults parsed from `src/Common/DitherFilter.cpp`. EGL (Mesa llvmpipe).

    python3 sandbox_dither_preview.py frames    # out/: off, every mode x pattern, sheets (4x crops)
    python3 sandbox_dither_preview.py lines     # line protection per pixel kind (--save: map; --fill-off)
    python3 sandbox_dither_preview.py colour    # band hue and luminance kept, per mode
    python3 sandbox_dither_preview.py moire     # low-pass error, grain x dot size
    python3 sandbox_dither_preview.py compare   # GL2 vs GL3
    python3 sandbox_dither_preview.py timing    # on this software GL, not the installation PC

Sheets: rows = off, then the configurations; columns = mound (band edges,
grain), water basin (water lines, particles), ramp (band boundary).
Needs numpy, Pillow, PyOpenGL (and `libegl1 libegl-mesa0 libgl1-mesa-dri`).
