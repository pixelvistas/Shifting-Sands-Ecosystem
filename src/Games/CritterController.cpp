#include "CritterController.h"
#include "ofxImGui.h"

void CCritterController::setup(std::shared_ptr<KinectProjector> const& k, PuckTracker* tracker, CSonicWaveController* sonicWave)
{
	kinectProjector = k;
	handField.setup(k);
	puckTracker = tracker;
	sonicWaveController = sonicWave;
	draggedTangible = -1;
	showHandDebug = false;
	gradientFlipped = false;
	critterSpawnCount = 5;
	bodyColorRGB[0] = Critter::BODY_COLOR.r / 255.0f;
	bodyColorRGB[1] = Critter::BODY_COLOR.g / 255.0f;
	bodyColorRGB[2] = Critter::BODY_COLOR.b / 255.0f;
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

	playArea = kinectROI;
	playArea.scaleFromCenter(0.9f, 0.9f); // 5% margin on each side

	Critter::setDrawFlipped(kinectProjector->getProjectionFlipped());

	if (firstValidROI) {
		addCritters(critterSpawnCount);
		addTangible();
	}
}

ofPoint CCritterController::randomLocationInROI()
{
	double W = playArea.getWidth() * 0.8;
	double H = playArea.getHeight() * 0.8;
	double X = playArea.getLeft() + 0.1 * playArea.getWidth();
	double Y = playArea.getTop() + 0.1 * playArea.getHeight();
	return ofPoint(ofRandom(X, X + W), ofRandom(Y, Y + H));
}

void CCritterController::addCritters(int n) {
	if (playArea.width <= 0)
		return;

	for (int i = 0; i < n; i++) {
		Critter c(kinectProjector, randomLocationInROI(), playArea);
		c.setup();
		critters.push_back(c);
	}
}

void CCritterController::addTangible() {
	if (playArea.width <= 0)
		return;

	tangibles.push_back(Tangible(kinectProjector, randomLocationInROI(), playArea));
}

void CCritterController::update()
{
	if (!kinectProjector->isImageStabilized())
		return;

	handField.update();

	bool puckPresent = puckTracker && puckTracker->isPuckPresent();
	ofPoint puckLocation = puckPresent ? puckTracker->getPuckLocation() : ofPoint();
	float puckRadius = puckPresent ? puckTracker->getRadiusKinectPixels() : 0.0f;

	for (auto & c : critters) {
		bool insideRing = sonicWaveController && sonicWaveController->isInsideAnyRing(c.getLocation());
		c.update(handField, tangibles, puckPresent, puckLocation, puckRadius, insideRing);
	}

	for (auto & t : tangibles)
		t.update(handField);

	fbo.begin();
	ofClear(255, 255, 255, 0);

	for (auto & t : tangibles)
		t.draw();
	for (auto & c : critters)
		c.draw();

	if (puckPresent) {
		// Visual confirmation that the system is treating the puck as a
		// recognized object, not just invisibly colliding with it - same
		// look as Tangible::draw() so it reads as "the same kind of thing."
		ofVec2f projCoord = kinectProjector->kinectCoordToProjCoord(puckLocation.x, puckLocation.y);
		ofPushStyle();
		ofNoFill();
		ofSetColor(80, 255, 140);
		ofSetLineWidth(2.5f);
		ofDrawCircle(projCoord.x, projCoord.y, puckRadius);
		ofPopStyle();
	}

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
	bool puckPresent = puckTracker && puckTracker->isPuckPresent();
	if (puckPresent)
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.55f, 1.0f), "Puck: DETECTED - treated as a solid obstacle (radius %.1f px)",
			puckTracker->getRadiusKinectPixels());
	else
		ImGui::Text("Puck: not detected (tune detection in the Sonic Wave panel)");

	if (ImGui::ColorEdit3("Body color", bodyColorRGB)) {
		Critter::BODY_COLOR = ofColor(bodyColorRGB[0] * 255.0f, bodyColorRGB[1] * 255.0f, bodyColorRGB[2] * 255.0f);
	}
	ImGui::SliderFloat("Gravity", &Critter::GRAVITY, 0.0f, 0.3f);
	ImGui::SliderFloat("Damping", &Critter::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Sleep speed", &Critter::SLEEP_SPEED, 0.0f, 0.5f);
	ImGui::SliderInt("Sleep frames", &Critter::SLEEP_FRAME_THRESHOLD, 1, 120);
	ImGui::SliderFloat("Hand push (still hand)", &Critter::HAND_PUSH_STRENGTH, 0.0f, 10.0f);
	ImGui::SliderFloat("Herd strength (moving hand)", &Critter::HERD_STRENGTH, 0.0f, 1.0f);
	if (ImGui::Checkbox("Flip gradient sign", &gradientFlipped))
		Critter::GRADIENT_SIGN = gradientFlipped ? -1.0f : 1.0f;

	ImGui::Separator();
	ImGui::SliderFloat("Wander strength", &Critter::WANDER_STRENGTH, 0.0f, 0.2f);
	ImGui::SliderFloat("Wander turn rate", &Critter::WANDER_TURN_RATE, 0.0f, 1.0f);
	ImGui::SliderFloat("Wander slope falloff", &Critter::WANDER_SLOPE_FALLOFF, 0.0f, 50.0f);

	ImGui::Separator();
	ImGui::SliderFloat("Hand threshold", &HandField::THRESHOLD, 1.0f, 100.0f);
	ImGui::SliderFloat("Hand influence radius", &HandField::INFLUENCE_RADIUS, 10.0f, 300.0f);
	ImGui::SliderFloat("Hand still threshold", &HandField::STILL_THRESHOLD, 0.1f, 10.0f);
	ImGui::SliderFloat("Hand velocity smoothing", &HandField::VELOCITY_SMOOTHING, 0.05f, 1.0f);
	ImGui::Checkbox("Show hand mask", &showHandDebug);

	ImGui::Separator();
	ImGui::SliderFloat("Tangible damping", &Tangible::DAMPING, 0.5f, 0.99f);
	ImGui::SliderFloat("Tangible hand push", &Tangible::HAND_PUSH_STRENGTH, 0.0f, 10.0f);

	ImGui::SliderInt("Critters per spawn", &critterSpawnCount, 1, 20);
	if (ImGui::Button("Add critters"))
		addCritters(critterSpawnCount);
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
