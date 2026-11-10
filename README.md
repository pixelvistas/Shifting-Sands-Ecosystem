# Shifting Sands - Ecosystem

An augmented reality (AR) sandbox for critical environmental engagement, built as a
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
  (ofxImGui needs the two small fixes in `PATCHES.md`, sections 1-2)
- A Kinect v1 (Xbox 360 model 1414, model 1473, or Kinect for Windows)
- Kinect driver stack (see `PATCHES.md`, section 3):
  - libfreenect 0.7.5 (OpenKinect, tag `v0.7.5`), built from source by the
    scripts in `tools/drivers/`
  - libusb 1.0.22 or later (Windows: 1.0.30 from a pinned vcpkg; Linux: the distribution
    package on Linux)
  - Windows: the libusbK driver, installed with [Zadig](https://zadig.akeo.ie/)
    for each Kinect sub-device
  - Linux: libfreenect's udev rule (`platform/linux/udev/51-kinect.rules`)
  - Build tools: Git, CMake 3.15 or later, and Visual Studio 2022/2026 with
    "Desktop development with C++" (Windows) or `build-essential` and
    `pkg-config` (Linux)
- A projector and a sandbox surface.

Platform status (see `docs/DRIVER_TEST.md`):
- **Windows:** the upstream stack streams depth from a model 1473 (tested
  2026-11-07, Release x64, libfreenect 0.7.5, libusb 1.0.30, libusbK). In that
  test RUN! did not load the saved calibration, so calibrate at the start of
  each session; the cause has not been investigated.
- **Linux:** not yet tested on hardware. The driver stack compiles, links and
  initialises, but the project itself does not yet build with GCC (see
  `docs/DEVLOG.md`).

## Building

### Windows (Visual Studio)

1. Install openFrameworks 0.12.1 and the addons listed above; apply
   `PATCHES.md` sections 1-2 to ofxImGui. Place this project in
   `of_v0.12.1/apps/myApps/`.
2. From a "Developer PowerShell for VS", in the project folder, run:

       powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1

   This builds libfreenect 0.7.5 against libusb 1.0.30, installs it into
   `addons/ofxKinect/libs/libfreenect/`, copies the patched `ofxKinect.cpp`,
   and prints both versions.
3. Install the libusbK driver with Zadig for each Kinect sub-device
   (`docs/DRIVER_TEST.md`, Windows steps).
4. Open `Magic-Sand-Ecosystem.sln`, select the **Release** configuration and
   build. `KinectDriver.props` selects the driver stack (`upstream` by default).

### Linux (Makefile)

1. Install openFrameworks 0.12.1 for Linux, run its
   `scripts/linux/<distro>/install_dependencies.sh` and
   `scripts/linux/compileOF.sh`, then install the addons and apply
   `PATCHES.md` sections 1-2. Place this project in `of_v0.12.1/apps/myApps/`.
2. `sudo apt install git cmake build-essential pkg-config libusb-1.0-0-dev`
3. In the project folder: `tools/drivers/build_libfreenect_linux.sh --install-udev`,
   then unplug and replug the Kinect.
4. `make Release` and `make RunRelease`.

Known issue: with GCC in C++20 mode, `make Release` currently stops on the
project's C++17 compatibility shim (`src/KinectProjector/KinectProjector.h`)
and, depending on the ofxCv revision, on ofxCv's `Tracker.h`. Neither is part of
the driver stack; see `docs/DEVLOG.md` (2026-11-03).

### Fallback: openFrameworks 0.9.3 libfreenect (Windows)

If the upstream stack does not stream depth on a given machine, restore the
libfreenect transplant that was used before:
`tools\drivers\build_libfreenect_windows.ps1 -RestoreFallback`, then set
`KinectDriverStack` to `fallback` in `KinectDriver.props` and rebuild. Details
are in `PATCHES.md`, section 4.

## Calibration

Follow the standard Magic Sand calibration flow: flatten the sand, manually
define the sand region, run automatic kinect/projector calibration, and complete
the raised-board step. In this fork, calibration confirmation prompts are ImGui
popups (click OK to advance each step).

---

## Credits & license

This is a fork of **Magic Sand** by Thomas Wolf and Rasmus R. Paulsen, which in
turn draws on the Augmented Reality Sandbox by Oliver Kreylos (UC Davis). All
original work remains under its original license (GNU GPL v2). This fork is
distributed under the same terms. See `COPYING` for the full license text.

Modifications and ecosystem extensions © 2026, PiET Lab / York University.
