/***********************************************************************
HumanAgent.h - ELF's human agents (BDagent/BDenvironment.stepAgents()),
new to this fork - there is no equivalent creature in the earlier
slope-gravity/hand-interaction system Critter.h used to be. Ported the
same way Critter now is: grid-stepping movement on VegetationField's
per-kinect-pixel grid, no continuous physics.

Movement mirrors stepAgents() exactly, including its "wait" stutter-step:
if the immediate step cell is blocked (snow/water) but the final
destination is not, the agent waits one tick, then forces through on the
next - see stepMovement(). This differs from Critter/BDdeer's more
permissive "move if EITHER cell is clear" check; ELF's own stepDeer() and
stepAgents() genuinely use different movement rules, not just different
economies, so this keeps them separately implemented rather than sharing
one movement function - matching how ELF itself never factors this logic
out either.

Economy is BDagent's: gathers fruit or nuts at its own cell (preferring
whichever it's taken less of so far, falling back to the other - see
VegetationField::eatFruitOrNut()), fishes automatically while standing in
water (a per-tick chance equal to its own fishing skill, which always
increases by 1 per attempt whether or not it succeeds), and hunts any
co-located, still-alive Critter (deer) - exact grid-cell equality is
meaningful here since both species share the identical grid, unlike a
continuous-position system where "co-located" would need a distance
threshold. A successful hunt kills the deer and fills food to max;
hunting skill rises by 5 per attempt regardless of success, matching
stepAgents() exactly. reduceExp() ports the skill decay: fishing/hunting
each have a small per-tick chance to drop once above 10, and gathering
counts (fruittaken/nutstaken) are bled down by 10/tick once their
combined getGathering() exceeds 10 - this is ELF's own literal formula
(fruittaken + nutstaken/100, integer division included) even though it
reads like it may have been intended as (fruittaken+nutstaken)/100; it's
reproduced as written for behavioral fidelity, not "fixed."

Color is computed fresh each frame from whichever skill currently
dominates (BDagent.getAgentCol()): fishing winning shows blue, hunting
red, otherwise (including a fresh spawn with no skill lead) yellow -
agents are not born into a role, they drift into whichever one their
recent luck favors.

One update() call == one ELF tick, same as Critter - see that file's
header note for why the food/skill magnitudes below are literal ELF
values rather than rescaled.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"
#include <vector>

#include "VegetationField.h"
#include "Critter.h"

class HumanAgent {
public:
	HumanAgent(int startGX, int startGY);

	// deer is hunted from directly (BDdeer.makeDead() equivalent) - matches
	// stepAgents()'s "for (BDdeer huntdeer : deerlist)" loop exactly, since
	// both species share VegetationField's grid.
	void update(VegetationField & vegetationField, std::vector<Critter> & deer);
	void draw(ofVec2f const& projCoord) const;

	int getGX() const { return gx; }
	int getGY() const { return gy; }

	bool isDeadForRemoval() const { return dead && deadTimer > LIFE_OF_CORPSE; }
	bool consumeSpawnRequest() { bool s = pendingSpawn; pendingSpawn = false; return s; }

	static float START_FOOD;
	static float FOOD_DRAIN_PER_TICK;
	static float LIFE_OF_CORPSE;
	static float SPAWN_CHANCE_PER_TICK;
	static float MAX_FOOD;                // a successful fish/hunt sets food straight to this, matching setFood(MAXFOOD)
	static ofColor FISHER_COLOR;          // matches BDagent.FISHER = Color.BLUE
	static ofColor HUNTER_COLOR;          // matches BDagent.HUNTER = Color.RED
	static ofColor GATHERER_COLOR;        // matches BDagent.GATHERER = Color.YELLOW
	static ofColor DEAD_COLOR;            // matches BDagent.DEAD = Color.BLACK
	static float DOT_SIZE;

private:
	void stepMovement(VegetationField & vegetationField);
	void pickNewDestination(int cols, int rows);
	void reduceExp();
	int getGathering() const; // BDagent.getGathering(), literal formula
	ofColor currentColor() const;
	static ofPoint cellCenterKinectCoord(int cellGX, int cellGY, VegetationField & vegetationField);

	int gx, gy;
	int destGX, destGY;
	bool hasDestination;
	bool waiting; // BDagent's wait toggle - see stepMovement()

	float food;
	int fishing;   // 1-100
	int hunting;   // 1-100
	int fruittaken, nutstaken;

	bool dead;
	float deadTimer;
	bool pendingSpawn;
};
