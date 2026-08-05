# Upstream Pull Requests — draft descriptions

Five fixes in this fork address defects in `thomwolf/Magic-Sand` v1.5.4.1
rather than defects introduced by the openFrameworks 0.12.1 port. Each should
go upstream as a separate PR against `master`.

Upstream has had no release since October 2017 and carries ~37 open issues, so
these may not be merged. An open PR is a public, timestamped, citable artifact as well as evidence for the maintenance and accessibility claims, and a contribution to the commons irrespective of merge
status. To be cited in the thesis discussion.

**Before opening:** rebase each fix onto upstream `master` (pre-0.12.1) so the
diff contains only the fix, not the port. A PR that also migrates the GUI will
not be read.

---

## PR 1 — Fix producer/consumer buffer in `KinectGrabber`

**Priority: high.** Data corruption.

`KinectGrabber` calls `std::move` on the buffer passed to `filtered.send()`.
The consumer may still hold a reference to that buffer, so the move leaves the
consumer reading from a moved-from object. Symptoms are intermittent and
frame-dependent: torn or empty depth frames under load, hard to reproduce
deliberately, easy to misattribute to the sensor.

Removing the `std::move` costs one copy per frame and eliminates the race.

**Files:** `src/KinectProjector/KinectGrabber.cpp`

---

## PR 2 — Normalise chessboard corner ordering in `CalibrateNextPoint()`

**Priority: high.** Calibration.

Detected chessboard corners are paired without first normalising their order.
OpenCV's corner ordering depends on board orientation relative to the camera,
so a run can complete successfully and report success while producing an
incorrect kinect→projector transform.

This is the most valuable of the five: calibration difficulty is the single
most common complaint in the upstream issue tracker, and a failure mode that
_reports success_ is worse than one that errors out, because users conclude
their hardware placement is at fault and re-run indefinitely.

**Files:** `src/KinectProjector/KinectProjector.cpp`

---

## PR 3 — Fix `savePointPair()` filename construction

**Priority: medium.** Affects debugging and calibration persistence.

Filename construction bug causes calibration point pairs to be written to an
unintended path (or overwritten). Straightforward fix.

**Files:** `src/KinectProjector/KinectProjector.cpp`

---

## PR 4 — Add buffer/ROI snapshot guards in filtering and gradient update

**Priority: medium.** Out-of-bounds access on ROI change.

`filter()`, `applySpaceFilter()` and `updateGradientField()` read buffer
dimensions and the kinect ROI without snapshotting them. If the ROI changes
mid-frame — during manual ROI definition, or on automatic ROI detection —
these functions can index past the end of the buffer.

Fix takes a local snapshot of the ROI and dimensions at entry and uses those
consistently for the rest of the call.

**Files:** `src/KinectProjector/KinectProjector.cpp`

---

## PR 5 — Reallocate buffers after `kinect.open()`

**Priority: medium.** Stale dimensions on device open.

Buffers are sized before `kinect.open()` returns, so if the device reports
different dimensions than assumed, subsequent frames are written into
incorrectly sized buffers.

**Files:** `src/KinectProjector/KinectGrabber.cpp`

---

## Suggested PR template

```markdown
### Problem

<what goes wrong, and what the user observes — symptoms first>

### Cause

<the specific mechanism, with the line reference>

### Fix

<what this PR changes, in one or two sentences>

### Testing

Reproduced and verified on: Kinect model 1473, <OS>, <toolchain>.
<how you confirmed the fix>

### Notes

Found while modernizing Magic Sand for openFrameworks 0.12.1 as part of a
graduate research project. This PR contains only the fix, rebased onto
`master`; it does not include the port.
```

---

## After opening

Record the PR URLs and dates. They belong in:

- `CHANGELOG.md` — link each fix to its upstream PR
- Thesis §4.2 — as evidence the fixes are upstream defects, not port artifacts
- Thesis §9.4 — as the concrete instance of the maintenance argument
