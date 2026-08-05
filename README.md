# Shifting Sands - Ecosystem

An augmented-reality sandbox for critical environmental engagement, built as a
modernized and extended fork of [Magic Sand](https://github.com/thomwolf/Magic-Sand).

This project is part of a Digital Media MRP at York University (PiET Lab). It
ports the original Magic Sand codebase to a current toolchain and reframes the
sandbox around ecological attention; moving from a topographic visualization
toward a living terrain of moisture, vegetation, and simulated ecosystem dynamics.

---

## What this fork adds

**Modernization.** The upstream Magic Sand was last updated in 2017 and targets
openFrameworks 0.9.3, which no longer builds on current toolchains. This fork:

- Ports the codebase to **openFrameworks 0.12.1**.
- Replaces the deprecated ofxDatGui interface with an **ofxImGui** immediate-mode GUI.
- Migrates the settings system to the rewritten **ofXml** API.
- Adds a **C++17 compatibility layer** for the bundled dlib.
- Documents a working path for running a **Kinect model 1473** on modern
  openFrameworks on Windows (see `PATCHES.md`).

**Ecosystem direction (in progress).** The rendering and interaction model is
being reworked from a height-based colormap toward an ecosystem simulation:
moisture and water accumulation, vegetation that grows and recedes over time,
and terrain-driven dynamics. This is the core creative-research contribution and
is under active development.

---

## Requirements

- openFrameworks 0.12.1
- Addons: ofxCv, ofxImGui, ofxKinect, ofxOpenCv, ofxXmlSettings
- A Kinect v1 (Xbox 360 / Kinect for Windows). See `PATCHES.md` for
  model-1473-specific notes on Windows.
- A projector and a sandbox surface.

## Building

See `PATCHES.md` for the addon modifications this fork depends on (some are
Windows-and-Kinect-1473-specific and are not needed on Linux). In brief:

1. Install openFrameworks 0.12.1 and the addons listed above.
2. Apply the addon patches documented in `PATCHES.md`.
3. Open the project and build.

## Calibration

Follow the standard Magic Sand calibration flow: flatten the sand, manually
define the sand region, run automatic kinect/projector calibration, and complete
the raised-board step. In this fork, calibration confirmation prompts are ImGui
popups (click OK to advance each step).

---

## Credits & license

## Credits & license

This is a fork of **Magic Sand** by Thomas Wolf and Rasmus R. Paulsen, which in
turn draws on the Augmented Reality Sandbox by Oliver Kreylos (UC Davis). All
original work remains under its original license (GNU GPL v2). This fork is
distributed under the same terms. See `COPYING` for the full license text.

Modifications and ecosystem extensions © 2026, PiET Lab / York University.
