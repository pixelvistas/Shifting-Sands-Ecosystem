#include "VegetationLayer.h"
#include "ofxImGui.h"
#include <algorithm>

float VegetationLayer::SI_MOIST[4] = { 0.02f, 0.12f, 0.65f, 0.95f };

// Slope units match gradientAtKinectCoord()'s raw, unnormalized magnitude -
// these starting brackets are a guess pending real-hardware tuning (see
// header note), picked wide enough that a mostly-flat calibrated sandbox
// shouldn't read as uniformly unsuitable.
float VegetationLayer::SI_SLOPE[4] = { 0.0f, 0.0f, 3.0f, 8.0f };

float VegetationLayer::R_ESTAB = 0.8f;
float VegetationLayer::GROWTH_RATE = 0.6f;
float VegetationLayer::MORTALITY_RATE = 0.3f;
// Spec's published pioneer K_max is 0.4 (ECOSIMSPEC.md §6) - raised here so
// a fully-suitable cell actually reads as visually full on a first pass at
// getting vegetation on screen at all. Lower this once the look is judged
// against real vegetation density, not just "is anything visible."
float VegetationLayer::K_MAX = 0.85f;
float VegetationLayer::SIM_YEARS_PER_SECOND = 1.0f;

float VegetationLayer::ESTABLISH_BELOW_DENSITY = 0.03f;
float VegetationLayer::SEED_DENSITY = 0.05f;

float VegetationLayer::MIN_DRAW_DENSITY = 0.05f;
float VegetationLayer::FULL_COVER_DENSITY = 0.6f;
int VegetationLayer::STIPPLE_DENSITY = 5;
float VegetationLayer::SPRITE_MIN = 1.5f;
float VegetationLayer::SPRITE_MAX = 4.0f;
ofColor VegetationLayer::SPECIES_COLOR = ofColor(150, 220, 60); // pioneer: bright yellow-green, ECOSIMSPEC.md §5.11.3

namespace {
	const int VEG_GRID_STEP = 16; // kinect px per cell - coarser than moisture's grid, keeps per-frame stipple draw calls reasonable

	// §5.6's trapezoidal SI curve, verbatim.
	float trapezoidalSI(float x, float mn, float optLo, float optHi, float mx)
	{
		if (x <= mn || x >= mx) return 0.0f;
		if (x < optLo) return (x - mn) / (optLo - mn);
		if (x <= optHi) return 1.0f;
		return (mx - x) / (mx - optHi);
	}

	float smoothstep(float edge0, float edge1, float x)
	{
		float t = ofClamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}
}

void VegetationLayer::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	gridStep = VEG_GRID_STEP;
	cols = 0;
	rows = 0;
}

void VegetationLayer::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void VegetationLayer::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
	if (kinectROI.width <= 0 || kinectROI.height <= 0)
		return;

	int newCols = std::max(1, (int)(kinectROI.width / gridStep));
	int newRows = std::max(1, (int)(kinectROI.height / gridStep));
	if (newCols == cols && newRows == rows)
		return; // dimensions unchanged - keep standing vegetation, same convention as HydrologyLayer's moisture grid

	cols = newCols;
	rows = newRows;
	density = cv::Mat::zeros(rows, cols, CV_32F);
}

float VegetationLayer::suitabilityAt(float moisture, float slope) const
{
	float siMoist = trapezoidalSI(moisture, SI_MOIST[0], SI_MOIST[1], SI_MOIST[2], SI_MOIST[3]);
	float siSlope = trapezoidalSI(slope, SI_SLOPE[0], SI_SLOPE[1], SI_SLOPE[2], SI_SLOPE[3]);
	// Geometric mean of 2 factors (no soil term until step 5) - §5.6.
	return sqrt(siMoist * siSlope);
}

void VegetationLayer::update(HydrologyLayer & hydrology)
{
	if (density.empty() || !kinectProjector->isImageStabilized())
		return;

	float dtSim = ofGetLastFrameTime() * SIM_YEARS_PER_SECOND;

	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float kx = kinectROI.x + gx * gridStep + gridStep / 2.0f;
			float ky = kinectROI.y + gy * gridStep + gridStep / 2.0f;

			float moisture = hydrology.getMoistureAt(kx, ky);
			float slope = kinectProjector->gradientAtKinectCoord(kx, ky).length();
			float S = suitabilityAt(moisture, slope);

			float & V = density.at<float>(gy, gx);

			float K = K_MAX * S;
			if (K > 0.01f)
				V += GROWTH_RATE * V * (1.0f - V / K) * dtSim;
			V -= MORTALITY_RATE * (1.0f - S) * V * dtSim;

			if (V < ESTABLISH_BELOW_DENSITY) {
				float pEstab = R_ESTAB * S * dtSim;
				if (ofRandom(1.0f) < pEstab)
					V = std::max(V, SEED_DENSITY);
			}

			V = ofClamp(V, 0.0f, 1.0f);
		}
	}

	draw();
}

void VegetationLayer::draw()
{
	if (!fbo.isAllocated())
		return;

	fbo.begin();
	ofClear(255, 255, 255, 0);
	ofPushStyle();
	ofFill();

	for (int gy = 0; gy < rows; gy++) {
		for (int gx = 0; gx < cols; gx++) {
			float V = density.at<float>(gy, gx);
			if (V < MIN_DRAW_DENSITY)
				continue;

			float alpha = smoothstep(MIN_DRAW_DENSITY, FULL_COVER_DENSITY, V);
			int n = std::max(1, (int)ofMap(alpha, 0.0f, 1.0f, 1, STIPPLE_DENSITY));

			float kxCell = kinectROI.x + gx * gridStep;
			float kyCell = kinectROI.y + gy * gridStep;

			for (int i = 0; i < n; i++) {
				float kx = kxCell + ofRandom(gridStep);
				float ky = kyCell + ofRandom(gridStep);
				ofVec2f p = kinectProjector->kinectCoordToProjCoord(kx, ky);
				float size = ofMap(V, MIN_DRAW_DENSITY, 1.0f, SPRITE_MIN, SPRITE_MAX, true);
				ofSetColor(SPECIES_COLOR, (int)(alpha * 255));
				ofDrawCircle(p.x, p.y, size);
			}
		}
	}

	ofPopStyle();
	fbo.end();
}

void VegetationLayer::drawMainWindow(float x, float y, float width, float height)
{
	if (fbo.isAllocated())
		fbo.draw(x, y, width, height);
}

void VegetationLayer::drawProjectorWindow()
{
	if (fbo.isAllocated())
		fbo.draw(0, 0);
}

void VegetationLayer::drawGui()
{
	ImGui::Begin("Vegetation");
	ImGui::Text("Single species (pioneer) - build order step 4.");
	ImGui::Text("No soil, no seed bank yet - see VegetationLayer.h.");

	ImGui::Separator();
	ImGui::Text("Suitability curves (min, opt_lo, opt_hi, max):");
	ImGui::SliderFloat4("Moisture SI", SI_MOIST, 0.0f, 1.0f);
	ImGui::SliderFloat4("Slope SI", SI_SLOPE, 0.0f, 15.0f);

	ImGui::Separator();
	ImGui::SliderFloat("Establishment rate", &R_ESTAB, 0.0f, 3.0f);
	ImGui::SliderFloat("Growth rate", &GROWTH_RATE, 0.0f, 3.0f);
	ImGui::SliderFloat("Mortality rate", &MORTALITY_RATE, 0.0f, 3.0f);
	ImGui::SliderFloat("Max density (K_max)", &K_MAX, 0.05f, 1.0f);
	ImGui::SliderFloat("Sim years / real second", &SIM_YEARS_PER_SECOND, 0.05f, 10.0f);

	ImGui::Separator();
	ImGui::SliderFloat("Min draw density", &MIN_DRAW_DENSITY, 0.0f, 0.3f);
	ImGui::SliderFloat("Full cover density", &FULL_COVER_DENSITY, 0.1f, 1.0f);
	ImGui::SliderInt("Stipple points/cell", &STIPPLE_DENSITY, 1, 20);
	ImGui::SliderFloat("Sprite size min", &SPRITE_MIN, 0.5f, 10.0f);
	ImGui::SliderFloat("Sprite size max", &SPRITE_MAX, 0.5f, 15.0f);

	if (ImGui::Button("Clear vegetation"))
		density.setTo(0.0f);

	ImGui::End();
}
