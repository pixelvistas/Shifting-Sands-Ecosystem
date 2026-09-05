#include "CritterController.h"
#include "ofxImGui.h"
#include <algorithm>

void CCritterController::setup(std::shared_ptr<KinectProjector> const& k, VegetationField* v)
{
	kinectProjector = k;
	vegetationField = v;
	deerSpawnCount = 5;
	humanSpawnCount = 5;
	maxDeerPopulation = 60;
	maxHumanPopulation = 60;
	deerColorRGB[0] = Critter::BODY_COLOR.r / 255.0f;
	deerColorRGB[1] = Critter::BODY_COLOR.g / 255.0f;
	deerColorRGB[2] = Critter::BODY_COLOR.b / 255.0f;
}

void CCritterController::setProjectorRes(ofVec2f & PR)
{
	projRes = PR;
	fbo.allocate(projRes.x, projRes.y, GL_RGBA);
}

void CCritterController::setKinectROI(ofRectangle & KROI)
{
	bool firstValidROI = (kinectROI.width <= 0) && (KROI.width > 0);
	kinectROI = KROI;

	if (firstValidROI) {
		addDeer(deerSpawnCount);
		addHumans(humanSpawnCount);
	}
}

int CCritterController::randomCol() const
{
	return (vegetationField && vegetationField->getCols() > 0) ? (int)ofRandom((float)vegetationField->getCols()) : 0;
}

int CCritterController::randomRow() const
{
	return (vegetationField && vegetationField->getRows() > 0) ? (int)ofRandom((float)vegetationField->getRows()) : 0;
}

void CCritterController::addDeer(int n)
{
	if (!vegetationField || vegetationField->getCols() <= 0)
		return;
	for (int i = 0; i < n; i++)
		deer.push_back(Critter(randomCol(), randomRow()));
}

void CCritterController::addHumans(int n)
{
	if (!vegetationField || vegetationField->getCols() <= 0)
		return;
	for (int i = 0; i < n; i++)
		humans.push_back(HumanAgent(randomCol(), randomRow()));
}

ofPoint CCritterController::gridToProjCoord(int gx, int gy) const
{
	ofVec2f origin = vegetationField->getGridOrigin();
	float step = vegetationField->getGridStep();
	float kx = origin.x + gx * step + step / 2.0f;
	float ky = origin.y + gy * step + step / 2.0f;
	return kinectProjector->kinectCoordToProjCoord(kx, ky);
}

void CCritterController::update()
{
	if (!kinectProjector->isImageStabilized() || !vegetationField)
		return;

	// Deer - mirrors BDenvironment.stepDeer()'s newdeerlist/it.remove() pattern.
	std::vector<Critter> newDeer;
	for (auto & d : deer) {
		d.update(*vegetationField);
		if (d.consumeSpawnRequest() && (int)(deer.size() + newDeer.size()) < maxDeerPopulation)
			newDeer.push_back(Critter(d.getGX(), d.getGY()));
	}
	deer.insert(deer.end(), newDeer.begin(), newDeer.end());
	deer.erase(std::remove_if(deer.begin(), deer.end(), [](Critter const& d) { return d.isDeadForRemoval(); }), deer.end());

	// Humans - mirrors stepAgents()'s equivalent pattern. Runs after the
	// deer update above so a hunt this tick sees each deer's fresh
	// position, matching BDframe.setPixels()'s step() calling stepDeer()
	// before stepAgents().
	std::vector<HumanAgent> newHumans;
	for (auto & h : humans) {
		h.update(*vegetationField, deer);
		if (h.consumeSpawnRequest() && (int)(humans.size() + newHumans.size()) < maxHumanPopulation)
			newHumans.push_back(HumanAgent(h.getGX(), h.getGY()));
	}
	humans.insert(humans.end(), newHumans.begin(), newHumans.end());
	humans.erase(std::remove_if(humans.begin(), humans.end(), [](HumanAgent const& h) { return h.isDeadForRemoval(); }), humans.end());

	fbo.begin();
	ofClear(255, 255, 255, 0);
	for (auto & d : deer)
		d.draw(gridToProjCoord(d.getGX(), d.getGY()));
	for (auto & h : humans)
		h.draw(gridToProjCoord(h.getGX(), h.getGY()));
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
	ImGui::Begin("Population");
	ImGui::Text("%d deer, %d humans", (int)deer.size(), (int)humans.size());

	if (ImGui::ColorEdit3("Deer color", deerColorRGB))
		Critter::BODY_COLOR = ofColor(deerColorRGB[0] * 255.0f, deerColorRGB[1] * 255.0f, deerColorRGB[2] * 255.0f);

	ImGui::Separator();
	ImGui::Text("Deer (BDdeer)");
	ImGui::SliderFloat("Deer start food", &Critter::START_FOOD, 0.0f, 255.0f);
	ImGui::SliderFloat("Deer food drain/tick", &Critter::FOOD_DRAIN_PER_TICK, 0.0f, 5.0f);
	ImGui::SliderFloat("Deer corpse lifetime (ticks)", &Critter::LIFE_OF_CORPSE, 0.0f, 2000.0f);
	ImGui::SliderFloat("Deer spawn chance %/tick", &Critter::SPAWN_CHANCE_PER_TICK, 0.0f, 20.0f);
	ImGui::SliderFloat("Deer dot size", &Critter::DOT_SIZE, 2.0f, 30.0f);
	ImGui::SliderInt("Deer per spawn click", &deerSpawnCount, 1, 20);
	ImGui::SliderInt("Max deer population", &maxDeerPopulation, 1, 300);
	if (ImGui::Button("Add deer"))
		addDeer(deerSpawnCount);
	ImGui::SameLine();
	if (ImGui::Button("Remove all deer"))
		deer.clear();

	ImGui::Separator();
	ImGui::Text("Humans (BDagent)");
	ImGui::SliderFloat("Human start food", &HumanAgent::START_FOOD, 0.0f, 255.0f);
	ImGui::SliderFloat("Human food drain/tick", &HumanAgent::FOOD_DRAIN_PER_TICK, 0.0f, 5.0f);
	ImGui::SliderFloat("Human corpse lifetime (ticks)", &HumanAgent::LIFE_OF_CORPSE, 0.0f, 2000.0f);
	ImGui::SliderFloat("Human spawn chance %/tick", &HumanAgent::SPAWN_CHANCE_PER_TICK, 0.0f, 20.0f);
	ImGui::SliderFloat("Human dot size", &HumanAgent::DOT_SIZE, 2.0f, 30.0f);
	ImGui::SliderInt("Humans per spawn click", &humanSpawnCount, 1, 20);
	ImGui::SliderInt("Max human population", &maxHumanPopulation, 1, 300);
	if (ImGui::Button("Add humans"))
		addHumans(humanSpawnCount);
	ImGui::SameLine();
	if (ImGui::Button("Remove all humans"))
		humans.clear();

	ImGui::Separator();
	ImGui::Text("Population caps are adapted for real-time rendering cost -");
	ImGui::Text("ELF's own MAXDEERS/MAXAGENTS=1000 assume no per-agent draw cost.");

	ImGui::End();
}
