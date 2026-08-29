/***********************************************************************
HydrologyParticle.h - a single flowing-water particle: advected downhill
by the sand's own slope, jittered so a stream braids instead of reading
as one deterministic thread, and deflected around hands. This is
ECOSIMSPEC.md build order step 1 ("gradient map + particle advection +
point rendering, no ecology") with step 3 (puck spawn) folded in, since
flow with nothing feeding it is invisible and untestable - see
HydrologyLayer.h for why those two are implemented together.

Deliberately shaped like Critter/SonicParticle (constructor takes the
spawn location and borders, setup() rolls per-particle randomness,
update() returns whether it's still alive, draw() renders it), not like
ECOSIMSPEC.md section 3's GPU ping-pong FBO particle textures. That GPU
design is sized for tens of thousands of particles and needs a compute-
style advect/deposit/cull shader pipeline; nothing in this fork does that
anywhere; every other "particle" here (Critter, Tangible, SonicParticle)
is a plain CPU object in a std::vector; and this is a hand-testable
mechanics pass, not a performance-critical one yet. Revisit the GPU
version only if profiling on real hardware says the CPU vector can't
keep up at the particle counts this installation actually uses - the
spec's own §3.1 note ("256^2 = 65,536 is the realistic ceiling") is a
ceiling for tens of thousands of simultaneously-visible particles, which
a slow, short-lived trickle of stream particles is nowhere near.

Two forces are reused rather than reimplemented, matching how every
other agent in src/Games queries the same shared sensing:
- Steepest-descent direction is KinectProjector::gradientAtKinectCoord(),
  the same field Critter's gravity already uses - and this particle
  reuses Critter::GRADIENT_SIGN directly rather than adding a second,
  separately-tuned copy of the same flag. Both are "which way is
  downhill" on the same physical rig with the same mount and
  calibration; it is one empirical fact, not two, so there is one knob
  for it (see Critter.h's header note on why that flag exists at all).
- Obstacle deflection is HandField::isInHand()/pushDirection(), the same
  hard push-out Critter falls back to under a held-still hand. Water
  doesn't need Critter's separate "moving hand herds it" nuance (that
  distinction exists for the trapping behavior of a living thing, not a
  fluid), so this always deflects on contact rather than only when the
  hand has stopped moving.
- Braiding jitter is a persistent per-particle wander heading nudged by a
  small random turn each frame (Critter::wanderAngle/vehicle.cpp's
  wandertheta idiom), not ECOSIMSPEC.md §5.2's per-frame noise2D(p.xy, t)
  sample. Nothing in this codebase calls ofNoise/ofSignedNoise anywhere,
  while the persistent-heading approach is already implemented, tuned,
  and explicitly documented (see Critter.h) as the fix for a real
  artifact - resampling a fresh random direction every frame reads as
  agents spinning in place. Reusing a working, in-repo pattern beats
  porting a GPU-shader-shaped formula verbatim into CPU code nobody here
  has run yet.

A third departure from §5.2, alongside the two above: integration is
per-frame (velocity += forces; location += velocity;), not scaled by dt -
see the note at the top of update() in the .cpp. Matches Critter exactly;
the spec's continuous-time formula is written for a GPU shader ticking at
a fixed simulation rate, which this single-window 60fps CPU loop already
effectively is.

Ecosystem module, part of the Shifting Sands fork of Magic Sand. See
ECOSIMSPEC.md §5.2 for the general force composition this is based on
(gravity + jitter + obstacle) and §9 for the build order.
***********************************************************************/

#pragma once
#include "ofMain.h"

#include "../KinectProjector/KinectProjector.h"
#include "../Games/HandField.h"

class HydrologyParticle {
public:
	HydrologyParticle(std::shared_ptr<KinectProjector> const& k, ofPoint slocation, ofRectangle sborders);

	void setup();
	// Returns false once the particle should be removed - either it aged
	// out (LIFETIME_MIN..MAX, randomized per particle so a spawned burst
	// doesn't blink out in unison) or it drifted outside borders (drained
	// off the tracked play area). No moisture/ET death yet - that needs
	// texWater/field capacity, which is build order step 2, not this one.
	bool update(HandField & handField);
	void draw();

	const ofPoint & getLocation() const { return location; }
	const ofVec2f & getVelocity() const { return velocity; }
	float getAge() const { return age; }
	float getLifetime() const { return lifetime; }

	// Physics tuning. Loosely modeled on ECOSIMSPEC.md §6 "Hydrology
	// (wall-clock)" (w_grav/w_jitter/w_obs/drag_base/v_max), but NOT dt-
	// scaled - see the per-frame integration note at the top of update()
	// in the .cpp. These are frame-based, same convention as Critter's
	// GRAVITY/DAMPING, and are NOT directly comparable to the spec's
	// per-second values.
	static float GRAVITY_WEIGHT;   // per-frame multiplier on the raw gradient (cf. Critter::GRAVITY = 0.05)
	static float JITTER_WEIGHT;    // per-frame multiplier on the wander heading (cf. Critter::WANDER_STRENGTH = 0.03)
	static float JITTER_TURN_RATE; // radians/frame the wander heading can turn - see .cpp note on why this is a
	                                // persistent wandering heading (Critter/vehicle's idiom)
	                                // rather than ECOSIMSPEC.md §5.2's per-frame noise2D sample
	static float OBSTACLE_WEIGHT;  // per-frame multiplier on hand push-out
	static float DRAG;             // multiplicative per-frame damping, 0..1 (cf. Critter::DAMPING = 0.92)
	static float MAX_SPEED;        // kinect px/frame, not px/s - see update()'s per-frame note

	static float LIFETIME_MIN, LIFETIME_MAX; // seconds - age/lifetime still use real dt, see update()

	// Rendering - see ECOSIMSPEC.md §5.11.4 ("Flow"): a short streak along
	// the velocity direction, not a round dot, so direction reads without
	// a separate vector-field overlay. Tail = location - velocity *
	// STREAK_LEN; since velocity is now kinect px/frame (see above),
	// STREAK_LEN is a small frame count, not a time constant - how many
	// frames of recent travel the tail reaches back over.
	static float STREAK_LEN;
	static float STREAK_WIDTH;
	static ofColor STREAK_COLOR;

private:
	std::shared_ptr<KinectProjector> kinectProjector;

	ofPoint location;
	ofVec2f velocity;
	ofVec2f projectorCoord;

	// Persistent wander heading, same idiom as Critter::wanderAngle -
	// see the .cpp note.
	float wanderAngle;

	float age;
	float lifetime;

	ofRectangle borders;
};
