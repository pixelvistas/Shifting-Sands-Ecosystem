/***********************************************************************
PlantLayer - shows SuccessionModel's vegetation on the sand as individual
top-down plants (display only; the model is not changed).

- Sites: a jittered grid over the ROI, spacing PLANT_SPACING_MM, each site
  offset by up to +-0.35 spacing (quantised to 1/255 so the shader, which
  reads the offsets from the state texture, places it exactly where this
  class samples the model). Built once per ROI / spacing; they never move.
- Each frame a site looks at the model's cell under it: the dominant role,
  if its cover exceeds PLANT_MIN_COVER and the cell is not water, is the
  target, at radius MAX_RADIUS_MM[role] x cover. The drawn radius eases
  toward the target over PLANT_EASE_SEC; when the target role changes the
  old plant shrinks to nothing before the new one grows in.
- The state (role, radius, jitter) is packed into one RGBA8 texel per site
  (packState); heightMapShader.frag draws a filled disc with a darker rim
  per site, in mm on the sand, below the water overlay and contour lines.
  renderOver() is the same drawing on the CPU, for the headless harness;
  keep the two in sync.

Plain C++ (no openFrameworks), like SuccessionModel. VegetationField owns
one instance (site-feedback model only).

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include <cstdint>
#include <vector>

class PlantLayer
{
public:
	enum { ROLE_NONE = -1, PIONEER = 0, INTERMEDIATE = 1, CLIMAX = 2 };

	// cover[r]: SuccessionModel::cover(r) (cols x rows); water: nonzero =
	// water (may be null). Rebuilds the sites when the grid or spacing
	// changes.
	void update(const float * const cover[3], const unsigned char * water, int cols, int rows, float mmPerCell, float dt);

	int sitesX() const { return nx; }
	int sitesY() const { return ny; }
	float builtSpacingMM() const { return spacing; }
	// One RGBA8 texel per site, row-major (sitesX x sitesY):
	// R = role + 1 (0 = none), G = radius / radiusScale(), B/A = jitter + 0.5.
	void packState(std::vector<unsigned char> & rgba) const;
	float radiusScale() const; // the largest max radius, mm

	// CPU version of the shader's drawing, for the harness: composites the
	// plants over an RGB image of the grid area at pxPerMM pixels per mm
	// (image x = grid x * mmPerCell * pxPerMM). waterAtPx may be null.
	void renderOver(std::vector<unsigned char> & rgb, int w, int h, float pxPerMM,
		const unsigned char * waterAtPx = nullptr) const;

	// Plant display (2026-11-10): DISCS = this class's discs; GRAIN = the
	// Bayer grain dither in heightMapShader.frag (default); OFF. The disc
	// state is only updated in DISCS mode.
	enum { DISPLAY_DISCS = 0, DISPLAY_GRAIN = 1, DISPLAY_OFF = 2 };
	static int PLANT_DISPLAY;
	static bool drawDiscs() { return PLANT_DISPLAY == DISPLAY_DISCS; }
	// Grain dither: size multiplier (pioneer 1 px, intermediate 2, climax 4)
	// and brightness with band fill on / off (SandSurfaceRenderer picks one).
	static float GRAIN_SCALE;
	static float GRAIN_BRIGHTNESS_FILL;
	static float GRAIN_BRIGHTNESS_DARK;

	// Tunables (Vegetation panel -> Plants).
	static float PLANT_SPACING_MM;
	static float PLANT_MIN_COVER;
	static float PLANT_EASE_SEC;
	static float MAX_RADIUS_MM[3];
	// Fill and rim colours (rim = fill at 60% of its HSL lightness).
	static const unsigned char FILL[3][3];
	static const unsigned char RIM[3][3];

private:
	void build(int cols, int rows, float mmPerCell);
	int cols = 0, rows = 0, nx = 0, ny = 0;
	float mmPerCell = 0.0f, spacing = 0.0f;
	std::vector<float> jx, jy;         // offset in spacings, -0.35..0.35, quantised
	std::vector<int> role;             // drawn role
	std::vector<float> radius;         // drawn radius, mm
};
