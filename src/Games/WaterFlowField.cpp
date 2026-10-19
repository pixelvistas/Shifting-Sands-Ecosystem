#include "WaterFlowField.h"
#include "ofxImGui.h"
#include <algorithm>
#include <cmath>

bool WaterFlowField::ENABLE_FLOW_FIELD = true;
int WaterFlowField::GLYPH_SPACING_CELLS = 8;
float WaterFlowField::MIN_VELOCITY_TO_SHOW = 0.0005f;
float WaterFlowField::ARROW_LENGTH_SCALE = 200.0f;
float WaterFlowField::ARROW_MIN_LEN_CELLS = 1.0f;
float WaterFlowField::ARROW_MAX_LEN_CELLS = 6.0f;
float WaterFlowField::GLYPH_THICKNESS = 2.0f;
float WaterFlowField::GLYPH_COLOR[3] = { 0.85f, 0.95f, 1.0f };

void WaterFlowField::setup(std::shared_ptr<KinectProjector> const& k, VegetationField* v)
{
	kinectProjector = k;
	vegetationField = v;
}

void WaterFlowField::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void WaterFlowField::setKinectROI(ofRectangle & KROI)
{
	kinectROI = KROI;
}

ofVec2f WaterFlowField::gridToKinectCoord(float gx, float gy) const
{
	ofVec2f origin = vegetationField->getGridOrigin();
	float step = vegetationField->getGridStep();
	return ofVec2f(origin.x + gx * step, origin.y + gy * step);
}

ofPoint WaterFlowField::gridToProjCoord(float gx, float gy) const
{
	ofVec2f kc = gridToKinectCoord(gx, gy);
	return kinectProjector->kinectCoordToProjCoord(kc.x, kc.y);
}

void WaterFlowField::drawArrow(ofVec2f gridPos, ofVec2f gridDir, float lengthCells)
{
	ofVec2f dir = gridDir.getNormalized();
	ofVec2f tipGrid = gridPos + dir * lengthCells;

	ofPoint base = gridToProjCoord(gridPos.x, gridPos.y);
	ofPoint tip = gridToProjCoord(tipGrid.x, tipGrid.y);
	ofDrawLine(base, tip);

	// Arrowhead - two short strokes back from the tip, the standard
	// glyph shape: rotate the reversed shaft direction +-25 degrees.
	float headLen = lengthCells * 0.35f;
	float a = ofDegToRad(25.0f);
	ofVec2f back = dir * -1.0f;
	ofVec2f head1Dir(back.x * cosf(a) - back.y * sinf(a), back.x * sinf(a) + back.y * cosf(a));
	ofVec2f head2Dir(back.x * cosf(-a) - back.y * sinf(-a), back.x * sinf(-a) + back.y * cosf(-a));
	ofPoint head1 = gridToProjCoord(tipGrid.x + head1Dir.x * headLen, tipGrid.y + head1Dir.y * headLen);
	ofPoint head2 = gridToProjCoord(tipGrid.x + head2Dir.x * headLen, tipGrid.y + head2Dir.y * headLen);
	ofDrawLine(tip, head1);
	ofDrawLine(tip, head2);
}

void WaterFlowField::update()
{
	if (!vegetationField || vegetationField->getCols() <= 0)
		return;

	int cols = vegetationField->getCols();
	int rows = vegetationField->getRows();
	int spacing = std::max(1, GLYPH_SPACING_CELLS);

	fbo.begin();
	ofClear(255, 255, 255, 0);
	if (ENABLE_FLOW_FIELD) {
		ofSetColor(GLYPH_COLOR[0] * 255.0f, GLYPH_COLOR[1] * 255.0f, GLYPH_COLOR[2] * 255.0f);
		ofSetLineWidth(GLYPH_THICKNESS);
		for (int gy = spacing / 2; gy < rows; gy += spacing) {
			for (int gx = spacing / 2; gx < cols; gx += spacing) {
				ofVec2f kc = gridToKinectCoord((float)gx, (float)gy);
				// Gate on the same hysteresis-stabilized classification
				// Critter/HumanAgent movement and the renderer already
				// read (see isWaterAt()'s own header note) - drawing a
				// flow arrow somewhere that isn't real water by the
				// simulation's own definition would misrepresent state
				// that doesn't exist, regardless of whether it happens
				// to look blue on screen (see CLAUDE.md's note on the
				// separate HeightColorMap/VegetationField elevation-
				// scale mismatch on this branch).
				if (!vegetationField->isWaterAt(kc.x, kc.y))
					continue;

				ofVec2f v = vegetationField->getFlowVelocityAt(gx, gy);
				float speed = v.length();
				if (speed < MIN_VELOCITY_TO_SHOW)
					continue; // calm water - no real flow to show here, not a bug

				float lenCells = ofClamp(speed * ARROW_LENGTH_SCALE, ARROW_MIN_LEN_CELLS, ARROW_MAX_LEN_CELLS);
				drawArrow(ofVec2f((float)gx, (float)gy), v, lenCells);
			}
		}
	}
	fbo.end();
}

void WaterFlowField::drawMainWindow(float x, float y, float width, float height)
{
	fbo.draw(x, y, width, height);
}

void WaterFlowField::drawProjectorWindow()
{
	fbo.draw(0, 0);
}

void WaterFlowField::drawGui()
{
	ImGui::Begin("Water Flow");
	ImGui::Text("A dense field of arrows over the water, each showing");
	ImGui::Text("stepFlow()'s real local flow direction and relative");
	ImGui::Text("speed - redrawn fresh every frame, no tracked state.");
	ImGui::Checkbox("Enable flow field", &ENABLE_FLOW_FIELD);
	ImGui::SliderInt("Glyph spacing (cells)", &GLYPH_SPACING_CELLS, 2, 40);
	ImGui::SliderFloat("Min velocity to show", &MIN_VELOCITY_TO_SHOW, 0.0f, 1.0f, "%.4f");
	ImGui::SliderFloat("Arrow length scale (visualization only)", &ARROW_LENGTH_SCALE, 0.0f, 2000.0f);
	ImGui::SliderFloat("Arrow min length (cells)", &ARROW_MIN_LEN_CELLS, 0.5f, 10.0f);
	ImGui::SliderFloat("Arrow max length (cells)", &ARROW_MAX_LEN_CELLS, 1.0f, 30.0f);
	ImGui::SliderFloat("Glyph thickness", &GLYPH_THICKNESS, 0.5f, 10.0f);
	ImGui::ColorEdit3("Glyph color", GLYPH_COLOR);

	// Same "measure before tuning" posture as every other panel in this
	// project - lets the sliders above be set from a real number instead
	// of by feel once this is actually on hardware.
	if (vegetationField && vegetationField->getCols() > 0) {
		int cx = vegetationField->getCols() / 2;
		int cy = vegetationField->getRows() / 2;
		float depth = vegetationField->getWaterDepthAt(cx, cy);
		ofVec2f v = vegetationField->getFlowVelocityAt(cx, cy);
		ImGui::Text("ROI-center depth: %.4f, flow velocity: (%.4f, %.4f), |v|=%.4f",
			depth, v.x, v.y, v.length());
	}

	ImGui::End();
}
