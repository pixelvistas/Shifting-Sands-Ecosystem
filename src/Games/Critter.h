/***********************************************************************
Critter.h - ELF's deer (BDdeer/BDenvironment.stepDeer()), ported directly
rather than adapted onto this fork's earlier slope-gravity/hand-interaction
creature. That earlier version (forked from vehicle.h's boid steering, its
own thing predating this ELF comparison) has been removed entirely -
gravity, wander, HandField interaction, Tangible/puck collision, and the
sonic-ring color tint are all gone, because none of that exists in ELF.
The plan is to recreate ELF's actual structure first; any of that earlier
behavior can come back later as a deliberate addition, not as inherited
baggage.

Movement is ELF's grid-stepping-toward-a-random-destination, not
continuous physics: a deer holds an integer grid cell (gx, gy) into the
same per-kinect-pixel grid VegetationField uses, picks a random
destination cell, and each update() steps at most one cell closer to it
along each axis independently - see stepMovement(), a direct port of
stepDeer()'s movement block including its permissive OR check (move if
EITHER the immediate step OR the final destination is clear of snow/
water, not requiring both). One deviation: ELF uses "destX > 0" as its
own "no destination yet" sentinel (a minor quirk, since a legitimately
picked destX of exactly 0 gets misread the same way) - this uses an
explicit hasDestination flag instead, which changes nothing about the
visible walk-to-a-random-point-and-repick behavior. All water/snow/
grid-size queries go through VegetationField, so this class needs no
KinectProjector reference of its own at all.

Lifecycle is BDdeer's food economy: eats shrub-or-fruit at its own cell
(VegetationField::eatShrubOrFruit(), same preference order as
stepDeer()), drains food by a flat amount every update() call, dies at 0
and freezes in place as a dark corpse for LIFE_OF_CORPSE ticks before the
controller removes it, and has a flat percent chance per tick to request
a spawn while at max food. These constants (START_FOOD, MAX_FOOD,
FOOD_DRAIN_PER_TICK, LIFE_OF_CORPSE, SPAWN_CHANCE_PER_TICK) are ELF's own
literal magnitudes: one update() call is being treated as one ELF tick,
so unlike the elevation bands (a genuine unit mismatch that needed
rescaling) there's nothing to convert here, only a tick-rate difference
(this runs once per rendered frame; ELF's ran once per processed Kinect
frame) that's left as an acknowledged, GUI-tunable approximation.

Drawn as a small flat square with no heading indicator, matching
BDframe's fillRect(x*CELLWIDTH, y*CELLHEIGHT, CELLWIDTH, CELLHEIGHT) -
color is BODY_COLOR (cyan, matching BDdeer.LIVE) while alive, DEAD_COLOR
(matching BDdeer.DEAD = Color.DARK_GRAY) once dead. draw() takes the
already-computed projector coordinate rather than holding a
KinectProjector itself - the controller computes it from getGX()/getGY()
the same way for both Critter and HumanAgent.

The controller (not Critter) owns the population vector and is
responsible for collecting consumeSpawnRequest() results into new
Critters and erasing ones where isDeadForRemoval() is true, mirroring
BDenvironment.stepDeer()'s newdeerlist/it.remove() pattern.

Ecosystem extension, part of the Shifting Sands fork of Magic Sand.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "VegetationField.h"

class Critter {
public:
	Critter(int startGX, int startGY);

	void update(VegetationField & vegetationField);
	void draw(ofVec2f const& projCoord) const;

	int getGX() const { return gx; }
	int getGY() const { return gy; }
	bool isDead() const { return dead; }
	// Matches BDdeer.makeDead() - used by a successful HumanAgent hunt.
	void makeDead() { dead = true; food = 0.0f; }

	// Lifecycle - see the header note. The controller polls these after
	// update() to do the vector bookkeeping ELF does with
	// newdeerlist/it.remove() in BDenvironment.stepDeer().
	bool isDeadForRemoval() const { return dead && deadTimer > LIFE_OF_CORPSE; }
	bool consumeSpawnRequest() { bool s = pendingSpawn; pendingSpawn = false; return s; }

	// One update() call == one ELF tick - see the header note on why
	// these are literal ELF magnitudes, not rescaled.
	static float START_FOOD;
	static float MAX_FOOD;
	static float FOOD_DRAIN_PER_TICK;
	static float LIFE_OF_CORPSE;          // ticks a corpse lingers before the controller removes it
	static float SPAWN_CHANCE_PER_TICK;   // percent chance per tick to request a spawn while at MAX_FOOD
	static ofColor BODY_COLOR;            // cyan, matching BDdeer.LIVE
	static ofColor DEAD_COLOR;            // matching BDdeer.DEAD = Color.DARK_GRAY
	static float DOT_SIZE;                // projector pixels - visual only, ELF's literal 4px doesn't translate across projector resolutions

private:
	void stepMovement(VegetationField & vegetationField);
	void pickNewDestination(int cols, int rows);
	static ofPoint cellCenterKinectCoord(int cellGX, int cellGY, VegetationField & vegetationField);

	int gx, gy;             // ELF's myX/myY
	int destGX, destGY;     // ELF's destX/destY
	bool hasDestination;    // see header note replacing ELF's destX>0 sentinel

	float food;
	bool dead;
	float deadTimer;
	bool pendingSpawn;
};
