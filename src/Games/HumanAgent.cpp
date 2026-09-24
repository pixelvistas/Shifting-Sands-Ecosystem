#include "HumanAgent.h"

float HumanAgent::START_FOOD = 100.0f;
float HumanAgent::FOOD_DRAIN_PER_TICK = 1.0f;
float HumanAgent::LIFE_OF_CORPSE = 500.0f;
float HumanAgent::SPAWN_CHANCE_PER_TICK = 1.0f;
float HumanAgent::MAX_FOOD = 255.0f;
// 2026-09-24, per explicit user request: colorblind-accessible palette
// pass - see heightMapShader.frag's header note for the full rationale.
// Values below are bold/saturated rather than a muted reference-palette
// pick - an earlier version of this shifted gatherer only ~17% and
// fruit-vs-old-hunter only ~25% of the max possible RGB distance,
// confirmed too close to the originals on review. Separation is via the
// red/blue channels and lightness specifically (what survives
// deuteranopia - the green-sensing cone dropping out merges colors that
// only differ in "how green"), not hue alone.
// Fisher stays blue family, a deep indigo - low red/green, moderate
// blue, clearly darker than deer's bright cyan and the terrain shader's
// water color despite sharing the same family.
ofColor HumanAgent::FISHER_COLOR = ofColor(30, 30, 130);
// Hunter stays red/warm family, but NOT pure red: (255,0,0) was the
// EXACT same value as fruit's old peak color - confirmed as the direct
// cause of a real user's confusion this session ("is all this red
// hunting humans?"). A bold crimson instead - high red, low green, real
// blue component - separates from fruit's orange-red peak (high red,
// moderate green, near-zero blue) via green/blue, not just hue.
ofColor HumanAgent::HUNTER_COLOR = ofColor(200, 20, 90);
// Gatherer stays yellow family (also the default/fallback role - see
// currentColor() below) but a rich gold/amber rather than flat lemon
// yellow - full red, strong green, zero blue - clearly warmer/more
// saturated than the original (255,255,0) while staying unmistakably
// "yellow."
ofColor HumanAgent::GATHERER_COLOR = ofColor(255, 190, 0);
ofColor HumanAgent::DEAD_COLOR = ofColor(0, 0, 0);          // Color.BLACK - neutral, untouched
float HumanAgent::DOT_SIZE = 8.0f;

HumanAgent::HumanAgent(int startGX, int startGY)
{
	gx = startGX;
	gy = startGY;
	destGX = destGY = 0;
	hasDestination = false;
	waiting = false;
	food = START_FOOD;
	fishing = 1;
	hunting = 1;
	fruittaken = nutstaken = 0;
	dead = false;
	deadTimer = 0.0f;
	pendingSpawn = false;
}

ofPoint HumanAgent::cellCenterKinectCoord(int cellGX, int cellGY, VegetationField & vegetationField)
{
	ofVec2f origin = vegetationField.getGridOrigin();
	float step = vegetationField.getGridStep();
	return ofPoint(origin.x + cellGX * step + step / 2.0f, origin.y + cellGY * step + step / 2.0f);
}

void HumanAgent::pickNewDestination(int cols, int rows)
{
	destGX = (int)ofRandom((float)cols);
	destGY = (int)ofRandom((float)rows);
	hasDestination = true;
}

void HumanAgent::stepMovement(VegetationField & vegetationField)
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

		if (!targetBlocked) {
			gx = targetGX;
			gy = targetGY;
		} else if (destBlocked) {
			pickNewDestination(cols, rows);
		} else {
			// stepAgents()'s stutter-step: target blocked but the final
			// destination isn't - wait one tick, then force through.
			if (waiting) {
				gx = targetGX;
				gy = targetGY;
				waiting = false;
			} else {
				waiting = true;
			}
		}
	} else {
		pickNewDestination(cols, rows);
	}

	if (gx == destGX && gy == destGY)
		pickNewDestination(cols, rows);
}

int HumanAgent::getGathering() const
{
	// BDagent.getGathering(): literal formula, integer division included -
	// see the header note on why this isn't "corrected."
	int gathering = fruittaken + nutstaken / 100;
	if (gathering > 100)
		gathering = 100;
	return gathering;
}

ofColor HumanAgent::currentColor() const
{
	if (dead)
		return DEAD_COLOR;
	int gathering = getGathering();
	if (fishing > hunting && fishing > gathering)
		return FISHER_COLOR;
	if (hunting > fishing && hunting > gathering)
		return HUNTER_COLOR;
	return GATHERER_COLOR;
}

void HumanAgent::reduceExp()
{
	if (fishing > 10 && ofRandom(1.0f) < 0.3f)
		fishing--;
	if (hunting > 10 && ofRandom(1.0f) < 0.1f)
		hunting--;
	if (getGathering() > 10) {
		fruittaken -= 10;
		nutstaken -= 10;
	}
}

void HumanAgent::update(VegetationField & vegetationField, std::vector<Critter> & deer)
{
	if (dead) {
		deadTimer += 1.0f;
		return;
	}

	stepMovement(vegetationField);

	if (food < MAX_FOOD) {
		ofPoint here = cellCenterKinectCoord(gx, gy, vegetationField);

		// Gathering - see the header note on the fruittaken/nutstaken
		// preference chain this collapses from.
		bool preferNut = nutstaken < fruittaken;
		bool ateNut = false;
		float gained = vegetationField.eatFruitOrNut(here.x, here.y, preferNut, ateNut);
		if (gained > 0.0f) {
			// Intentional deviation from ELF: stepAgents() adds the eaten
			// amount to food with no cap, so a big fruit/nut tile can push
			// BDagent's food past MAXFOOD=255 in one bite (its constrainVal
			// helper exists but is never called on food). Clamping here is a
			// deliberate correction, not a literal reproduction.
			food = std::min(MAX_FOOD, food + gained);
			if (ateNut) nutstaken += (int)gained;
			else fruittaken += (int)gained;
		}

		// Fishing - stepAgents(): a per-tick chance equal to current skill,
		// which always increases by 1 regardless of success.
		if (vegetationField.isWaterAt(here.x, here.y)) {
			if (ofRandom(100.0f) < (float)fishing)
				food = MAX_FOOD;
			// Intentional deviation from ELF: BDagent.setFishing() has a
			// real bug - it writes the unclamped value straight to the
			// field, then clamps a local copy of the parameter that's
			// never written back, so the 1-100 clamp is dead code and
			// fishing skill is actually unbounded in real ELF. Clamping
			// here is a deliberate correction, not a literal reproduction.
			fishing = std::min(100, fishing + 1);
		}

		// Hunting - exact grid-cell equality, matching stepAgents()'s
		// getMyX()==huntdeer.getMyX() check (meaningful here since both
		// species share the same grid).
		for (auto & d : deer) {
			if (d.isDead())
				continue;
			if (d.getGX() == gx && d.getGY() == gy) {
				if (ofRandom(100.0f) < (float)hunting) {
					food = MAX_FOOD;
					d.makeDead();
				}
				// Same setHunting() clamp bug as fishing above - see that
				// comment. Clamped here deliberately, not literally.
				hunting = std::min(100, hunting + 5);
			}
		}
	}

	if (food >= MAX_FOOD && ofRandom(100.0f) < SPAWN_CHANCE_PER_TICK)
		pendingSpawn = true;

	food -= FOOD_DRAIN_PER_TICK;
	if (food <= 0.0f) {
		food = 0.0f;
		dead = true;
	}

	reduceExp();
}

void HumanAgent::draw(ofVec2f const& projCoord) const
{
	ofPushStyle();
	ofSetColor(currentColor());
	ofFill();
	ofDrawRectangle(projCoord.x - DOT_SIZE / 2.0f, projCoord.y - DOT_SIZE / 2.0f, DOT_SIZE, DOT_SIZE);
	ofPopStyle();
}
