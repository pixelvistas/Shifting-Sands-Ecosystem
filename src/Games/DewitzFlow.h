/***********************************************************************
DewitzFlow.h - the flow-particle system of Dewitz et al. (2018), "AR
Sound Sandbox: A Playful Interface for Musical and Artistic Expression",
Section 3.3, as specified for this project (docs/DEVLOG.md, 2026-10-21):

1. A 2D D2Q9 lattice-Boltzmann flow across the WHOLE sandbox, entering
   along one edge and leaving along the opposite edge. Collision is
   multi-relaxation-time (MRT, Lallemand & Luo 2000) with the shear rate
   set from TAU (default 1.98); a BGK toggle is kept for comparison.
2. Obstacles from the depth image: smoothed elevation above
   OBSTACLE_HEIGHT_MM or slope above OBSTACLE_SLOPE is a solid
   (bounce-back) cell. The mask is smoothed in time with hysteresis so
   Kinect noise does not flicker walls; a wall that opens starts at
   equilibrium with its fluid neighbours' mean velocity.
3. A gradient map of the smoothed terrain gives the direction of
   steepest descent, magnitude capped at GRADIENT_SATURATION.
4. Attractor points pull particles with a gravity-like (1/r^2, capped)
   force.
5. Each particle moves by SPEED * (W_FLOW * u/|u|max + W_GRAD * downhill
   + W_ATTR * attractors). Particles that enter an obstacle or leave the
   box respawn on the inlet edge.

Deliberately independent of openFrameworks (standard library only): it
takes the elevation grid as a plain array, so tools/dewitz_harness can
compile this exact file headless. Drawing is WaterFlowField's job.

Coordinates: "fine" = VegetationField's grid (one cell per Kinect pixel
in the ROI); the lattice is that grid downsampled by DOWNSAMPLE.
Read-only with respect to the hydrology - VegetationField::stepFlow()
(the moisture model) is untouched by and unaware of this class.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include <cstdint>
#include <random>
#include <vector>

class DewitzFlow
{
public:
	struct Attractor { float x, y; }; // fine grid coordinates

	// One frame: terrain/obstacles/gradient, STEPS_PER_FRAME lattice
	// steps, then particle advection. elevMM is cols x rows, row-major,
	// in mm (non-finite values are ignored). mmPerCell is the real size
	// of one fine cell, used to turn height differences into slopes.
	void update(const float * elevMM, int cols, int rows, float mmPerCell, float dt);

	// The three stages, public so the harness can time them separately.
	void updateTerrain(const float * elevMM, int cols, int rows, float mmPerCell, float dt);
	void stepLattice();
	void advectParticles(float dt);

	// Particles in fine grid coordinates.
	int particleCount() const { return (int)px.size(); }
	const std::vector<float> & particleX() const { return px; }
	const std::vector<float> & particleY() const { return py; }
	// Position at the start of the last advection step. A particle that
	// respawned in that step has prev == current (zero-length segment), so
	// a line from prev to current never jumps across the box.
	const std::vector<float> & particlePrevX() const { return prevX; }
	const std::vector<float> & particlePrevY() const { return prevY; }

	// Lattice inspection (lattice coordinates).
	int latticeWidth() const { return nx; }
	int latticeHeight() const { return ny; }
	bool isObstacle(int i, int j) const { return solid[j * nx + i] != 0; }
	float speedAt(int i, int j) const;
	// The flow term (u/|u|max) and downhill term (capped) at a fine-grid
	// position, before weighting - for inspection/readouts.
	void sampleTerms(float x, float y, float & flowX, float & flowY, float & downhillX, float & downhillY) const;
	float densityAt(int i, int j) const;

	// Readouts.
	long long getResets() const { return resets; }
	long long getNonFinite() const { return nonFinite; }
	long long getSteps() const { return steps; }
	int getObstacleCells() const { return obstacleCells; }
	int getMaskFlips() const { return maskFlips; }   // wall<->fluid changes, last frame
	float getMaxSpeed() const { return maxSpeed; }   // lattice units
	float getLatticeMs() const { return latticeMs; } // last frame, all steps
	float getParticleMs() const { return particleMs; }
	int getRespawnsLastFrame() const { return respawnsLastFrame; }
	int getExitsLastFrame() const { return exitsLastFrame; }       // left through the outlet edge

	std::vector<Attractor> attractors;

	enum { EDGE_LEFT = 0, EDGE_RIGHT = 1, EDGE_TOP = 2, EDGE_BOTTOM = 3 };

	// Tunables (see the header note; defaults in the .cpp).
	static int DOWNSAMPLE;             // fine cells per lattice cell (2-4)
	static float TAU;                  // shear relaxation time, Dewitz: 1.98
	static bool USE_MRT;               // false = BGK, for comparison
	static float INLET_SPEED;          // lattice units per step
	static int INLET_EDGE;             // outlet is the opposite edge
	static int STEPS_PER_FRAME;
	static float OBSTACLE_HEIGHT_MM;   // above this = solid
	static float OBSTACLE_SLOPE;       // rise/run above this = solid
	static float OBSTACLE_SMOOTH_SEC;  // time constant of the mask evidence
	static float TERRAIN_SMOOTH_SEC;   // time constant of the elevation EMA
	static float GRADIENT_SATURATION;  // slope at which downhill term = 1
	static float W_FLOW, W_GRAD, W_ATTR;
	static float SPEED;                // fine cells per second at |dir| = 1
	static float ATTRACTOR_RADIUS;     // fine cells; full pull inside this
	static int PARTICLE_COUNT;
	static float PARTICLE_LIFE_SEC;    // 0 = particles live until they exit

private:
	static const int Q = 9;
	void resize(int cols, int rows, int ds);
	void setEquilibrium(float * fc, float rho, float u, float v) const;
	void collideMRT(float * fc, float & rho, float & u, float & v) const;
	void collideBGK(float * fc, float & rho, float & u, float & v) const;
	void respawn(int k, bool anywhere);
	bool solidAtFine(float x, float y) const;
	float bilerp(const std::vector<float> & m, float lx, float ly) const;

	int cols = 0, rows = 0, ds = 0, nx = 0, ny = 0;
	float mmPerLatticeCell = 1.0f;
	bool terrainReady = false;

	std::vector<float> f, fTmp;         // nx * ny * Q
	std::vector<float> ux, uy, rho;     // per lattice cell
	std::vector<float> elev;            // smoothed elevation, mm
	std::vector<float> blurred;         // spatially smoothed elevation, mm
	std::vector<float> downX, downY;    // capped downhill direction
	std::vector<float> evidence;        // obstacle evidence, 0..1
	std::vector<unsigned char> solid;

	std::vector<float> px, py, age;
	std::vector<float> prevX, prevY;
	std::mt19937 rng{ 12345 };

	long long resets = 0, nonFinite = 0, steps = 0;
	int obstacleCells = 0, maskFlips = 0, respawnsLastFrame = 0, exitsLastFrame = 0;
	float maxSpeed = 0.0f, latticeMs = 0.0f, particleMs = 0.0f;
};
