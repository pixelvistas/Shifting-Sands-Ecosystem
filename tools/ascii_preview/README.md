# ascii_preview: headless checks for Prototype 3 ("ASCII")

Runs the module's own shader files (`bin/data/shaders/ascii/GL3`, or `GL2`
with `--gl2`) in an offscreen OpenGL context on a synthetic sandbox (519 x 295
Kinect grid, 1 m play area, 1280 x 800 projector, slightly rotated and
keystoned), with the same cell sizing, glyph ramp, Bayer dithering, smoothing
and hysteresis as `src/Prototypes/Ascii`. The defaults are parsed from
`AsciiGlyphRenderer.cpp` and `AsciiRenderer.cpp`; the font atlas follows
`AsciiFontAtlas::build()` with Pillow (FreeType, as `ofTrueTypeFont`).

    python3 ascii_preview.py preview      # out/preview.png, preview_zoom.png, preview_nodither.png
    python3 ascii_preview.py noise-test   # flat sand, +-1 mm noise each frame, 60 s at 60 fps
    python3 ascii_preview.py compare      # GL2 and GL3 variants give identical glyphs

`noise-test` options: `--ceilings 80 40` (calibrated ceilings, range -c..c),
`--quick` (10 s, fewer heights), `--gl2`. It exits non-zero unless no glyph
changes in any run and a control run without smoothing or hysteresis does
flicker (so the test can fail).

Needs Python 3 with numpy, Pillow and PyOpenGL, and an EGL driver that can
make an offscreen context (Linux/Mesa: `EGL_PLATFORM=surfaceless`, set by the
script). Not part of the openFrameworks build. The synthetic mapping stands in
for `KinectProjector::kinectCoordToProjCoord`; the real calibration is checked
only on the sandbox.
