# scene_preview: headless checks for Prototype 4 ("Scene")

Runs the module's own shaders (`bin/data/shaders/scene/GL3`, or `GL2` with
`--gl2`) offscreen (EGL, Mesa llvmpipe here) in `SceneRenderer::update()`'s
order, on the mock terrain, with every default parsed from
`src/Prototypes/Scene/*.cpp`. The grid (519 x 295, 1 m wide) and the
projector (1280 x 800) placement come from `tools/ascii_preview`'s
`Sandbox` stand-in for the calibration; the font atlas and its
coverage-sorted ramp follow `AsciiFontAtlas` and `SceneStylizer` (Pillow).

    python3 scene_preview.py styles              # out/style_*.png (+ _zoom), dither palettes
    python3 scene_preview.py sequence --style 1  # out/sequence: frames, GIF, strip (2 = dither)
    python3 scene_preview.py noise               # +-1 mm per frame, flat mock, 60 s (--scene-mult 2 to speed up)
    python3 scene_preview.py compare             # GL2 vs GL3
    python3 scene_preview.py timing              # per pass, on this GL (software: not the installation PC)
    ./build.sh && python3 scene_preview.py identity   # succession/water bit-identical with Scene reading

Differences from the app, by design: the preview's "dig" subtracts a
dimple from the mock height texture on the CPU (a stand-in for a hand in
the sand); the autonomous drops use numpy's generator instead of
`std::mt19937` (same rule, different sequence).
Needs numpy, Pillow, PyOpenGL; `identity` also needs g++ (`build.sh`).
