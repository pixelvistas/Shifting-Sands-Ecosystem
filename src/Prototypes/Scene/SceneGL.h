/***********************************************************************
SceneGL - Prototype 4 ("Scene"): small helpers shared by the Scene
classes: float FBO allocation (rectangle textures, as every Prototype 3/4
shader samples in texel coordinates), a full-target quad whose texture
coordinates equal its pixel coordinates, shader loading from
bin/data/shaders/scene/GL2|GL3, and a GPU timer (GL_TIME_ELAPSED queries,
read back one frame later, so it never stalls) for the per-pass readout.
***********************************************************************/

#pragma once
#include "ofMain.h"

namespace SceneGL
{
	// filter: GL_NEAREST or GL_LINEAR.
	void allocate(ofFbo & fbo, int w, int h, GLint internalFormat, GLint filter);
	ofMesh quad(float w, float h);
	// Loads scene.vert + <frag>.frag for the active renderer; logs on failure.
	bool load(ofShader & shader, const std::string & frag);
	// Draws q into fbo with shader bound and blending off (the caller sets
	// uniforms between begin() and draw via the callback-free pattern:
	// fbo.begin(); shader.begin(); ...; drawQuad(); shader.end(); fbo.end()).

	struct GpuTimer {
		void begin();
		void end();
		float ms() const { return lastMs; } // most recent completed measurement
	private:
		unsigned int ids[2] = { 0, 0 };
		bool pending[2] = { false, false };
		int cur = 0;
		bool supported = true, checked = false;
		float lastMs = 0.0f;
	};
}
