# Linux Setup — openFrameworks 0.12.1 + Shifting Sands: Ecosystem

Reference platform: **Linux**.

---

## 1. Install openFrameworks 0.12.1

Take the **release tarball**, not a git clone. The release ships prebuilt
dependencies; a git clone requires an extra `download_libs.sh` step and pulls
whatever `master` currently is, which is not what your project pins to.

Download the Linux 64-bit gcc release for 0.12.1 from
`openframeworks.cc/download/`, then:

```bash
cd ~
tar -xf of_v0.12.1_linux64gcc6_release.tar.gz
mv of_v0.12.1_linux64gcc6_release openFrameworks
export OF_ROOT=~/openFrameworks          # add to ~/.bashrc
```

Install system dependencies and compile the library:

```bash
cd $OF_ROOT/scripts/linux/ubuntu          # or debian/fedora/arch as appropriate
sudo ./install_dependencies.sh

cd $OF_ROOT/scripts/linux
./compileOF.sh -j$(nproc)
```

`compileOF.sh` takes 10–30 minutes. You do **not** need `compilePG.sh` (the
Project Generator) — your project files already exist.

Verify before going further:

```bash
cd $OF_ROOT/examples/graphics/polygonExample
make -j$(nproc) && make run
```

If a window with polygons appears, openFrameworks is working. If this fails,
fix it here — debugging it later inside your own project is much harder.

**If `install_dependencies.sh` errors on your distro version:** the script gates
on release number and can lag very new releases. Check which package it failed
on and install it manually; that is usually the whole fix.

---

## 2. Install the addons

Five addons. Three ship with openFrameworks and need no action:

- `ofxOpenCv` ✓ bundled
- `ofxKinect` ✓ bundled
- `ofxXmlSettings` ✓ bundled

Two are community addons:

```bash
cd $OF_ROOT/addons
git clone https://github.com/kylemcdonald/ofxCv.git
git clone https://github.com/jvcleave/ofxImGui.git
```

### Pin the versions — this matters

`ofxCv` and `ofxImGui` both track their own `master`. A clone today will not
match what you built against, and your ofxImGui patches are written against a
specific version. On the machine where the build currently works:

```bash
cd $OF_ROOT/addons/ofxCv && git rev-parse HEAD
cd $OF_ROOT/addons/ofxImGui && git rev-parse HEAD
```

Record both in `PATCHES.md`. On any new machine:

```bash
cd $OF_ROOT/addons/ofxImGui && git checkout <recorded-hash>
```

Unpinned addon versions are the most likely reason a future installation trial
fails, and it will present as a compiler error deep in ImGui rather than as a
version mismatch.

### Apply the cross-platform patches

```bash
cd $OF_ROOT/apps/myApps/Shifting-Sands-Ecosystem
./tools/apply_addon_patches.sh $OF_ROOT --cross-platform-only
```

That applies only the two ofxImGui fixes (the `ImDrawIdx` typedef and the
missing `ofUtils.h` include), leaving stock `ofxKinect` in place.

---

## 3. Clone the repo in the right place

Same constraint as before: `config.make` sets `OF_ROOT = ../../..`, so the
project must sit exactly three levels below the openFrameworks root.

```bash
cd $OF_ROOT/apps/myApps
git clone https://github.com/pixelvistas/Shifting-Sands-Ecosystem.git
cd Shifting-Sands-Ecosystem
code .
```

---

## 4. Kinect v1 permissions — do this before you debug anything

Without udev rules, libfreenect cannot open the device without root, and the
failure looks like a missing or broken sensor rather than a permissions problem.

```bash
sudo tee /etc/udev/rules.d/51-kinect.rules > /dev/null <<'RULES'
# Kinect for Xbox 360 / Kinect for Windows (model 1473)
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ae", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ad", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02b0", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02c2", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02be", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02bf", MODE="0666"
RULES

sudo udevadm control --reload-rules && sudo udevadm trigger
```

Unplug and replug the Kinect. Confirm it enumerates:

```bash
lsusb | grep -i microsoft     # expect Xbox NUI Camera / Audio / Motor
```

The `02c2`, `02be`, `02bf` IDs are the model 1473 variants specifically.

Also blacklist the kernel's own Kinect driver, which fights libfreenect:

```bash
echo "blacklist gspca_kinect" | sudo tee /etc/modprobe.d/blacklist-kinect.conf
sudo rmmod gspca_kinect 2>/dev/null || true
```

---

## 5. Build

```bash
make Debug -j$(nproc)
./bin/Shifting-Sands-Ecosystem_debug
```

Or in VS Code: `Ctrl+Shift+B`, and `F5` to debug under gdb.

Note the `Makefile` derives the binary name from the directory, so renaming the
folder renames the binary. Keep `launch.json` in sync if you rename.

---

## Two risks to test early

Both are cheap to check and expensive to discover in September.

### Risk 1 — the Kinect may not open on Linux

Your `PATCHES.md` says libfreenect "generally handles Kinect v1 far more
gracefully" on Linux — but that is an assumption you have not yet tested. The
1473 is the awkward model; it is what forced the 0.9.3 transplant on Windows.

**Test:** run the app, open the depth view, confirm frames arrive. If stock
`ofxKinect` cannot open the 1473 on Linux either, you will need the same kind of
workaround, and better to know now.

### Risk 2 — multi-display on Linux

Upstream Magic Sand documents a known Linux limitation: **multiple displays do
not work properly**, and the only reliable configuration was projector-only,
which puts the operator GUI on the sand.

This is a structural problem for a two-window application — you need an operator
window _and_ a projector window. It would be awkward for demos and worse for
calibration.

The optimistic reading: that limitation was recorded against openFrameworks
0.9.3 in 2017, and GLFW multi-window handling on Linux has improved a great deal
since. It may simply work on 0.12.1.

**Test:** connect the projector as a second display, launch, and confirm the GUI
lands on the laptop screen and the projection window on the projector.

If it works, say so explicitly in the thesis and in `CHANGELOG.md` — resolving a
documented upstream limitation is a concrete contribution. If it does not, this
becomes the highest-priority fix in the project, ahead of the module refactor,
because everything downstream depends on being able to run a session.

---

## Order of operations

```
1. openFrameworks installed, polygonExample runs
2. Addons cloned, versions recorded, ofxImGui patches applied
3. Repo cloned into apps/myApps
4. udev rules, Kinect enumerates
5. make Debug succeeds
6. App runs, Kinect opens, depth view shows frames     ← Risk 1 resolved
7. Two-display test                                     ← Risk 2 resolved
8. Full calibration completes, colormap projects on sand
--- baseline confirmed, only now start on TASKS.md ---
9. Group A (compliance), Group B (housekeeping)
10. Group D (module refactor)
```

Do not start the refactor before step 8. A broken baseline plus a large refactor
is very hard to bisect.
