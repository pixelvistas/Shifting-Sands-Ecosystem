/***********************************************************************
SandboxModule.cpp - Base class for extensible sandbox modules
Copyright (c) 2026 <YOUR NAME> (Shifting Sands - Ecosystem, York University)

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

#include "SandboxModule.h"

#include <algorithm>
#include <cmath>
#include <fstream>

// ---------------------------------------------------------------- lifecycle

void SandboxModule::setup(std::shared_ptr<KinectProjector> const &k)
{
	kinectProjector = k;

	if (kinectProjector)
	{
		kinectRes = kinectProjector->getKinectRes();
		kinectROI = kinectProjector->getKinectROI();
	}

	clockStarted = false;
	elapsed = 0.0f;
}

void SandboxModule::activate()
{
	active = true;
	clockStarted = false; // restart the clock cleanly on re-entry
	onActivate();
}

void SandboxModule::deactivate()
{
	active = false;
	onDeactivate();
}

float SandboxModule::tickClock(float maxDelta)
{
	const float now = ofGetElapsedTimef();

	if (!clockStarted)
	{
		lastTick = now;
		clockStarted = true;
		return 0.0f;
	}

	float dt = now - lastTick;
	lastTick = now;

	// Clamp so that a stall (calibration, window drag, debugger break) does
	// not deliver a multi-second step into an accumulator.
	dt = std::max(0.0f, std::min(dt, maxDelta));

	elapsed += dt;
	return dt;
}

// ------------------------------------------------------------- state alloc

void SandboxModule::allocateState(int layers, int cell, float initial)
{
	nLayers = std::max(0, layers);
	cellSize = std::max(1, cell);

	stateW = static_cast<int>(std::ceil(kinectROI.width / cellSize));
	stateH = static_cast<int>(std::ceil(kinectROI.height / cellSize));

	stateW = std::max(stateW, 1);
	stateH = std::max(stateH, 1);

	state.assign(nLayers, std::vector<float>(
							  static_cast<size_t>(stateW) * static_cast<size_t>(stateH), initial));

	ofLogNotice("SandboxModule") << getName() << ": allocated " << nLayers
								 << " state layer(s) at " << stateW << "x" << stateH
								 << " (cell " << cellSize << " px)";
}

// ------------------------------------------------------------ state access

bool SandboxModule::inStateBounds(int sx, int sy) const
{
	return sx >= 0 && sy >= 0 && sx < stateW && sy < stateH;
}

float SandboxModule::getState(int layer, int sx, int sy) const
{
	if (layer < 0 || layer >= nLayers || !inStateBounds(sx, sy))
		return 0.0f;
	return state[layer][static_cast<size_t>(sy) * stateW + sx];
}

void SandboxModule::setState(int layer, int sx, int sy, float v)
{
	if (layer < 0 || layer >= nLayers || !inStateBounds(sx, sy))
		return;
	state[layer][static_cast<size_t>(sy) * stateW + sx] = v;
}

void SandboxModule::addState(int layer, int sx, int sy, float v)
{
	if (layer < 0 || layer >= nLayers || !inStateBounds(sx, sy))
		return;
	state[layer][static_cast<size_t>(sy) * stateW + sx] += v;
}

void SandboxModule::stateToKinect(int sx, int sy, float &kx, float &ky) const
{
	kx = kinectROI.x + (sx + 0.5f) * cellSize;
	ky = kinectROI.y + (sy + 0.5f) * cellSize;
}

void SandboxModule::kinectToState(float kx, float ky, int &sx, int &sy) const
{
	sx = static_cast<int>((kx - kinectROI.x) / cellSize);
	sy = static_cast<int>((ky - kinectROI.y) / cellSize);
}

float SandboxModule::getStateAtKinect(int layer, float kx, float ky) const
{
	int sx, sy;
	kinectToState(kx, ky, sx, sy);
	return getState(layer, sx, sy);
}

void SandboxModule::setStateAtKinect(int layer, float kx, float ky, float v)
{
	int sx, sy;
	kinectToState(kx, ky, sx, sy);
	setState(layer, sx, sy, v);
}

void SandboxModule::addStateAtKinect(int layer, float kx, float ky, float v)
{
	int sx, sy;
	kinectToState(kx, ky, sx, sy);
	addState(layer, sx, sy, v);
}

void SandboxModule::clearState(int layer, float v)
{
	if (layer < 0 || layer >= nLayers)
		return;
	std::fill(state[layer].begin(), state[layer].end(), v);
}

void SandboxModule::clearAllState(float v)
{
	for (int i = 0; i < nLayers; ++i)
		clearState(i, v);
}

float SandboxModule::getStateMin(int layer) const
{
	if (layer < 0 || layer >= nLayers || state[layer].empty())
		return 0.0f;
	return *std::min_element(state[layer].begin(), state[layer].end());
}

float SandboxModule::getStateMax(int layer) const
{
	if (layer < 0 || layer >= nLayers || state[layer].empty())
		return 0.0f;
	return *std::max_element(state[layer].begin(), state[layer].end());
}

float SandboxModule::getStateMean(int layer) const
{
	if (layer < 0 || layer >= nLayers || state[layer].empty())
		return 0.0f;
	double sum = 0.0;
	for (float f : state[layer])
		sum += f;
	return static_cast<float>(sum / state[layer].size());
}

// ----------------------------------------------------------- ROI / resample

void SandboxModule::setKinectROI(ofRectangle const &roi)
{
	if (roi == kinectROI)
		return;

	const ofRectangle oldROI = kinectROI;

	if (nLayers > 0)
	{
		// IMPORTANT: resample rather than clear. The accumulated state is the
		// point of this class; discarding it on an ROI change would look like
		// a module bug and would silently break any mechanic that depends on
		// history surviving a recalibration.
		resampleState(oldROI, roi);
	}

	kinectROI = roi;
	onStateResampled(oldROI, roi);
}

void SandboxModule::resampleState(ofRectangle const &oldROI,
								  ofRectangle const &newROI)
{
	const int newW = std::max(1,
							  static_cast<int>(std::ceil(newROI.width / cellSize)));
	const int newH = std::max(1,
							  static_cast<int>(std::ceil(newROI.height / cellSize)));

	std::vector<std::vector<float>> resampled(
		nLayers, std::vector<float>(
					 static_cast<size_t>(newW) * static_cast<size_t>(newH), 0.0f));

	// Map each new cell back to absolute kinect coordinates, then into the
	// old grid. Nearest-neighbour: state layers are frequently categorical
	// (species present, burned/unburned) and interpolation would invent
	// values that never occurred. Cells outside the old ROI stay at 0.
	for (int layer = 0; layer < nLayers; ++layer)
	{
		for (int ny = 0; ny < newH; ++ny)
		{
			for (int nx = 0; nx < newW; ++nx)
			{

				const float kx = newROI.x + (nx + 0.5f) * cellSize;
				const float ky = newROI.y + (ny + 0.5f) * cellSize;

				const int ox = static_cast<int>((kx - oldROI.x) / cellSize);
				const int oy = static_cast<int>((ky - oldROI.y) / cellSize);

				if (ox >= 0 && oy >= 0 && ox < stateW && oy < stateH)
				{
					resampled[layer][static_cast<size_t>(ny) * newW + nx] =
						state[layer][static_cast<size_t>(oy) * stateW + ox];
				}
			}
		}
	}

	state = std::move(resampled);
	stateW = newW;
	stateH = newH;

	ofLogNotice("SandboxModule") << getName()
								 << ": state resampled to " << stateW << "x" << stateH
								 << " after ROI change";
}

// -------------------------------------------------------------- projector

float SandboxModule::elevationAt(float kx, float ky) const
{
	if (!kinectProjector)
		return 0.0f;
	return kinectProjector->elevationAtKinectCoord(kx, ky);
}

ofVec2f SandboxModule::gradientAt(float kx, float ky) const
{
	if (!kinectProjector)
		return ofVec2f(0.0f, 0.0f);
	return kinectProjector->gradientAtKinectCoord(kx, ky);
}

ofVec2f SandboxModule::toProjector(float kx, float ky) const
{
	if (!kinectProjector)
		return ofVec2f(0.0f, 0.0f);
	return kinectProjector->kinectCoordToProjCoord(kx, ky);
}

// ------------------------------------------------------------ persistence

bool SandboxModule::saveState(std::string const &path) const
{
	std::ofstream f(ofToDataPath(path, true), std::ios::binary);
	if (!f)
	{
		ofLogError("SandboxModule") << "could not open " << path << " for writing";
		return false;
	}

	const int magic = 0x53534553; // 'SSES'
	const int version = 1;
	f.write(reinterpret_cast<const char *>(&magic), sizeof(int));
	f.write(reinterpret_cast<const char *>(&version), sizeof(int));
	f.write(reinterpret_cast<const char *>(&nLayers), sizeof(int));
	f.write(reinterpret_cast<const char *>(&cellSize), sizeof(int));
	f.write(reinterpret_cast<const char *>(&stateW), sizeof(int));
	f.write(reinterpret_cast<const char *>(&stateH), sizeof(int));
	f.write(reinterpret_cast<const char *>(&elapsed), sizeof(float));

	for (int i = 0; i < nLayers; ++i)
	{
		f.write(reinterpret_cast<const char *>(state[i].data()),
				static_cast<std::streamsize>(state[i].size() * sizeof(float)));
	}

	return f.good();
}

bool SandboxModule::loadState(std::string const &path)
{
	std::ifstream f(ofToDataPath(path, true), std::ios::binary);
	if (!f)
	{
		ofLogError("SandboxModule") << "could not open " << path << " for reading";
		return false;
	}

	int magic = 0, version = 0, layers = 0, cell = 0, w = 0, h = 0;
	float el = 0.0f;

	f.read(reinterpret_cast<char *>(&magic), sizeof(int));
	f.read(reinterpret_cast<char *>(&version), sizeof(int));
	f.read(reinterpret_cast<char *>(&layers), sizeof(int));
	f.read(reinterpret_cast<char *>(&cell), sizeof(int));
	f.read(reinterpret_cast<char *>(&w), sizeof(int));
	f.read(reinterpret_cast<char *>(&h), sizeof(int));
	f.read(reinterpret_cast<char *>(&el), sizeof(float));

	if (!f || magic != 0x53534553 || version != 1)
	{
		ofLogError("SandboxModule") << path << " is not a valid state file";
		return false;
	}

	std::vector<std::vector<float>> loaded(
		layers, std::vector<float>(
					static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f));

	for (int i = 0; i < layers; ++i)
	{
		f.read(reinterpret_cast<char *>(loaded[i].data()),
			   static_cast<std::streamsize>(loaded[i].size() * sizeof(float)));
		if (!f)
		{
			ofLogError("SandboxModule") << "truncated state file: " << path;
			return false;
		}
	}

	state = std::move(loaded);
	nLayers = layers;
	cellSize = cell;
	stateW = w;
	stateH = h;
	elapsed = el;

	ofLogNotice("SandboxModule") << getName() << ": loaded state from " << path;
	return true;
}