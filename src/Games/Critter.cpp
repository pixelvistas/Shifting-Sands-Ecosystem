#include "Critter.h"

float Critter::START_FOOD = 50.0f;
float Critter::MAX_FOOD = 255.0f;
float Critter::FOOD_DRAIN_PER_TICK = 1.0f;
float Critter::LIFE_OF_CORPSE = 500.0f;
float Critter::SPAWN_CHANCE_PER_TICK = 1.0f;
ofColor Critter::BODY_COLOR = ofColor(0, 255, 255);   // cyan, matching BDdeer.LIVE
ofColor Critter::DEAD_COLOR = ofColor(64, 64, 64);    // matching java.awt.Color.DARK_GRAY
float Critter::DOT_SIZE = 8.0f;

Critter::Critter(int startGX, int startGY)
{
	gx = startGX;
	gy = startGY;
	destGX = destGY = 0;
	hasDestination = false;
	food = START_FOOD;
	dead = false;
	deadTimer = 0.0f;
	pendingSpawn = false;
}

ofPoint Critter::cellCenterKinectCoord(int cellGX, int cellGY, VegetationField & vegetationField)
{
	ofVec2f origin = vegetationField.getGridOrigin();
	float step = vegetationField.getGridStep();
	return ofPoint(origin.x + cellGX * step + step / 2.0f, origin.y + cellGY * step + step / 2.0f);
}

void Critter::pickNewDestination(int cols, int rows)
{
	destGX = (int)ofRandom((float)cols);
	destGY = (int)ofRandom((float)rows);
	hasDestination = true;
}

void Critter::stepMovement(VegetationField & vegetationField)
{
	int cols = vegetationField.getCols();
	int rows = vegetationField.getRows();
	if (cols <= 0 || rows <= 0)
		return;

	if (hasDestination) {
		int targetGX = (gx > destGX) ? gx - 1 : (gx < destGX ? gx + 1 : gx);
		int targetGY = (gy > destGY) ? gy - 1 : (gy < destGY ? gy + 1 : gy);

		ofPoint targetKinect = cellCenterKinectCoord(targetGX, targetGY, vegetationField);
		ofPoint destKinect = cellCenterKinectCoord(destGX, destGY, vegetationField);
		bool targetBlocked = vegetationField.isWaterAt(targetKinect.x, targetKinect.y) || vegetationField.isSnowAt(targetKinect.x, targetKinect.y);
		bool destBlocked = vegetationField.isWaterAt(destKinect.x, destKinect.y) || vegetationField.isSnowAt(destKinect.x, destKinect.y);

		// stepDeer(): moves if EITHER the immediate step or the final
		// destination is clear - a permissive OR, not "both must be clear."
		if (!targetBlocked || !destBlocked) {
			gx = targetGX;
			gy = targetGY;
		} else {
			pickNewDestination(cols, rows);
		}
	} else {
		pickNewDestination(cols, rows);
	}

	if (gx == destGX && gy == destGY)
		pickNewDestination(cols, rows);
}

void Critter::update(VegetationField & vegetationField)
{
	if (dead) {
		// stepDeer()'s dead branch: only the removal timer advances.
		deadTimer += 1.0f;
		return;
	}

	stepMovement(vegetationField);

	// Eat shrub-or-fruit at the current cell - matches stepDeer()'s
	// "if (food < MAXFOOD) { eat }" guard.
	if (food < MAX_FOOD) {
		ofPoint here = cellCenterKinectCoord(gx, gy, vegetationField);
		float gained = vegetationField.eatShrubOrFruit(here.x, here.y);
		food = std::min(MAX_FOOD, food + gained);
	}

	// Spawn - matches stepDeer()'s "if (food==MAXFOOD) roll DEERSPAWNCHANCE".
	// Population cap/spawn placement is the controller's job.
	if (food >= MAX_FOOD && ofRandom(100.0f) < SPAWN_CHANCE_PER_TICK)
		pendingSpawn = true;

	food -= FOOD_DRAIN_PER_TICK;
	if (food <= 0.0f) {
		food = 0.0f;
		dead = true;
	}
}

void Critter::draw(ofVec2f const& projCoord) const
{
	ofPushStyle();
	ofSetColor(dead ? DEAD_COLOR : BODY_COLOR);
	ofFill();
	// Flat square, no heading indicator - matches BDframe's
	// fillRect(x*CELLWIDTH, y*CELLHEIGHT, CELLWIDTH, CELLHEIGHT).
	ofDrawRectangle(projCoord.x - DOT_SIZE / 2.0f, projCoord.y - DOT_SIZE / 2.0f, DOT_SIZE, DOT_SIZE);
	ofPopStyle();
}
