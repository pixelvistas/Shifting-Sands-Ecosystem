#!/usr/bin/env bash
# build_libfreenect_linux.sh
#
# Builds upstream libfreenect (OpenKinect, pinned tag below) against the
# distribution's libusb-1.0 and installs it into the layout that
# openFrameworks' ofxKinect addon consumes on Linux:
#
#   $OF_ROOT/addons/ofxKinect/libs/libfreenect/
#       include/   libfreenect.h, libfreenect_registration.h, libfreenect_audio.h
#                  freenect_internal.h, usb_libusb10.h   (ofxKinect.cpp reads
#                  freenect_device internals, so these must match the library)
#       lib/<abi>/libfreenect.a                         (<abi> = linux64 | linuxaarch64)
#       APACHE20, GPL2, CONTRIB, VERSIONS.txt
#
# The oF Makefile links libs/*/lib/<abi>/*.a automatically and pulls in
# libusb-1.0 through ofxKinect's addon_config.mk (pkg-config), so no
# project file needs editing. The previous libs/libfreenect folder (stock
# oF sources, which the Makefile would otherwise compile) is moved to
# addons/ofxKinect/_libfreenect_backups/<timestamp>/, outside libs/, so it
# is neither compiled nor lost.
#
# Usage:
#   tools/drivers/build_libfreenect_linux.sh [--of-root PATH] [--work-dir PATH]
#                                            [--install-udev] [--skip-addon-patch] [--dry-run]
#
#   --of-root      openFrameworks 0.12.1 root (default: ../../.. from the project)
#   --work-dir     scratch directory for the clone and build
#                  (default: ~/.cache/shifting-sands/libfreenect-<tag>)
#   --install-udev copy libfreenect's platform/linux/udev/51-kinect.rules to
#                  /etc/udev/rules.d/ (uses sudo) and reload udev
#   --skip-addon-patch  do not copy _addon_patches/ofxKinect/src/ofxKinect.cpp
#                  (camera-only sub-device setting) into the addon; by default
#                  it is copied and the stock file kept as ofxKinect.cpp.stock-backup
#   --dry-run      build and report, but do not touch the oF tree
#
# Requirements (Debian/Ubuntu): git cmake build-essential pkg-config libusb-1.0-0-dev

set -euo pipefail

# ---- pinned versions ------------------------------------------------------
LIBFREENECT_REPO="https://github.com/OpenKinect/libfreenect.git"
LIBFREENECT_TAG="v0.7.5"
LIBFREENECT_COMMIT="09a1f098040d00e6070c18174904547ec31d2774"   # v0.7.5, 2024-01-06
LIBUSB_MIN_VERSION="1.0.22"   # first release with native libusbK isochronous support;
                              # on Linux the distro package is used (Ubuntu 24.04: 1.0.27)

# ---- arguments --------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
OF_ROOT="$PROJECT_DIR/../../.."
WORK_DIR="${HOME}/.cache/shifting-sands/libfreenect-${LIBFREENECT_TAG}"
INSTALL_UDEV=0
SKIP_ADDON_PATCH=0
DRY_RUN=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --of-root)      OF_ROOT="$2"; shift 2 ;;
        --work-dir)     WORK_DIR="$2"; shift 2 ;;
        --install-udev) INSTALL_UDEV=1; shift ;;
        --skip-addon-patch) SKIP_ADDON_PATCH=1; shift ;;
        --dry-run)      DRY_RUN=1; shift ;;
        -h|--help)      sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }

# ---- prerequisites ----------------------------------------------------------
for tool in git cmake make pkg-config cc; do
    command -v "$tool" >/dev/null || die "'$tool' not found. Install: sudo apt install git cmake build-essential pkg-config libusb-1.0-0-dev"
done
pkg-config --exists libusb-1.0 || die "libusb-1.0 development package not found (sudo apt install libusb-1.0-0-dev)"
LIBUSB_VERSION="$(pkg-config --modversion libusb-1.0)"
pkg-config --atleast-version="$LIBUSB_MIN_VERSION" libusb-1.0 \
    || die "libusb-1.0 $LIBUSB_VERSION is older than the required $LIBUSB_MIN_VERSION"

case "$(uname -m)" in
    x86_64)  OF_ABI="linux64" ;;
    aarch64) OF_ABI="linuxaarch64" ;;
    armv7l)  OF_ABI="linuxarmv7l" ;;
    *) die "unsupported architecture $(uname -m)" ;;
esac

ADDON_DIR="$OF_ROOT/addons/ofxKinect"
if [[ $DRY_RUN -eq 0 ]]; then
    [[ -f "$ADDON_DIR/src/ofxKinect.h" ]] || die "ofxKinect not found at $ADDON_DIR (use --of-root)"
    OF_ROOT="$(cd "$OF_ROOT" && pwd)"
    ADDON_DIR="$OF_ROOT/addons/ofxKinect"
fi

# ---- fetch (pinned tag, verified commit) ------------------------------------
SRC_DIR="$WORK_DIR/src"
BUILD_DIR="$WORK_DIR/build"
mkdir -p "$WORK_DIR"
if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --quiet --depth 1 --branch "$LIBFREENECT_TAG" "$LIBFREENECT_REPO" "$SRC_DIR"
fi
ACTUAL_COMMIT="$(git -C "$SRC_DIR" rev-parse HEAD)"
[[ "$ACTUAL_COMMIT" == "$LIBFREENECT_COMMIT" ]] \
    || die "libfreenect checkout is $ACTUAL_COMMIT, expected $LIBFREENECT_COMMIT for $LIBFREENECT_TAG (delete $SRC_DIR and retry)"

# ---- configure and build: C library only, static, no examples ----------------
rm -rf "$BUILD_DIR"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_FAKENECT=OFF \
    -DBUILD_C_SYNC=OFF \
    -DBUILD_CPP=OFF \
    -DBUILD_CV=OFF \
    -DBUILD_AS3_SERVER=OFF \
    -DBUILD_PYTHON=OFF -DBUILD_PYTHON2=OFF -DBUILD_PYTHON3=OFF \
    -DBUILD_OPENNI2_DRIVER=OFF \
    -DBUILD_REDIST_PACKAGE=ON
# Only the static target is built: ofxKinect links one archive into the app,
# and no shared libfreenect needs to be installed system-wide.
cmake --build "$BUILD_DIR" --target freenectstatic --parallel

STATIC_LIB="$BUILD_DIR/lib/libfreenect.a"
[[ -f "$STATIC_LIB" ]] || die "build finished but $STATIC_LIB is missing"

# ---- install into ofxKinect -------------------------------------------------
if [[ $DRY_RUN -eq 0 ]]; then
    DEST="$ADDON_DIR/libs/libfreenect"
    if [[ -e "$DEST" ]]; then
        BACKUP="$ADDON_DIR/_libfreenect_backups/$(date +%Y%m%d-%H%M%S)"
        mkdir -p "$BACKUP"
        mv "$DEST" "$BACKUP/libfreenect"
        echo "Previous libs/libfreenect moved to $BACKUP/libfreenect"
    fi
    mkdir -p "$DEST/include" "$DEST/lib/$OF_ABI"
    cp "$SRC_DIR/include/libfreenect.h" \
       "$SRC_DIR/include/libfreenect_registration.h" \
       "$SRC_DIR/include/libfreenect_audio.h" \
       "$SRC_DIR/src/freenect_internal.h" \
       "$SRC_DIR/src/usb_libusb10.h" \
       "$DEST/include/"
    cp "$STATIC_LIB" "$DEST/lib/$OF_ABI/libfreenect.a"
    cp "$SRC_DIR/APACHE20" "$SRC_DIR/GPL2" "$SRC_DIR/CONTRIB" "$DEST/"
    cat > "$DEST/VERSIONS.txt" <<EOF
Installed by tools/drivers/build_libfreenect_linux.sh on $(date -u +%Y-%m-%dT%H:%M:%SZ)
libfreenect $LIBFREENECT_TAG ($ACTUAL_COMMIT), static, C library only
libusb-1.0  $LIBUSB_VERSION (system package, linked via pkg-config)
EOF
    if [[ $SKIP_ADDON_PATCH -eq 0 ]]; then
        PATCHED="$PROJECT_DIR/_addon_patches/ofxKinect/src/ofxKinect.cpp"
        TARGET="$ADDON_DIR/src/ofxKinect.cpp"
        if ! cmp -s "$PATCHED" "$TARGET"; then
            cp "$TARGET" "$TARGET.stock-backup"
            cp "$PATCHED" "$TARGET"
            echo "Copied patched ofxKinect.cpp into the addon (previous file kept as ofxKinect.cpp.stock-backup)."
        fi
    fi
fi

# ---- udev rule (optional) ---------------------------------------------------
if [[ $INSTALL_UDEV -eq 1 ]]; then
    sudo cp "$SRC_DIR/platform/linux/udev/51-kinect.rules" /etc/udev/rules.d/51-kinect.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo "udev rule installed; unplug and replug the Kinect (USB and power)."
fi

# ---- report -----------------------------------------------------------------
LF_VERSION="$(sed -n 's/^set (PROJECT_VER_\(MAJOR\|MINOR\|PATCH\) \([0-9]*\)).*/\2/p' "$SRC_DIR/CMakeLists.txt" | paste -sd.)"
echo
echo "libfreenect version: $LF_VERSION (tag $LIBFREENECT_TAG, commit $ACTUAL_COMMIT)"
echo "libusb version:      $LIBUSB_VERSION"
if [[ $DRY_RUN -eq 0 ]]; then
    echo "Installed into:      $ADDON_DIR/libs/libfreenect (lib/$OF_ABI/libfreenect.a)"
else
    echo "Dry run: built $STATIC_LIB; oF tree not modified."
fi
