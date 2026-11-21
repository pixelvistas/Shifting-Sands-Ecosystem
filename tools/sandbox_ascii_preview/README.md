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

The font atlas is baked with Pillow from `bin/data/fonts/JetBrainsMono-Regular.ttf`
with the same fitting rule as `AsciiFilter::ensureAtlas()`; openFrameworks'
FreeType rendering will differ by a pixel here and there.

Needs numpy, Pillow, PyOpenGL and `libegl1 libegl-mesa0 libgl1-mesa-dri`
(Mesa llvmpipe, surfaceless EGL); `checks` needs git (reads
`origin/ss/dither-baseline`).
