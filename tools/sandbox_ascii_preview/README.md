# sandbox_ascii_preview: headless checks for the Sandbox ASCII filter

Branch `ss/ascii-baseline`. Uses `../sandbox_dither_preview` for the Sandbox
frame (the app's own height-map shader, a stand-in calibration, a synthetic
box with every elevation band, a basin under simulated water and a grid of
pioneer / intermediate / climax patches). With the filter on, the height
shader is drawn twice as `SandSurfaceRenderer` does: the colour
(`plantDisplay` 3, Sandbox lines off) and the parts (`partsPass` 1). Marks
are stand-ins for the FBOs the app hands over: seeds (premultiplied),
Dewitz particles (additive RGB) and six critters (straight alpha), with the
glyphs parsed from the layer classes. The filter runs from
`bin/data/shaders/ascii/GL3|GL2` with the passes, formats and defaults of
`src/Common/AsciiFilter.cpp` (parsed). A "hand" (a 75 mm bump) crosses the
flat sand over 8 frames, so trails show.

    python3 sandbox_ascii_preview.py frames             # out/: each cell preset x trails x glow, crops, atlases
    python3 sandbox_ascii_preview.py frames --fill-off  # the same with band fill off
    python3 sandbox_ascii_preview.py checks             # exact black, Off/Dither identity, GL2 = GL3, parts
    python3 sandbox_ascii_preview.py timing             # software GL: not the installation PC
    python3 sandbox_ascii_preview.py pairs --before DIR # before / after sheets against an earlier out/ (copied aside first)
    python3 sandbox_ascii_preview.py debug --tag after  # debug views (bands, edges, lit, pool) + Full/Dark output, 7x10, pool histogram
    python3 sandbox_ascii_preview.py debug --tag after --speckle --noise-mm 3 --blob-px 4   # mixed plants, correlated depth noise
    python3 sandbox_ascii_preview.py edges --noise-mm 2 # edge flicker with / without band hysteresis, runs, stroke agreement
    python3 sandbox_ascii_preview.py debugpairs --before DIR   # before / after sheets of the debug views

Mock noise: `--noise-mm` adds a fresh Gaussian jitter to the filtered depth
every frame (`--blob-px` correlates it spatially); `--speckle` adds mixed
plant cover on all land, the dominant stage changing every few pixels. The
height shader is drawn three times per frame as SandSurfaceRenderer does
with ASCII on: colour, parts (with band hysteresis against the previous
frame's parts) and surface normals (float target).

The font atlas is baked with Pillow from `bin/data/fonts/JetBrainsMono-Regular.ttf`
with the same size and centring rule as `AsciiFilter::ensureAtlas()` (default:
the reference's, 1.1 x cell height, centred by advance and line box). The app
uses Consolas or Menlo first if installed, as the reference does; the preview
always uses the bundled font. openFrameworks' FreeType rendering will differ
by a pixel here and there.

Technique after "The Betta Fish in ASCII" by @dwcminh.ascii,
https://github.com/dwcminh/ascii-art (BETTA-FISH); see `docs/DEVLOG.md`.

Needs numpy, Pillow, PyOpenGL and `libegl1 libegl-mesa0 libgl1-mesa-dri`
(Mesa llvmpipe, surfaceless EGL); `checks` needs git (reads
`origin/ss/dither-baseline`).
