# Development log

## 2026-11-03: Kinect driver stack on current upstream libfreenect (branch `ss/current-drivers`)

**Objective.** Replace the Windows-only transplant of the openFrameworks 0.9.3
(2016) libfreenect folder with the current upstream release, libfreenect 0.7.5
(OpenKinect, January 2024), built against a current libusb on Windows and
Linux, with a rebuild path that a third party can follow. The work is
time-boxed: if the model 1473 does not stream depth on the new stack by the end
of the hardware test (`docs/DRIVER_TEST.md`), the 0.9.3 path is kept and the
failure recorded. Nothing in `src/Games/`, the shaders or the colour maps was
changed.

### Decisions

1. **Pinned versions.** libfreenect tag `v0.7.5`, verified against commit
   `09a1f098040d00e6070c18174904547ec31d2774`. Windows libusb from vcpkg tag
   `2026.10.16` (commit `9eec002d…`), whose `libusb` port is 1.0.30, triplet
   `x64-windows-static-md`. Linux libusb from the distribution package, with a
   minimum of 1.0.22 enforced by the script (Ubuntu 24.04 supplies 1.0.27).
2. **Prebuilt static library instead of compiled-in sources.** ofxKinect 0.12.1
   compiles libfreenect from `libs/libfreenect/src`. The scripts instead build
   the upstream `freenectstatic` target with CMake (C library only; examples,
   fakenect, C sync, C++ wrapper and Python off) and install headers and the
   archive into `libs/libfreenect/`. On Linux the oF Makefile picks the archive
   up from `lib/linux64/` with no project change; on Windows a property sheet
   (`KinectDriver.props`, property `KinectDriverStack`) selects between the
   prebuilt library and the old compiled-in sources. A pre-build check stops
   with an explicit message if the addon folder and the selection disagree.
3. **Internal headers are installed.** `ofxKinect.cpp` includes
   `freenect_internal.h` and reads `freenect_device` fields
   (`registration.zero_plane_info`, `motor_control_with_audio_enabled`). The
   scripts therefore install `freenect_internal.h` and `usb_libusb10.h` from the
   same checkout as the library, so the structure layout seen by ofxKinect
   matches the compiled code.
4. **Camera sub-device only, as a single setting.** In libfreenect 0.7.5, on a
   model 1473 or K4W, selecting `FREENECT_DEVICE_MOTOR` makes the library also
   enable `FREENECT_DEVICE_AUDIO` and upload audio firmware (`audios.bin`)
   before motor control is available (`usb_libusb10.c`, `fnusb_open_subdevices`).
   The sandbox does not use the motor: `src/` contains no call to
   `setCameraTiltAngle`, `setLed` or the accelerometer, and the "Tilt X/Y"
   sliders in `KinectProjector.cpp` sit in the commented-out ofxDatGui
   `setupGui()` and only rotate the software base-plane normal. The default is
   therefore `FREENECT_DEVICE_CAMERA` (`OFXKINECT_OPEN_MOTOR=0`), with
   `MOTOR | CAMERA` selectable for comparison (`KinectOpenMotor` in
   `KinectDriver.props`, or `PROJECT_DEFINES` in `config.make`). libfreenect's
   tilt and LED functions return without USB traffic when the motor sub-device
   is not enabled, so the unconditional calls in ofxKinect's thread loop are
   harmless. No firmware file is shipped, and `BUILD_REDIST_PACKAGE=ON` keeps
   the libfreenect build from fetching one. In the upstream Windows
   configuration `ofxKinectExtras.cpp` (stock ofxKinect, which embeds firmware
   bytes for use under `OFX_KINECT_EXTRA_FW`) is not compiled.
5. **ssize_t guard dropped on the upstream path.** The guard was needed
   because the 0.9.3 `platform/windows/unistd.h` declared a 32-bit `ssize_t`,
   conflicting with libusb's `typedef SSIZE_T ssize_t` under MSVC. With a
   prebuilt library the application never compiles libfreenect sources or
   includes `unistd.h`; the public `libfreenect.h` includes only `stdint.h` and
   `winsock.h`. Upstream's own `unistd.h` (used only inside the CMake build)
   declares a 64-bit `ssize_t` on Win64 and is left unmodified.
6. **MSVC runtime.** libfreenect's `CMakeLists.txt` overwrites the
   per-configuration flag variables, which drops `/MD`. The Windows script
   passes `CMAKE_POLICY_DEFAULT_CMP0091=NEW` and `CMAKE_MSVC_RUNTIME_LIBRARY`,
   so the library is built against the dynamic CRT like openFrameworks without
   editing an upstream or generated file. Only the static target is built,
   because the shared target's import library has the same name.
7. **Fallback preserved.** The 0.9.3 transplant moved, unchanged, from
   `_addon_patches/ofxKinect/libs/libfreenect/` to
   `_addon_patches/fallback-libfreenect-0.9.3/libfreenect/`. The Windows script
   restores it with `-RestoreFallback`; `KinectDriverStack=fallback` then
   compiles it from source with `MOTOR | CAMERA`, as in the build recorded as
   working. The scripts never delete an existing `libs/libfreenect`; they move
   it to `ofxKinect/_libfreenect_backups/<timestamp>/`, outside `libs/`, so no
   build system scans it.
8. **Correction to earlier documentation.** The `ofxKinect.cpp` previously
   stored in `_addon_patches` differed from stock ofxKinect 0.12.1 by a single
   blank line: stock ofxKinect already selects `MOTOR | CAMERA`. The working
   Windows stream is therefore attributable to the library transplant alone,
   not to a sub-device change, and `PATCHES.md` now says so.

### Verification performed in this session (Linux container, Ubuntu 24.04, GCC 13)

- `tools/drivers/build_libfreenect_linux.sh` ran end to end against a copy of
  stock ofxKinect 0.12.1: commit verified, `libfreenect.a` built, layout
  installed, versions reported (libfreenect 0.7.5, libusb 1.0.27).
- A test program using the same symbols and internal fields as ofxKinect
  linked against the installed archive and system libusb and ran
  (`freenect_init`, camera-only selection confirmed, 0 devices).
- Full project build with the openFrameworks 0.12.1 Linux release, stock
  ofxKinect plus the patched `ofxKinect.cpp`, jvcleave/ofxImGui `da2d27b` with
  `PATCHES.md` sections 1-2, kylemcdonald/ofxCv `209a398`:
  - The patched `ofxKinect.cpp` compiled against the 0.7.5 headers without
    error.
  - The build failed elsewhere, for two reasons unrelated to the driver stack:
    (a) the project's C++17 shim in `src/KinectProjector/KinectProjector.h`
    and `KinectProjectorCalibration.h` redefines `std::binary_function` /
    `std::unary_function`, which libstdc++ still declares in C++20 mode
    ("redefinition of 'struct std::binary_function'"); (b) ofxCv
    `Tracker.h:128` declares the constructor as `Tracker<T>()`, which GCC 11+
    rejects in C++20 ("expected unqualified-id before ')' token").
  - In a scratch copy only (not in this repository), guarding both shims with
    `#if defined(_MSC_VER)` and changing `Tracker<T>()` to `Tracker()` made the
    project compile and link. The binary contains libfreenect 0.7.5 statically
    and loads `libusb-1.0.so.0` (1.0.27). Run headless under Xvfb with no Kinect
    attached, it logged `ofxKinect.driver: init(): sub-devices CAMERA only (no
    motor, LED or firmware)`, then "no available devices found" and retried,
    without crashing. Rebuilt with `PROJECT_DEFINES=OFXKINECT_OPEN_MOTOR=1`, it
    logged `MOTOR | CAMERA`.
  - Consequently, the Linux build is verified for the driver stack (compile,
    link, initialisation) but **not** for the project as committed, which does
    not compile on GCC/C++20 because of (a); (b) depends on the ofxCv revision.
    Neither fix was applied to the repository: (a) touches project code outside
    this task's scope and (b) is in a third-party addon. Both are open items.
- `KinectDriver.props` was evaluated with MSBuild (dotnet 8) using a stub
  project: defaults, both stacks, the motor override and all four error
  conditions behaved as intended.
- **Not verified:** the Windows script has not been run or parsed in this
  session (no Windows host; installing PowerShell for a parse check was not
  permitted), the Visual Studio build of the modified `.vcxproj` has not been
  compiled, and no hardware test has been run on either platform.

### Open items

- Hardware test on Windows and Linux (`docs/DRIVER_TEST.md`).
- Linux portability: C++17 shim guard in `KinectProjector.h` /
  `KinectProjectorCalibration.h`; ofxCv revision or patch for `Tracker.h`.
- If Windows passes, decide whether the fallback can be retired.

## 2026-11-07: vcpkg pin moved after the first Windows build attempt

The first run of `build_libfreenect_windows.ps1` on the Windows test machine
failed while vcpkg built libusb: `vcpkg_fixup_pkgconfig` (called by the libusb
port) downloads MSYS2 packages at versions fixed by the vcpkg tag, and
`msys2-runtime-3.5.4-2` (required by tag `2025.01.13`) returned HTTP 404 on all
six mirrors. Other MSYS2 packages downloaded in the same run, so the network was
not at fault: MSYS2 had deleted that version. The pin was moved to vcpkg
`2026.10.16` (commit `9eec002df1cd109f8f104cd966a763daf9751489`; libusb port
1.0.30, which requires `msys2-runtime-3.6.10-4` and `pkgconf-3.0.7-1`). This is
a property of vcpkg's MSYS2 dependency, not of libusb: any fixed vcpkg tag will
eventually fail the same way, so the comment above the pin in the script
explains how to move it. The Linux path is unaffected (distribution libusb).

Correction, same day: the commit first recorded for tag `2026.10.16`
(`5d4a3dc9…`) was the annotated tag object, not the commit it points to, so the
script's commit check stopped the second run. The pinned commit is
`9eec002df1cd109f8f104cd966a763daf9751489` (the tag's `^{}` entry). The
libfreenect pin was already a commit hash (`v0.7.5^{}` = `09a1f098…`).

## 2026-11-07: first Windows build - libraries built, link error fixed

`build_libfreenect_windows.ps1` completed on the Windows test machine
(libfreenect 0.7.5 at `09a1f098…`; libusb 1.0.30, vcpkg 2026.10.16,
`x64-windows-static-md`). The Visual Studio Release build then failed with four
LNK2019 errors: `ofxKinectContext::init()` referenced
`ofxKinectExtras::getFWData1473/getFWSize1473/getFWDatak4w/getFWSizek4w`.
Cause: decision 4 above was based on a wrong reading. The stock
`ofxKinectExtras.h` defines `OFX_KINECT_EXTRA_FW` unconditionally, so stock
`ofxKinect.cpp` always passes the embedded audio firmware to libfreenect, and
excluding `ofxKinectExtras.cpp` from the upstream build left those calls
unresolved. Fix: the patched `ofxKinect.cpp` sets the firmware addresses only
when `OFXKINECT_OPEN_MOTOR=1` (libfreenect uses them only when it opens the
audio sub-device, i.e. only in MOTOR mode), and the project compiles
`ofxKinectExtras.cpp` for `fallback` or `KinectOpenMotor=true`. Checked on the
Linux build in this session: camera-only `ofxKinect.o` has no reference to the
firmware functions and links; MOTOR mode has all four and links. Consequence for
the optional MOTOR comparison: libfreenect will now receive the embedded
firmware, so it may be able to open the audio sub-device, rather than failing
on a missing `audios.bin` as previously predicted.

## 2026-11-07: Release link failure (C1900) - libraries now built with the project's toolset

After the link fix above, the Debug build ran and streamed depth with the new
stack, at about 6 fps; the user then switched to Release (the same Debug-vs-
Release cause recorded on 2026-09-24 for low frame rates). The Release link
failed with `C1900: Il mismatch between 'P1' version '20251208' and 'P2' version
'20240319'` and `LNK1257: code generation failed`. Cause: the script built
libfreenect with CMake's default toolset and libusb with vcpkg's default, both
the newest installed (Visual Studio 2026), while the project builds with v143
(Visual Studio 2022 toolset). libusb's MSVC Release project uses whole-program
optimisation (`/GL`), whose intermediate code can only be read by the same
compiler version, so the v143 linker rejected it; Debug has no `/GL`, which is
why Debug linked. Fix: the script reads `<PlatformToolset>` from the project
(override `-PlatformToolset`), builds libusb with an overlay vcpkg triplet
`x64-windows-static-md-<toolset>` that sets `VCPKG_PLATFORM_TOOLSET`, and passes
`-T <toolset>` to CMake for libfreenect. Not yet re-run on Windows.

## 2026-11-07: Windows hardware test - depth streams on the upstream stack

With libusb and libfreenect rebuilt with the v143 toolset, the Release x64 build
linked and ran on the Windows test machine with the model 1473 (camera and
audio already on libusbK; no motor device on this model). Per the user, checks
1-4 and 6 of `docs/DRIVER_TEST.md` passed: depth streamed, the camera-only log
line appeared, frame rate and console errors were as expected, and the ROI
elevation on flat sand matched the old build within 1 mm. Check 5 failed: RUN!
did not load the saved calibration. The cause was not investigated, so it is
not attributed to the driver stack (the calibration files were copied into the
test folder from the dev clone, which is on a different branch). The user
decided to recalibrate at the start of each session rather than investigate.
The earlier ~6 fps reading was a Debug build. Outcome of the time box: the new
stack works on Windows, so it stays the default; the 0.9.3 fallback is kept.
Exact timings, frame rates and elevation readings are to be filled into the
results table by the user.

