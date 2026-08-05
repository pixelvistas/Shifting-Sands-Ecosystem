# Changelog

All notable divergences of **Shifting Sands - Ecosystem** from its upstream,
[Magic Sand](https://github.com/thomwolf/Magic-Sand) v1.5.4.1 (10 October 2017).

Format loosely follows [Keep a Changelog](https://keepachangelog.com/).
This project inherits GNU GPL v2 (or later) from upstream.

---

## [Unreleased]

### Added

- `SandboxModule` base class: a formal extension point for custom modes, with
  an owned persistent state layer that survives recalibration and ROI changes.
  Upstream had no module interface — new modes were written by reading
  `ofApp.cpp` and copying what the bundled games did.
- `tools/add_modification_notices.py` — GPLv2 §2(a) compliance tooling.
- `tools/apply_addon_patches.sh` — scripted application of the addon
  modifications previously documented for manual application.

### Changed

- Module dispatch in `ofApp` moved from concrete members to a registry.

---

## [0.1.0] — 2026-07-28 — Modernization

The first working build of the upstream codebase on a current toolchain.
Upstream targets openFrameworks 0.9.3 and no longer builds as shipped.

### Changed

- Ported to **openFrameworks 0.12.1** from 0.9.3.
- Replaced the deprecated **ofxDatGui** interface with **ofxImGui**
  (immediate-mode). `StubModal` calibration prompts became real ImGui popups.
- Migrated the settings system to the rewritten **ofXml** API.
- Added a **C++17 compatibility shim** for `std::binary_function` in the
  bundled dlib.
- `main.cpp`: `ofWindowSettings` width/height → `setSize()` / `getWidth()` /
  `getHeight()`.
- `SandSurfaceRenderer`: `addTexCoord` → `glm::vec2`.
- Dropped addons `ofxDatGui`, `ofxModal`, `ofxParagraph`. Retained `ofxCv`,
  `ofxKinect`, `ofxOpenCv`, `ofxXmlSettings`. Added `ofxImGui`.

### Fixed

These are bugs in Magic Sand 1.5.4.1, not artifacts of the port. Each is
proposed upstream separately; see `UPSTREAM_PRS.md`.

- **`KinectGrabber`: producer/consumer buffer race.** Removed `std::move` on
  `filtered.send()`, which moved from a buffer still referenced by the
  consumer.
- **`KinectProjector::CalibrateNextPoint()`: chessboard corner ordering.**
  Corner order was not normalised before pairing, which could produce a valid
  calibration run with a silently incorrect transform. Calibration failure is
  the most frequently reported problem in the upstream issue tracker.
- **`savePointPair()` filename bug.**
- **Missing bounds/lifetime guards** in `filter()`, `applySpaceFilter()` and
  `updateGradientField()` — added buffer/ROI snapshot guards.
- **Buffer reallocation after `kinect.open()`**, which previously used stale
  dimensions.
- `KinectGrabber`: missing `<algorithm>` include.
- `KinectProjector`: unqualified `std::min` / `std::max`.

### Added — platform support

- Documented a working configuration for **Kinect model 1473 (K4W)** on
  openFrameworks 0.12.1 under Windows, including the libfreenect 0.9.3
  transplant and `ssize_t` guard broadening. See `PATCHES.md`.
- Addon modifications mirrored in-repo under `_addon_patches/` so a fresh
  machine can reproduce the build.

---

## History

Magic Sand releases prior to this fork:

- **1.5.4.1** — 2017-10-10 — calibration checkerboard fix; Linux makefiles.
- **1.5.4** — 2017-09-23 — Kinect FPS counter, XCode files, full-frame filter,
  depth inpainting, GUI scaling, ROI debug view.
- **1.5.0** — 2017-08-08 — first release of Magic-Sand with games.
- **1.1** — 2016-07-29 — first release under GPL.
- **1.0** — 2016-07-26 — first release.

No upstream release since October 2017.
