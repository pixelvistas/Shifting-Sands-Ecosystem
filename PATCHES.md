# Addon Patches — Magic-Sand-Ecosystem

This fork depends on modifications to shared openFrameworks addons that live
outside this project folder (in `of_0.12.1/addons/`). A normal project commit
does not capture them. The modified files are copied here under `_addon_patches/`
mirroring their original paths. To rebuild on a fresh machine, install the stock
addons, then apply these changes.

The ofxImGui fixes (sections 1-2) are cross-platform. The Kinect driver stack
(section 3) replaces the earlier Windows-only libfreenect transplant with a
pinned upstream libfreenect build on Windows and Linux; the transplant is kept
as a documented fallback (section 4).

Target environment (Windows): openFrameworks 0.12.1, Visual Studio 2026 (v143/v144
toolset), Kinect model 1473 (K4W).

---

## 1. ofxImGui/src/imconfig.h  (CROSS-PLATFORM — still needed on Linux)

PROBLEM: ofxImGui's config told ImGui to use openFrameworks' `ofIndexType` as its
draw-index type via `#define ImDrawIdx ofIndexType`. This required oF headers to be
fully parsed before imgui.h, which broke depending on include order, producing
dozens of "ofIndexType is not a valid template argument" errors deep in imgui.h.

FIX: Around line 103, change:
    #define ImDrawIdx ofIndexType
to:
    #define ImDrawIdx unsigned int

This removes ImGui's dependency on an openFrameworks type entirely.

---

## 2. ofxImGui/src/BaseEngine.cpp  (CROSS-PLATFORM — still needed on Linux)

PROBLEM: BaseEngine.cpp calls `ofToString(...)` but only includes
ofAppBaseWindow.h, ofAppRunner.h, and imgui.h — not the header that declares
ofToString. On oF 0.12.1 this surfaces as "ofToString: identifier not found"
and a knock-on "operator = is ambiguous".

FIX: In the include block near the top, add:
    #include "ofUtils.h"

---

## 3. Kinect driver stack: upstream libfreenect v0.7.5 + current libusb (current path)

STATUS (branch `ss/current-drivers`): Windows recorded as streaming depth from
the model 1473 on this path (2026-11-07, `docs/DRIVER_TEST.md`), with one
failed check: RUN! did not load the saved calibration (cause not investigated;
recalibrate each session). Linux not yet tested on hardware. Section 4 (the
0.9.3 transplant) remains available as a fallback.

Pinned versions:

| Component   | Version | Source |
|-------------|---------|--------|
| libfreenect | 0.7.5   | OpenKinect/libfreenect tag `v0.7.5`, commit `09a1f098040d00e6070c18174904547ec31d2774` (2024-01-06) |
| libusb (Windows) | 1.0.30 | vcpkg tag `2026.10.16` (commit `9eec002df1cd109f8f104cd966a763daf9751489`), port `libusb` 1.0.30, triplet `x64-windows-static-md` plus the project's MSVC toolset (overlay triplet `x64-windows-static-md-v143`) |
| libusb (Linux)   | distro package, >= 1.0.22 (Ubuntu 24.04: 1.0.27) | `libusb-1.0-0-dev` |
| Windows device driver | libusbK, installed with Zadig | one entry per Kinect sub-device |

Since libusb 1.0.22, libusbK isochronous transfers are supported natively on
Windows, so neither a custom libusb build nor the `libusb10emu` layer of the
0.9.3 transplant is required.

### 3a. Building and installing the library

Run the script for the platform from the project folder:

    Windows:  powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1
    Linux:    tools/drivers/build_libfreenect_linux.sh

Each script fetches libfreenect at the pinned tag and verifies the commit,
obtains libusb (vcpkg on Windows, the distribution package on Linux), builds
the static C library only (`freenectstatic`; examples, fakenect, C sync
wrapper, C++ wrapper and Python bindings off; `BUILD_REDIST_PACKAGE=ON`, so no
firmware is fetched), installs it into `<OF>/addons/ofxKinect/libs/libfreenect/`
and prints the libfreenect and libusb versions. The folder that was there
before is moved, not deleted, to `<OF>/addons/ofxKinect/_libfreenect_backups/<timestamp>/`
(outside `libs/`, so no build system scans it).

Installed layout:

    libs/libfreenect/include/          libfreenect.h, libfreenect_registration.h,
                                       libfreenect_audio.h, freenect_internal.h,
                                       usb_libusb10.h  [+ libusb-1.0/libusb.h on Windows]
    libs/libfreenect/lib/vs/x64/{Release,Debug}/   freenect.lib, libusb-1.0.lib  (Windows)
    libs/libfreenect/lib/linux64/libfreenect.a                                  (Linux)
    libs/libfreenect/{APACHE20,GPL2,CONTRIB,VERSIONS.txt}

The two internal headers are installed because `ofxKinect.cpp` includes
`freenect_internal.h` and reads `freenect_device` fields
(`registration.zero_plane_info`, `motor_control_with_audio_enabled`). They are
copied from the same checkout as the library, so the structure layout matches.

Differences from what ofxKinect ships, and how they are handled without
renaming or hand-editing generated files:

- ofxKinect 0.12.1 compiles libfreenect from source. On Linux the oF Makefile
  links `libs/*/lib/linux64/*.a` automatically and `addon_config.mk` already
  adds libusb-1.0 via pkg-config, so no project setting changes.
- On Windows the project lists the libfreenect `.c` files explicitly. The
  project now imports `KinectDriver.props`, whose `KinectDriverStack` property
  selects `upstream` (default: link the prebuilt `freenect.lib` and
  `libusb-1.0.lib`, compile no libfreenect sources) or `fallback` (section 4).
  A pre-build check stops with an explicit message if the addon folder does
  not match the selected stack.
- libfreenect's `CMakeLists.txt` overwrites the per-configuration flag
  variables, which drops `/MD`. The Windows script sets policy CMP0091 and
  `CMAKE_MSVC_RUNTIME_LIBRARY` on the command line, so the library uses the
  dynamic CRT (`/MD`, `/MDd` for Debug) like openFrameworks.
- Both libraries are compiled with the MSVC toolset the project itself uses
  (`<PlatformToolset>` in the `.vcxproj`, currently v143), read by the script.
  libusb's own MSVC Release build uses whole-program optimisation (`/GL`), and
  `/GL` objects can only be linked by the same compiler version; built with a
  newer Visual Studio's default toolset, the project's Release link fails with
  `C1900: Il mismatch between 'P1' version ... and 'P2' version ...`.
- Upstream builds both a shared and a static library named `freenect.lib`;
  only the static target is built, so the import library cannot overwrite it.
- `ofxKinectExtras.cpp` (stock ofxKinect) contains the Kinect audio firmware
  as a byte array, and the stock `ofxKinectExtras.h` always defines
  `OFX_KINECT_EXTRA_FW`, so stock `ofxKinect.cpp` always hands that firmware to
  libfreenect at start-up. libfreenect only uses it when the audio sub-device is
  opened, which happens only with `MOTOR` selected. The patched `ofxKinect.cpp`
  therefore passes the firmware only when `OFXKINECT_OPEN_MOTOR=1`, and the
  Visual Studio project compiles `ofxKinectExtras.cpp` only for `fallback` or
  `KinectOpenMotor=true`; a camera-only Windows build contains no firmware. On
  Linux the Makefile still compiles the stock file, but in camera-only mode
  nothing references it. (An earlier version of this section said
  `OFX_KINECT_EXTRA_FW` was undefined; that was wrong, and the first Windows
  build failed with four LNK2019 errors as a result.)

### 3b. ofxKinect/src/ofxKinect.cpp: sub-device selection (CROSS-PLATFORM)

Copy: `_addon_patches/ofxKinect/src/ofxKinect.cpp` (both build scripts copy it
into the addon unless told not to, keeping the original as
`ofxKinect.cpp.stock-backup`).

Note on history: the file previously stored here differed from stock
ofxKinect 0.12.1 by one blank line. Stock ofxKinect already calls
`freenect_select_subdevices(..., MOTOR | CAMERA)`; the "fix" recorded in the
earlier version of this section was therefore the stock setting, and the
working Windows stream came from the library transplant in section 4.

Change: the sub-device selection is a single setting, `OFXKINECT_OPEN_MOTOR`:

- `0` (default): `FREENECT_DEVICE_CAMERA` only. With libfreenect 0.7.5, a model
  1473 or Kinect for Windows reaches its motor and LED through the audio
  sub-device, and selecting MOTOR makes libfreenect enable AUDIO and upload
  audio firmware (`audios.bin`). Opening the camera alone needs no firmware.
  The sandbox does not use the motor: a search of `src/` finds no call to
  `setCameraTiltAngle`, `setLed` or the accelerometer, and the "Tilt X"/"Tilt Y"
  sliders in `KinectProjector.cpp` (inside the commented-out ofxDatGui
  `setupGui()`) only rotate the software base-plane normal.
- `1`: `FREENECT_DEVICE_MOTOR | FREENECT_DEVICE_CAMERA`, the stock behaviour,
  kept for comparison and used by the fallback.

Where to set it: `KinectOpenMotor` in `KinectDriver.props` (Visual Studio;
defaults to `false` for `upstream` and `true` for `fallback`), or
`PROJECT_DEFINES = OFXKINECT_OPEN_MOTOR=1` in `config.make` (Makefile). The
selected mode is logged at startup (`ofxKinect: init(): sub-devices ...`).
In camera-only mode `hasCamTiltControl()`/`hasLedControl()` return false;
libfreenect's tilt and LED calls return immediately without USB traffic.

### 3c. ssize_t guard

Not needed on the upstream path. The guard in section 4 existed because the
0.9.3 copy of `platform/windows/unistd.h` declared `typedef long ssize_t`
(32-bit on Windows), which conflicts with the 64-bit definition elsewhere in
the build (libusb's `libusb.h` declares `typedef SSIZE_T ssize_t` under MSVC).
On the upstream path the application never compiles libfreenect sources and
never includes `unistd.h` (the public `libfreenect.h` includes only
`stdint.h` and `winsock.h`); libfreenect itself is compiled by its own CMake
build with its unmodified 0.7.5 `unistd.h`, which declares `ssize_t` as
`__int64` on Win64. If that CMake build fails on `ssize_t`, record it in
`docs/DRIVER_TEST.md` rather than patching the upstream file.

---

## 4. FALLBACK: libfreenect transplant from openFrameworks 0.9.3 (WINDOWS 1473)

This is the configuration that produced a working depth stream on Windows
before branch `ss/current-drivers`. It is kept, unchanged, as a fallback.

PROBLEM it solved: oF 0.12.1's bundled libfreenect could not open the 1473 on
Windows ("could not open device 0"); oF 0.9.3's libfreenect (2016) could.

Contents: `_addon_patches/fallback-libfreenect-0.9.3/libfreenect/` is the
complete libfreenect folder from the oF 0.9.3 VS release
(of_v0.9.3_vs_release.zip, `addons/ofxKinect/libs/libfreenect/`), with one
edit, the broadened ssize_t guard in `platform/windows/unistd.h`:

    #if !defined(_SSIZE_T_) && !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T)
    #define _SSIZE_T_
    #define _SSIZE_T_DEFINED
    typedef long ssize_t;
    #endif

(The transplanted copy guarded only on `_SSIZE_T_`; the modern Windows SDK
already defines ssize_t, which caused a redefinition error.)

To restore it:

1. `powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1 -RestoreFallback`
   (moves the current `libs/libfreenect` to `_libfreenect_backups/` and copies
   the fallback folder into place; `libs/libusb` from the stock oF download is
   never modified, and the fallback links its `libusb.lib`).
2. Set `<KinectDriverStack>fallback</KinectDriverStack>` in `KinectDriver.props`
   (or build with `/p:KinectDriverStack=fallback`). This also selects
   `KinectOpenMotor=true` (MOTOR | CAMERA), the only sub-device combination
   recorded as working with this library.
3. Rebuild. If depth does not stream, put each sub-device back on the driver
   it had before the libusbK change (recorded in `docs/DRIVER_TEST.md`, step A0)
   with Zadig. Which driver the original working installation used is not
   recorded elsewhere in this repository.

On Linux this transplant was never needed; the stock oF 0.12.1 libfreenect
sources (restorable from `_libfreenect_backups/`) or section 3 apply.

---

## In-project changes (already tracked in this repo, listed for completeness)

- src/main.cpp — ofWindowSettings width/height → setSize()/getWidth()/getHeight()
- src/KinectProjector/KinectProjector.h/.cpp — C++17 binary_function shim;
  ofxDatGui→ofxImGui GUI port (immediate-mode drawGui); StubModal→real ImGui
  popups for calibration prompts; ofXml 0.9.3→0.12.1 API port; std::min/max
  qualification; buffer/ROI snapshot guards in filter()/applySpaceFilter()/
  updateGradientField(); chessboard corner-order normalization in
  CalibrateNextPoint(); savePointPair() filename bug fixed.
- src/KinectProjector/KinectGrabber.cpp — removed std::move on filtered.send()
  (fixed producer/consumer buffer race); <algorithm> include; buffer re-alloc
  after kinect.open().
- src/KinectProjector/KinectProjectorCalibration.h/.cpp — C++17 shim; ofXml port.
- src/SandSurfaceRenderer/SandSurfaceRenderer.cpp — ofXml port; addTexCoord→
  glm::vec2; GUI stripped.
- src/Games/*.cpp — ofXml port (ReferenceMapHandler, SandboxScoreTracker).
- addons.make — dropped ofxDatGui/ofxModal/ofxParagraph; kept ofxCv, ofxImGui,
  ofxKinect, ofxOpenCv, ofxXmlSettings.
