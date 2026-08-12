#include "CritterController.h"
#include "ofxImGui.h"

void CCritterController::setup(std::shared_ptr<KinectProjector> const& k)
{
	kinectProjector = k;
	handField.setup(k);
	draggedTangible = -1;
	showHandDebug = false;
	gradientFlipped = false;
}

void CCritterController::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void CCritterController::setKinectRes(ofVec2f & KR)
{
	kinectRes = KR;
}

void CCritterController::setKinectROI(ofRectangle & KROI)
{
	bool firstValidROI = (kinectROI.width <= 0) && (KROI.width > 0);
	kinectROI = KROI;

	Critter::setDrawFlipped(kinectProjector->getProjectionFlipped());

	if (firstValidROI) {
		addCritters(200);
		addTangible();
	}
}

ofPoint CCritterController::randomLocationInROI()
{
	double W = kinectROI.getWidth() * 0.8;
	double H = kinectROI.getHeight() * 0.8;
	double X = kinectROI.getLeft() + 0.1 * kinectROI.getWidth();
	double Y = kinectROI.getTop() + 0.1 * kinectROI.getHeight();
	return ofPoint(ofRandom(X, X + W), ofRandom(Y, Y + H));
}

void CCritterController::addCritters(int n)
{
	if (kinectROI.width <= 0)
		return;

	for (int i = 0; i < n; i++) {
		Critter c(kinectProjector, randomLocationInROI(), kinectROI);
		c.setup();
		critters.push_back(c);
	}
}

void CCritterController::addTangible()
{
	if (kinectROI.width <= 0)
		return;

	tangibles.push_back(Tangible(kinectProjector, randomLocationInROI(), kinectROI));
}

void CCritterController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();

	for (auto & c : critters)
		c.update(handField, tangibles);

	for (auto & t : tangibles)
		t.update(handField);

	fbo.begin();
	ofClear(255, 255, 255, 0);

	for (auto & t : tangibles)
		t.draw();
	for (auto & c : critters)
		c.draw();

	if (showHandDebug) {
		// Fixed-position thumbnail, not spatially registered to the sand -
		// same "approximate preview" convention as the rest of this window.
		handField.draw(10, 10, 160, 120);
	}

	fbo.end();
}

void CCritterController::drawMainWindow(float x, float y, float width, float height)
{
	fbo.draw(x, y, width, height);
}

void CCritterController::drawProjectorWindow()
{
	fbo.draw(0, 0);
}

void CCritterController::drawGui()
{
	ImGui::Begin("Critters");
	ImGui::Text("%d critters, %d tangibles", (int)critters.size(), (int)tangibles.size());

	ImGui::SliderFloat("Gravity", &Critter::GRAVITY, 0.0f, 0.3f);
	ImGui::SliderFloat("Damping", &Critter::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Sleep speed", &Critter::SLEEP_SPEED, 0.0f, 0.5f);
	ImGui::SliderInt("Sleep frames", &Critter::SLEEP_FRAME_THRESHOLD, 1, 120);
	ImGui::SliderFloat("Hand push", &Critter::HAND_PUSH_STRENGTH, 0.0f, 10.0f);
	if (ImGui::Checkbox("Flip gradient sign", &gradientFlipped))
		Critter::GRADIENT_SIGN = gradientFlipped ? -1.0f : 1.0f;

	ImGui::Separator();
	ImGui::SliderFloat("Hand threshold", &HandField::THRESHOLD, 1.0f, 100.0f);
	ImGui::Checkbox("Show hand mask", &showHandDebug);

	ImGui::Separator();
	ImGui::SliderFloat("Tangible damping", &Tangible::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Tangible hand push", &Tangible::HAND_PUSH_STRENGTH, 0.0f, 10.0f);

	if (ImGui::Button("Add 50 critters"))
		addCritters(50);
	ImGui::SameLine();
	if (ImGui::Button("Remove all critters"))
		critters.clear();
	if (ImGui::Button("Add tangible"))
		addTangible();

	ImGui::End();
}

void CCritterController::mousePressed(int x, int y, int button)
{
	for (size_t i = 0; i < tangibles.size(); i++) {
		float d = (ofPoint(x, y) - tangibles[i].getLocation()).length();
		if (d < tangibles[i].getRadius() + 6) {
			draggedTangible = (int)i;
			tangibles[i].setDragging(true);
			break;
		}
	}
}

void CCritterController::mouseDragged(int x, int y, int button)
{
	if (draggedTangible >= 0 && draggedTangible < (int)tangibles.size())
		tangibles[draggedTangible].setLocation(ofPoint(x, y));
}

void CCritterController::mouseReleased(int x, int y, int button)
{
	if (draggedTangible >= 0 && draggedTangible < (int)tangibles.size())
		tangibles[draggedTangible].setDragging(false);
	draggedTangible = -1;
}
