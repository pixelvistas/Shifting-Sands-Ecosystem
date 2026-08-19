# Addon Patches — Magic-Sand-Ecosystem

This fork depends on modifications to shared openFrameworks addons that live
outside this project folder (in `of_0.12.1/addons/`). A normal project commit
does not capture them. The modified files are copied here under `_addon_patches/`
mirroring their original paths. To rebuild on a fresh machine, install the stock
addons, then apply these changes.

IMPORTANT: Most of these are **Windows-specific workarounds** for running a
Kinect model 1473 on modern openFrameworks. On Linux, libfreenect generally
handles Kinect v1 far more gracefully, so several of these (the libfreenect
transplant, the ssize_t guard) may be unnecessary or need a different approach.
The ofxImGui fixes are cross-platform and still apply.

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

## 3. ofxKinect/src/ofxKinect.cpp  (WINDOWS 1473-specific)

PROBLEM: On the Kinect 1473/K4W, libfreenect's default open sequence tries to
open the motor subdevice, which fails and corrupts init:
"Failed to open motor subdevice", "iso_callback -5".

FIX: In the `freenect_select_subdevices` call (~line 926), the working
configuration for the 1473 was:
    freenect_select_subdevices(kinectContext,
        (freenect_device_flags)(FREENECT_DEVICE_MOTOR | FREENECT_DEVICE_CAMERA));
(Dropping AUDIO. Note: various combinations were tested; MOTOR|CAMERA with the
0.9.3 libfreenect transplant below is what produced a working stream. On Linux,
the stock configuration may work without change.)

---

## 4. ofxKinect/libs/libfreenect/  (WINDOWS 1473-specific — whole folder)

PROBLEM: oF 0.12.1's bundled libfreenect could not open the 1473 on Windows
("could not open device 0"). oF 0.9.3's libfreenect can.

FIX: The entire libfreenect folder was replaced with the one from the oF 0.9.3
VS release (of_v0.9.3_vs_release.zip → addons/ofxKinect/libs/libfreenect/).
The original 0.12.1 folder was backed up as `libfreenect_0.12.1_backup`.

Sub-fix (ssize_t): The transplanted 0.9.3 libfreenect's bundled `unistd.h`
defines `ssize_t` with a guard that only checks `_SSIZE_T_`. Modern Windows SDK
already defines ssize_t, causing a redefinition error. The guard was broadened to:
    #if !defined(_SSIZE_T_) && !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T)
    #define _SSIZE_T_
    #define _SSIZE_T_DEFINED
    typedef long ssize_t;
    #endif

On Linux this whole transplant is likely unnecessary — use the stock ofxKinect
libfreenect and Kinect udev rules instead.

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
