# Kinect driver hardware test (branch `ss/current-drivers`)

Purpose: establish, per platform, whether the Kinect model 1473 streams depth
on the upstream driver stack (libfreenect 0.7.5 + libusb 1.0.30 on Windows, camera
sub-device only), and record the result. A platform may be described as
working in `README.md` or `PATCHES.md` only after a **pass** is recorded in the
results table below.

Time box: if depth does not stream on the new stack by the end of this test,
stop, keep (or restore) the 0.9.3 fallback path (`PATCHES.md`, section 4), and
record what failed in the table and in `docs/DEVLOG.md`.

Reference values from the old (fallback) build, needed for check 5: flat sand
reads approximately **+9 mm** in "Raw elevation at ROI center" (Vegetation panel).
If possible, read this value on the old build immediately before switching,
with the sand untouched between the two readings.

---

## A. Windows

### A0. Record the current drivers first

The working fallback build runs on whatever driver each sub-device has now,
and that driver is not documented anywhere in this repository. Before running
Zadig, open Device Manager, find each Kinect entry (usually under
"libusbK USB Devices", "libusb-win32 devices" or "Universal Serial Bus
devices"), open Properties → Driver, and record:

| Sub-device | Driver provider / name | Driver version |
|------------|------------------------|----------------|
| Xbox NUI Camera |  |  |
| Xbox NUI Motor (model 1414 only; write "not present" on a 1473) |  |  |
| Audio           |  |  |

### A1. Driver installation (Zadig)

1. Connect the Kinect (USB and its mains power adapter). Wait for Windows to
   finish enumerating it.
2. Run Zadig (https://zadig.akeo.ie/). Menu **Options → List All Devices**.
3. For each of the following entries, select it in the drop-down, set the
   target driver (right of the green arrow) to **libusbK**, and click
   **Replace Driver** (or **Install Driver**):
   - [ ] "Xbox NUI Camera"
   - [ ] "Xbox NUI Motor" (model 1414 only; a model 1473 has no separate motor
     device, because its motor and LED are controlled through the audio device)
   - [ ] "Xbox Kinect Audio" or "Kinect for Windows Audio" (whichever is listed)

   The application opens only the camera. The motor and audio entries are
   still converted so that every sub-device uses the same driver; libfreenect
   0.7.5 also opens the audio sub-device briefly at start-up to set the LED as
   a keep-alive on 1473/K4W models, and logs an error, without stopping, if it
   cannot.
4. Record the libusbK driver version that Zadig shows (e.g. `libusbK (v3.1.0.0)`): ____________
5. Unplug and replug the Kinect USB cable.

### A2. Build

1. Open "Developer PowerShell for VS" in the project folder.
2. [ ] `powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1`
   completes without error. Copy the final two lines:
   - libfreenect version: ____________
   - libusb version: ____________
3. [ ] `KinectDriver.props` has `<KinectDriverStack Condition=...>upstream</KinectDriverStack>`
   (the default) and `KinectOpenMotor` is not overridden.
4. [ ] Open `Magic-Sand-Ecosystem.sln`, select **Release | x64**, Build →
   Rebuild Solution. The Output window shows
   `Kinect driver stack: upstream, OFXKINECT_OPEN_MOTOR=0` and the build succeeds.
   If the build fails, copy the first error into the notes column.

### A3. Run

Run with F5 (Release) and go through section C. Watch the Visual Studio
Output window / console for lines from `libfreenect`, `ofxKinect` and
`ofxKinect.driver`.

---

## B. Linux

### B1. Prerequisites and udev

1. [ ] `sudo apt install git cmake build-essential pkg-config libusb-1.0-0-dev`
2. [ ] In the project folder:
   `tools/drivers/build_libfreenect_linux.sh --install-udev`
   completes without error. Copy the final lines:
   - libfreenect version: ____________
   - libusb version: ____________
   (`--install-udev` copies libfreenect's `platform/linux/udev/51-kinect.rules`
   to `/etc/udev/rules.d/` and reloads udev.)
3. [ ] Unplug and replug the Kinect (USB and power).
4. [ ] `lsusb | grep 045e` lists the camera (`02ae` for model 1414,
   `02bf` for model 1473/K4W camera) and `ls -l /dev/bus/usb/<bus>/<dev>` for it
   shows `crw-rw-rw-`.

### B2. Build

1. [ ] `make Release` succeeds.
   Known issue, not caused by the driver stack: GCC with `-std=c++20` rejects
   the project's own C++17 shim in `src/KinectProjector/KinectProjector.h` and
   `KinectProjectorCalibration.h` (libstdc++ still declares
   `std::binary_function`), and current ofxCv `Tracker.h:128` uses
   `Tracker<T>()` as a constructor name, which GCC 11+ rejects in C++20. See
   `docs/DEVLOG.md` (2026-11-03) for the exact errors and the workaround used
   in the build check; record here whether you hit them.
2. [ ] `make RunRelease` (or `bin/<name>`) starts the app.

Then go through section C.

---

## C. Checks (both platforms)

Start a stopwatch when the app window appears.

1. **Depth within 5 s.** [ ] The Status panel shows "Kinect running" (green)
   and, with Advanced → "Display kinect depth view" ticked, a live depth image
   appears within 5 s of launch.
   Time observed: ______ s
2. **Sub-device mode.** [ ] The console shows
   `ofxKinect.driver: init(): sub-devices CAMERA only (no motor, LED or firmware)`.
3. **Frame rate over 60 s.** The GUI shows the application frame rate
   ("App FPS" in the Magic Sand panel); the Kinect's own frame rate is not
   displayed in the current GUI. Record App FPS at 0, 30 and 60 s after depth
   appears: ______ / ______ / ______
   Note any period where the depth image freezes: ____________
4. **libfreenect errors.** Copy every console line containing `libfreenect`,
   `freenect`, `iso_callback`, `Failed to open`, `could not open` or `LIBUSB_ERROR`
   during the first 60 s (or "none"): ____________
   (A single "Failed to set the LED of K4W or 1473 device" line is expected if
   the audio sub-device has no libusbK driver; record it anyway.)
5. **RUN! loads the saved calibration.** Click **RUN!** without recalibrating.
   [ ] The Status panel changes to "Application state: Running", with
   "ROI defined", "Baseplane found" and "Projector/Kinect calibrated" in green,
   and the projection lines up with the sand as on the old build.
   (These read `settings/calibration.xml` and `settings/kinectProjectorSettings.xml`
   from `bin/data/`; the files must be the ones from the working installation.)
6. **Elevation matches the old build.** With the sand flat and untouched,
   read "Raw elevation at ROI center" in the Vegetation panel three times, a
   few seconds apart: ______ / ______ / ______ mm.
   [ ] Mean within 1 mm of the old build's value (about +9 mm; exact old value
   if measured: ______ mm).

A platform **passes** when checks 1, 2, 5 and 6 pass and check 3 shows no
depth freeze. Checks 3 and 4 are recorded in full either way.

### Optional comparison: MOTOR | CAMERA

Only after the run above. Windows: build with `/p:KinectOpenMotor=true` (or set
`<KinectOpenMotor>true</KinectOpenMotor>` in `KinectDriver.props`). Linux:
`make Release PROJECT_DEFINES=OFXKINECT_OPEN_MOTOR=1` (touch
`addons/ofxKinect/src/ofxKinect.cpp` first so it recompiles). Repeat checks 1-4.
On a model 1473, libfreenect 0.7.5 will then also open the audio sub-device
and upload the audio firmware that stock ofxKinect embeds
(`ofxKinectExtras.cpp`), so motor and LED control may work. Record whether the
device opens, any "upload_firmware" error, and whether depth still streams.

### If the new stack fails (time box reached)

Windows:
1. `powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1 -RestoreFallback`
2. Set `<KinectDriverStack>fallback</KinectDriverStack>` in `KinectDriver.props`.
3. Rebuild (Release) and confirm depth streams as before.
If the fallback does not stream after the driver change, use Zadig to put
each sub-device back on the driver recorded in A0 (Zadig offers WinUSB,
libusb-win32 and libusbK), replug, and retry. Record whether this was needed.

Linux: the previous ofxKinect sources are in
`addons/ofxKinect/_libfreenect_backups/<timestamp>/libfreenect`; move that
folder back to `addons/ofxKinect/libs/libfreenect` and rebuild.

---

## Results

| Platform | OS version | libfreenect version | libusb version | Driver (libusbK version from Zadig; "udev rule" on Linux) | Pass/Fail | Notes (depth time, FPS 0/30/60 s, errors, calibration, elevation) |
|----------|------------|---------------------|----------------|------------------------------------------------------------|-----------|-------------------------------------------------------------------|
| Windows  | Windows ____ (fill in from `winver`) | 0.7.5 (`09a1f098`) | 1.0.30 (vcpkg 2026.10.16, toolset v143) | libusbK ____ (already installed before the test; camera and audio; model 1473 has no motor device) | Pass for the driver stack; check 5 failed | Tested 2026-11-07, Release x64. Checks 1-4 and 6 passed (depth streamed; camera-only log line present; frame rate and errors as expected; elevation matched the old build within 1 mm). Check 5 failed: RUN! did not load the saved calibration; cause not investigated and not attributed to the driver stack. User's decision: recalibrate at the start of each session. Exact timings, FPS and elevation readings: ____ |
| Linux    |            |                     |                |                                                            |           |                                                                   |
