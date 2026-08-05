/***********************************************************************
SandboxModule.h - Base class for extensible sandbox modules
Copyright (c) 2026 Shelby Murchie (Shifting Sands - Ecosystem, York University)

Derived from the module structure latent in Magic Sand
Copyright (c) 2016-2017 Thomas Wolf and Rasmus R. Paulsen (people.compute.dtu.dk/rapa)

This file is part of Shifting Sands - Ecosystem.

Shifting Sands - Ecosystem is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ofMain.h"
#include "KinectProjector/KinectProjector.h"

/*
SandboxModule
-------------
Common interface for anything that reads the sand surface and draws to the
projector. The lifecycle mirrors the de facto interface already shared by
CBoidGameController and CMapGameController, so existing modes port with
minimal change.

What this adds beyond the upstream pattern:

  1. A formal extension point. Upstream, "how to add a mode" was "read
	 ofApp.cpp and copy what the games do." Here it is a class to inherit.

  2. A PERSISTENT STATE LAYER. Every published mode in the SARndbox lineage
	 computes display = f(H): the output is a pure function of the current
	 sand surface, so reshaping the sand erases the result. A module may
	 instead compute display = f(H, S), where S is owned by the base class,
	 accumulates over time, and is NOT recoverable from the height field.

	 S survives recalibration and ROI changes by resampling (see
	 setKinectROI). It is deliberately not cleared on those events: clearing
	 would silently destroy the accumulated history and would surface as a
	 module bug rather than an infrastructure one.

Coordinate convention
---------------------
State cells are indexed in KINECT coordinates, downsampled by cellSize, and
offset from the ROI origin. This matches the upstream guidance to store
simulation objects in kinect coordinates and convert on display. Use
kinectProjector->kinectCoordToProjCoord() at draw time.
*/

class SandboxModule
{
public:
	SandboxModule() = default;
	virtual ~SandboxModule() = default;

	SandboxModule(const SandboxModule &) = delete;
	SandboxModule &operator=(const SandboxModule &) = delete;

	// ---- Identity -------------------------------------------------------

	// Short stable name, used in the GUI, in state filenames, and in logs.
	virtual std::string getName() const = 0;

	// One line shown in the module list.
	virtual std::string getDescription() const { return ""; }

	// Key that activates this module from the main window, or 0 for none.
	virtual int getActivationKey() const { return 0; }

	// ---- Lifecycle ------------------------------------------------------

	// Called once by the host. Derived classes should call the base
	// implementation first, then allocate their own state via allocateState().
	virtual void setup(std::shared_ptr<KinectProjector> const &k);

	// Called every frame, after kinectProjector->update(). Only called while
	// the module is active.
	virtual void update() = 0;

	// Draw to the projector window. Only called while active, and only when
	// the application state is RUNNING and calibration is not in progress.
	virtual void drawProjectorWindow() = 0;

	// Optional debug/preview draw in the main (operator) window.
	virtual void drawMainWindow(float x, float y, float width, float height) {}

	// ---- GUI ------------------------------------------------------------

	// ofxImGui is immediate-mode: build widgets inside drawGui() rather than
	// constructing them in setupGui(). setupGui() remains for one-time state.
	virtual void setupGui() {}
	virtual void drawGui() {}

	// ---- Activation -----------------------------------------------------

	virtual void activate();
	virtual void deactivate();
	bool isActive() const { return active; }

	// Retained for compatibility with the upstream controllers, which use
	// isIdle() to arbitrate between modes.
	virtual bool isIdle() const { return !active; }

	// Return true if the key was consumed, false to let the host handle it.
	virtual bool keyPressed(int key) { return false; }

	// ---- Geometry sync --------------------------------------------------
	// Called by the host when calibration or ROI changes. Overriding these,
	// call the base implementation: setKinectROI() resamples the state layer.

	virtual void setProjectorRes(ofVec2f const &pr) { projRes = pr; }
	virtual void setKinectRes(ofVec2f const &kr) { kinectRes = kr; }
	virtual void setKinectROI(ofRectangle const &roi);
	virtual void setDebug(bool flag) { debugOn = flag; }

	ofRectangle getKinectROI() const { return kinectROI; }

	// ---- Persistent state: public queries -------------------------------

	int getStateLayerCount() const { return nLayers; }
	int getStateCellSize() const { return cellSize; }
	int getStateWidth() const { return stateW; }
	int getStateHeight() const { return stateH; }

	// Wall-clock seconds this module has been active, excluding pauses.
	float getElapsed() const { return elapsed; }

	// Save/load the full state layer set. Returns false and logs on failure.
	bool saveState(std::string const &path) const;
	bool loadState(std::string const &path);

protected:
	// ---- Persistent state: derived-class API ----------------------------

	// Allocate nLayers state grids. cellSize is the kinect-pixel downsample
	// factor: 1 = per-pixel (expensive), 4 = a good default, 10 matches the
	// gradient field resolution. Call from setup() after the base setup().
	void allocateState(int layers, int cell = 4, float initial = 0.0f);

	// Bounds-checked accessors in STATE cell coordinates.
	float getState(int layer, int sx, int sy) const;
	void setState(int layer, int sx, int sy, float v);
	void addState(int layer, int sx, int sy, float v);

	// Bounds-checked accessors in KINECT coordinates (converts internally).
	float getStateAtKinect(int layer, float kx, float ky) const;
	void setStateAtKinect(int layer, float kx, float ky, float v);
	void addStateAtKinect(int layer, float kx, float ky, float v);

	// Whole-layer operations.
	void clearState(int layer, float v = 0.0f);
	void clearAllState(float v = 0.0f);
	float getStateMin(int layer) const;
	float getStateMax(int layer) const;
	float getStateMean(int layer) const;

	// Direct access for shader upload or tight loops. Row-major, stateW wide.
	std::vector<float> &stateLayer(int layer) { return state[layer]; }
	std::vector<float> const &stateLayer(int layer) const { return state[layer]; }

	// Coordinate helpers.
	void stateToKinect(int sx, int sy, float &kx, float &ky) const;
	void kinectToState(float kx, float ky, int &sx, int &sy) const;
	bool inStateBounds(int sx, int sy) const;

	// ---- Hooks ----------------------------------------------------------

	// Called after the ROI changes and the state has been resampled. Override
	// to fix up anything that caches cell coordinates (agent positions, etc.).
	virtual void onStateResampled(ofRectangle const &oldROI,
								  ofRectangle const &newROI) {}

	// Called on activate() / deactivate(), after the flag is set.
	virtual void onActivate() {}
	virtual void onDeactivate() {}

	// ---- Convenience passthroughs to the projector ----------------------

	float elevationAt(float kx, float ky) const;
	ofVec2f gradientAt(float kx, float ky) const;
	ofVec2f toProjector(float kx, float ky) const;

	// ---- Shared members -------------------------------------------------

	std::shared_ptr<KinectProjector> kinectProjector;

	ofRectangle kinectROI;
	ofVec2f projRes;
	ofVec2f kinectRes;

	bool active = false;
	bool debugOn = false;
	float elapsed = 0.0f; // seconds active; advance in update() via tickClock()

	// Advance the module clock. Call once per update() in derived classes.
	// Returns the delta in seconds, clamped to avoid huge steps after a stall.
	float tickClock(float maxDelta = 0.1f);

private:
	void resampleState(ofRectangle const &oldROI, ofRectangle const &newROI);

	std::vector<std::vector<float>> state;
	int nLayers = 0;
	int cellSize = 4;
	int stateW = 0;
	int stateH = 0;

	float lastTick = 0.0f;
	bool clockStarted = false;
};