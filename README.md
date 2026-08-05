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

Shifting Sands - Ecosystem is a modernized and extended fork of Magic Sand (v1.5.4.1, 2017) by Thomas Wolf and Rasmus R. Paulsen.

Magic Sand is itself a partial port of the Augmented Reality Sandbox / SARndbox by Oliver Kreylos (UC Davis, KeckCAVES) into openFrameworks, and is additionally adapted from ofxKinectProjectorToolkit by Gene Kogan.

This fork vendors a copy of libfreenect (from the openFrameworks 0.9.3 release of ofxKinect) under _addon_patches/, to support Kinect model 1473 on Windows. libfreenect is developed by the OpenKinect project and is dual-licensed Apache 2.0 / GPL 2.0.

All upstream work remains under its original license, the GNU General Public License v2 (or later). This fork is distributed under the same terms; see COPYING for the full text. Files modified in this fork carry modification notices per GPLv2 §2(a).

Modifications, the SandboxModule extension point, and the ecosystem modules are copyright © 2026 Shelby Murchie, and were developed as part of a Digital Media Major Research Project at York University (PiET Lab).
