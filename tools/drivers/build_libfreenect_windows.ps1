<#
.SYNOPSIS
    Builds upstream libfreenect (OpenKinect, pinned tag) against a current
    libusb from vcpkg and installs it into openFrameworks' ofxKinect addon.

.DESCRIPTION
    Replaces the libfreenect transplant from openFrameworks 0.9.3 (see
    PATCHES.md) with a reproducible build:

      libfreenect  v0.7.5   (commit 09a1f098040d00e6070c18174904547ec31d2774)
      libusb       1.0.30   (vcpkg port libusb 1.0.30 at vcpkg tag 2026.10.16,
                             triplet x64-windows-static-md-<toolset>: static
                             library, dynamic CRT /MD, matching openFrameworks)
    Both libraries are compiled with the MSVC toolset the project uses
    (<PlatformToolset> in Magic-Sand-Ecosystem.vcxproj, v143 at present;
    override with -PlatformToolset).

    Installed layout (consumed by KinectDriver.props when
    KinectDriverStack = upstream):

      <OF>\addons\ofxKinect\libs\libfreenect\
          include\              libfreenect.h, libfreenect_registration.h,
                                libfreenect_audio.h, freenect_internal.h,
                                usb_libusb10.h  (ofxKinect.cpp reads
                                freenect_device internals, so the internal
                                headers must come from the same build)
          include\libusb-1.0\   libusb.h
          lib\vs\x64\Release\   freenect.lib, libusb-1.0.lib
          lib\vs\x64\Debug\     freenect.lib, libusb-1.0.lib
          APACHE20, GPL2, CONTRIB, LIBUSB-COPYING.txt, VERSIONS.txt

    The previous libs\libfreenect folder is moved to
    <OF>\addons\ofxKinect\_libfreenect_backups\<timestamp>\ (never deleted).
    The patched ofxKinect.cpp from _addon_patches is copied into the addon
    unless -SkipAddonPatch is given (the stock file is backed up first).

    No firmware is downloaded, built or installed: BUILD_REDIST_PACKAGE is
    ON and the app opens the camera sub-device only.

    The Kinect itself still needs the libusbK driver installed with Zadig
    (one entry per sub-device); see docs\DRIVER_TEST.md.

.PARAMETER OfRoot
    openFrameworks 0.12.1 root. Default: three levels above the project.
.PARAMETER WorkDir
    Scratch directory for clones and builds.
    Default: %LOCALAPPDATA%\ShiftingSands\libfreenect-v0.7.5
.PARAMETER RestoreFallback
    Do not build. Restore the openFrameworks 0.9.3 libfreenect transplant
    from _addon_patches\fallback-libfreenect-0.9.3 instead. Afterwards set
    KinectDriverStack to "fallback" in KinectDriver.props.
.PARAMETER SkipAddonPatch
    Do not copy _addon_patches\ofxKinect\src\ofxKinect.cpp into the addon.
.PARAMETER DryRun
    Build and report, but do not modify the openFrameworks tree.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1
.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\drivers\build_libfreenect_windows.ps1 -RestoreFallback
#>
[CmdletBinding()]
param(
    [string]$OfRoot = "",
    [string]$WorkDir = "",
    [switch]$RestoreFallback,
    [switch]$SkipAddonPatch,
    [string]$PlatformToolset = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# ---- pinned versions ------------------------------------------------------
$LibfreenectRepo   = "https://github.com/OpenKinect/libfreenect.git"
$LibfreenectTag    = "v0.7.5"
$LibfreenectCommit = "09a1f098040d00e6070c18174904547ec31d2774"
$VcpkgRepo         = "https://github.com/microsoft/vcpkg.git"
# vcpkg pin. The libusb port runs vcpkg_fixup_pkgconfig, which downloads
# MSYS2 packages (pkgconf, msys2-runtime) at versions fixed by the vcpkg tag.
# MSYS2 deletes old package versions from all mirrors, so an old tag stops
# building with "Download failed ... status code 404" (this happened to tag
# 2025.01.13 / msys2-runtime-3.5.4-2 in November 2026). If that error appears,
# move the pin to a newer vcpkg release tag and update the commit and the
# expected libusb version from that tag's ports/libusb/vcpkg.json. vcpkg tags
# are annotated: the commit is the "^{}" line of
#   git ls-remote https://github.com/microsoft/vcpkg.git "refs/tags/<tag>*"
# not the tag object's own hash.
$VcpkgTag          = "2026.10.16"
$VcpkgCommit       = "9eec002df1cd109f8f104cd966a763daf9751489"
$LibusbExpected    = "1.0.30"          # libusb port version at $VcpkgTag
# The vcpkg triplet is x64-windows-static-md plus the project's MSVC toolset;
# it is built below, once the project folder is known.
$CMakeMinVersion   = [version]"3.15"   # CMAKE_MSVC_RUNTIME_LIBRARY / CMP0091

# ---- paths ------------------------------------------------------------------
$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

# ---- MSVC toolset: must match the project's -----------------------------------
# libusb's own Release build uses whole-program optimisation (/GL). Objects
# compiled that way can only be linked by the same compiler version, so both
# libraries are built with the toolset the project uses (<PlatformToolset> in
# Magic-Sand-Ecosystem.vcxproj), not with whatever the newest installed Visual
# Studio defaults to. Otherwise the project's Release link fails with
# "C1900: Il mismatch between 'P1' version ... and 'P2' version ...".
if (-not $PlatformToolset) {
    $vcxproj = Join-Path $ProjectDir "Magic-Sand-Ecosystem.vcxproj"
    $m = Select-String -Path $vcxproj -Pattern '<PlatformToolset>(v\d+)</PlatformToolset>' | Select-Object -First 1
    if ($m) { $PlatformToolset = $m.Matches[0].Groups[1].Value } else { $PlatformToolset = "v143" }
}
$VcpkgTriplet = "x64-windows-static-md-$PlatformToolset"
if (-not $OfRoot)  { $OfRoot  = Join-Path $ProjectDir "..\..\.." }
if (-not $WorkDir) { $WorkDir = Join-Path $env:LOCALAPPDATA "ShiftingSands\libfreenect-$LibfreenectTag" }

function Fail([string]$msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments)
    Write-Host "> $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { Fail "$Exe exited with code $LASTEXITCODE" }
}

function Require-Tool([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        Fail "'$name' was not found on PATH. Install it (Git for Windows, CMake, Visual Studio with 'Desktop development with C++') and retry from a 'Developer PowerShell for VS'."
    }
}

$AddonDir = Join-Path $OfRoot "addons\ofxKinect"
if (-not $DryRun) {
    if (-not (Test-Path (Join-Path $AddonDir "src\ofxKinect.h"))) {
        Fail "ofxKinect not found at $AddonDir (use -OfRoot)."
    }
    $AddonDir = (Resolve-Path $AddonDir).Path
}
$Dest = Join-Path $AddonDir "libs\libfreenect"

function Backup-CurrentLibfreenect {
    if (Test-Path $Dest) {
        $stamp  = Get-Date -Format "yyyyMMdd-HHmmss"
        $backup = Join-Path $AddonDir "_libfreenect_backups\$stamp"
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Move-Item -Path $Dest -Destination (Join-Path $backup "libfreenect")
        Write-Host "Previous libs\libfreenect moved to $backup\libfreenect"
    }
}

# ---- fallback restore ---------------------------------------------------------
if ($RestoreFallback) {
    $fallback = Join-Path $ProjectDir "_addon_patches\fallback-libfreenect-0.9.3\libfreenect"
    if (-not (Test-Path (Join-Path $fallback "src\core.c"))) { Fail "Fallback copy not found at $fallback" }
    if ($DryRun) { Write-Host "Dry run: would restore $fallback to $Dest"; exit 0 }
    Backup-CurrentLibfreenect
    Copy-Item -Recurse -Path $fallback -Destination $Dest
    Write-Host ""
    Write-Host "Restored the openFrameworks 0.9.3 libfreenect transplant to $Dest."
    Write-Host "Now set <KinectDriverStack>fallback</KinectDriverStack> in KinectDriver.props"
    Write-Host "(this also selects MOTOR | CAMERA, the configuration recorded as working)."
    Write-Host "The fallback links ofxKinect\libs\libusb\lib\vs\x64\libusb.lib from the stock"
    Write-Host "openFrameworks download, which this script never modifies."
    exit 0
}

# ---- prerequisites ------------------------------------------------------------
Require-Tool git
Require-Tool cmake
$cmakeFirstLine = (& cmake --version) | Select-Object -First 1
if ($cmakeFirstLine -notmatch '(\d+\.\d+(\.\d+)?)') { Fail "Could not read the CMake version from '$cmakeFirstLine'" }
$cmakeVersion = [version]$Matches[1]
if ($cmakeVersion -lt $CMakeMinVersion) { Fail "CMake $cmakeVersion is older than $CMakeMinVersion" }

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$WorkDir = (Resolve-Path $WorkDir).Path

function Get-PinnedClone([string]$repo, [string]$tag, [string]$commit, [string]$dir) {
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        Invoke-Native git @("-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1", "--branch", $tag, $repo, $dir)
    }
    $actual = (& git -C $dir rev-parse HEAD).Trim()
    if ($actual -ne $commit) { Fail "$dir is at $actual, expected $commit ($tag). Delete the folder and retry." }
}

# ---- libusb via vcpkg (pinned) --------------------------------------------------
$VcpkgDir = Join-Path $WorkDir "vcpkg"
Get-PinnedClone $VcpkgRepo $VcpkgTag $VcpkgCommit $VcpkgDir
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"
if (-not (Test-Path $VcpkgExe)) {
    Invoke-Native (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") @("-disableMetrics")
}
# Overlay triplet: the standard x64-windows-static-md settings plus the toolset.
$TripletDir = Join-Path $WorkDir "triplets"
New-Item -ItemType Directory -Force -Path $TripletDir | Out-Null
@(
    "set(VCPKG_TARGET_ARCHITECTURE x64)",
    "set(VCPKG_CRT_LINKAGE dynamic)",
    "set(VCPKG_LIBRARY_LINKAGE static)",
    "set(VCPKG_PLATFORM_TOOLSET $PlatformToolset)"
) | Set-Content -Encoding ASCII (Join-Path $TripletDir "$VcpkgTriplet.cmake")
Invoke-Native $VcpkgExe @("install", "libusb:$VcpkgTriplet", "--overlay-triplets=$TripletDir", "--classic", "--vcpkg-root", $VcpkgDir)

$UsbRoot    = Join-Path $VcpkgDir "installed\$VcpkgTriplet"
$UsbInclude = Join-Path $UsbRoot "include\libusb-1.0"
$UsbLibRel  = Join-Path $UsbRoot "lib\libusb-1.0.lib"
$UsbLibDbg  = Join-Path $UsbRoot "debug\lib\libusb-1.0.lib"
foreach ($p in @((Join-Path $UsbInclude "libusb.h"), $UsbLibRel, $UsbLibDbg)) {
    if (-not (Test-Path $p)) { Fail "vcpkg did not produce $p" }
}
$listing = (& $VcpkgExe list libusb --classic --vcpkg-root $VcpkgDir | Select-String "libusb:$VcpkgTriplet" | Select-Object -First 1).ToString()
$LibusbVersion = ($listing -split '\s+')[1]
if (-not $LibusbVersion.StartsWith($LibusbExpected)) {
    Fail "vcpkg installed libusb $LibusbVersion, expected $LibusbExpected (vcpkg tag $VcpkgTag)"
}

# ---- libfreenect (pinned), C library only, static ------------------------------
$SrcDir   = Join-Path $WorkDir "libfreenect"
$BuildDir = Join-Path $WorkDir "build"
Get-PinnedClone $LibfreenectRepo $LibfreenectTag $LibfreenectCommit $SrcDir
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }

# libfreenect's CMakeLists predates CMake 3.15 and overwrites the per-config
# flag variables, which drops /MD. Policy CMP0091 + CMAKE_MSVC_RUNTIME_LIBRARY
# select the dynamic CRT through CMake's own mechanism instead, so the
# library matches openFrameworks (/MD Release, /MDd Debug) without editing
# any upstream or generated file.
Invoke-Native cmake @(
    "-S", $SrcDir, "-B", $BuildDir, "-A", "x64", "-T", $PlatformToolset,
    "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>DLL",
    "-DLIBUSB_1_INCLUDE_DIR=$UsbInclude",
    "-DLIBUSB_1_LIBRARY=$UsbLibRel",
    "-DBUILD_EXAMPLES=OFF", "-DBUILD_FAKENECT=OFF", "-DBUILD_C_SYNC=OFF",
    "-DBUILD_CPP=OFF", "-DBUILD_CV=OFF", "-DBUILD_AS3_SERVER=OFF",
    "-DBUILD_PYTHON=OFF", "-DBUILD_PYTHON2=OFF", "-DBUILD_PYTHON3=OFF",
    "-DBUILD_OPENNI2_DRIVER=OFF", "-DBUILD_REDIST_PACKAGE=ON"
)
# Only the static target: the shared target writes an import library with
# the same name (freenect.lib) into the same folder.
foreach ($cfg in @("Release", "Debug")) {
    Invoke-Native cmake @("--build", $BuildDir, "--config", $cfg, "--target", "freenectstatic", "--parallel")
}

function Find-BuiltLib([string]$cfg) {
    $hit = Get-ChildItem -Path $BuildDir -Recurse -Filter "freenect.lib" |
           Where-Object { $_.DirectoryName -match "\\$cfg$" } | Select-Object -First 1
    if (-not $hit) { Fail "freenect.lib ($cfg) not found under $BuildDir" }
    return $hit.FullName
}
$FreenectRel = Find-BuiltLib "Release"
$FreenectDbg = Find-BuiltLib "Debug"

# ---- install into ofxKinect -----------------------------------------------------
if (-not $DryRun) {
    Backup-CurrentLibfreenect
    $inc = Join-Path $Dest "include"
    $rel = Join-Path $Dest "lib\vs\x64\Release"
    $dbg = Join-Path $Dest "lib\vs\x64\Debug"
    foreach ($d in @($inc, (Join-Path $inc "libusb-1.0"), $rel, $dbg)) {
        New-Item -ItemType Directory -Force -Path $d | Out-Null
    }
    foreach ($h in @("include\libfreenect.h", "include\libfreenect_registration.h",
                     "include\libfreenect_audio.h", "src\freenect_internal.h", "src\usb_libusb10.h")) {
        Copy-Item (Join-Path $SrcDir $h) $inc
    }
    Copy-Item (Join-Path $UsbInclude "libusb.h") (Join-Path $inc "libusb-1.0")
    Copy-Item $FreenectRel $rel
    Copy-Item $UsbLibRel   $rel
    Copy-Item $FreenectDbg $dbg
    Copy-Item $UsbLibDbg   $dbg
    foreach ($f in @("APACHE20", "GPL2", "CONTRIB")) { Copy-Item (Join-Path $SrcDir $f) $Dest }
    $usbCopyright = Join-Path $UsbRoot "share\libusb\copyright"
    if (Test-Path $usbCopyright) { Copy-Item $usbCopyright (Join-Path $Dest "LIBUSB-COPYING.txt") }
    @(
        "Installed by tools\drivers\build_libfreenect_windows.ps1 on $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "libfreenect $LibfreenectTag ($LibfreenectCommit), static, C library only",
        "libusb-1.0  $LibusbVersion (vcpkg $VcpkgTag, $VcpkgTriplet)",
        "Both built with MSVC toolset $PlatformToolset (from the project's PlatformToolset)"
    ) | Set-Content -Encoding ASCII (Join-Path $Dest "VERSIONS.txt")

    if (-not $SkipAddonPatch) {
        $patched = Join-Path $ProjectDir "_addon_patches\ofxKinect\src\ofxKinect.cpp"
        $target  = Join-Path $AddonDir "src\ofxKinect.cpp"
        if ((Get-FileHash $patched).Hash -ne (Get-FileHash $target).Hash) {
            Copy-Item $target "$target.stock-backup" -Force
            Copy-Item $patched $target -Force
            Write-Host "Copied patched ofxKinect.cpp into the addon (previous file kept as ofxKinect.cpp.stock-backup)."
        }
    }
}

# ---- report ---------------------------------------------------------------------------
$cm = Get-Content (Join-Path $SrcDir "CMakeLists.txt")
$lfVersion = (@("MAJOR", "MINOR", "PATCH") | ForEach-Object {
    $k = $_; (($cm | Select-String "set \(PROJECT_VER_$k ([0-9]+)\)").Matches[0].Groups[1].Value)
}) -join "."
Write-Host ""
Write-Host "libfreenect version: $lfVersion (tag $LibfreenectTag, commit $LibfreenectCommit)"
Write-Host "libusb version:      $LibusbVersion (vcpkg $VcpkgTag, triplet $VcpkgTriplet)"
Write-Host "MSVC toolset:        $PlatformToolset"
if ($DryRun) {
    Write-Host "Dry run: built $FreenectRel; openFrameworks tree not modified."
} else {
    Write-Host "Installed into:      $Dest"
    Write-Host "Build the app with <KinectDriverStack>upstream</KinectDriverStack> in KinectDriver.props."
}
