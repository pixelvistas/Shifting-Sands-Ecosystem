/***********************************************************************
LatticeFlow.h - a coarse D2Q9 lattice-Boltzmann (LBM) fluid solved
inside the water VegetationField's virtual-pipe model says exists
(2026-10-20, the hybrid hydrology agreed with the user).

Division of labour:
- VegetationField::stepFlow() (virtual pipes, Mei et al. 2007) decides
  WHERE water is: wet/dry, depth, pooling, drainage - the physically
  based quantities the succession model's moisture field will read.
- LatticeFlow decides HOW that water moves, for display and seed
  transport: a 2D incompressible-fluid velocity field with the
  streaming, obstacle-skirting, eddying character of the references
  (Liu 2020 and Dewitz et al. 2017 both used a 2D LBM). Wet cells are
  fluid, dry sand is wall (halfway bounce-back), fluid enters through
  SOURCE_EDGE at INLET_SPEED and leaves through SINK_EDGE with a
  zero-gradient (copy-from-interior) open boundary. The other edges
  are walls. Read-only against VegetationField; nothing here feeds back
  into the hydrology.

Grid: VegetationField's grid downsampled by DOWNSAMPLE (default 4, so
~130x74 = ~9.6K cells on this box's ~519x295 grid); a coarse cell is
fluid when at least WET_FRACTION of its fine cells are water. Cost per
step is ~9.6K cells x 9 directions of plain arithmetic - well under the
virtual-pipe pass's own cost.

Stability: BGK collision with relaxation time TAU (kinematic viscosity
nu = (TAU - 0.5)/3 in lattice units). Lower TAU = thinner fluid, more
eddies, less stable; lattice speeds must stay well below ~0.3. Cells
whose density leaves a sane range (or goes non-finite) are reset to
rest, so a bad setting degrades locally instead of blowing up the field.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <vector>

class VegetationField;

class LatticeFlow
{
public:
	// Rebuilds the fluid mask from vf's water classification and runs
	// STEPS_PER_FRAME lattice steps. frameDt only scales the velocity
	// reported to callers (one lattice step = frameDt / STEPS_PER_FRAME).
	void update(const VegetationField & vf, float frameDt);

	// Velocity at grid position (gx, gy) in VegetationField grid cells
	// per second, bilinear between coarse cells; zero on walls.
	ofVec2f velocityAt(float gx, float gy) const;
	bool isReady() const { return nx > 0 && ny > 0; }

	// Readouts for the GUI.
	int getFluidCells() const { return fluidCells; }
	int getInletCells() const { return inletCells; }
	float getMaxLatticeSpeed() const { return maxLatticeSpeed; }
	float getLastMs() const { return lastMs; }
	int getResets() const { return resets; }

	static bool ENABLE;
	static int DOWNSAMPLE;      // fine grid cells per lattice cell
	static float TAU;           // BGK relaxation time (> 0.5)
	static float INLET_SPEED;   // lattice units per step, keep < ~0.15
	static int STEPS_PER_FRAME;
	static float WET_FRACTION;  // fraction of fine cells that must be water

private:
	static const int Q = 9;
	void resize(int newNx, int newNy);
	void setEquilibrium(int cell, float rho, float ux, float uy, float * dst);
	float cellDensity(const float * src, int cell) const;
	void cellMoments(const float * src, int cell, float & rho, float & u, float & v) const;

	int nx = 0, ny = 0, ds = 0;
	std::vector<float> f, fTmp;     // nx * ny * Q
	std::vector<float> ux, uy;      // per cell, lattice units
	std::vector<unsigned char> fluid, wasFluid;
	float velocityScale = 0.0f;     // lattice units -> grid cells / s

	int fluidCells = 0, inletCells = 0, resets = 0;
	float maxLatticeSpeed = 0.0f, lastMs = 0.0f;
};
