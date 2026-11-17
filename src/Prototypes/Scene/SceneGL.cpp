#include "SceneGL.h"

void SceneGL::allocate(ofFbo & fbo, int w, int h, GLint internalFormat, GLint filter)
{
	ofFboSettings s;
	s.width = std::max(1, w);
	s.height = std::max(1, h);
	s.internalformat = internalFormat;
	s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
	s.minFilter = filter;
	s.maxFilter = filter;
	s.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
	s.wrapModeVertical = GL_CLAMP_TO_EDGE;
	s.useDepth = false;
	fbo.allocate(s);
	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

ofMesh SceneGL::quad(float w, float h)
{
	ofMesh q;
	q.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
	q.addVertex(glm::vec3(0, 0, 0)); q.addTexCoord(glm::vec2(0, 0));
	q.addVertex(glm::vec3(w, 0, 0)); q.addTexCoord(glm::vec2(w, 0));
	q.addVertex(glm::vec3(0, h, 0)); q.addTexCoord(glm::vec2(0, h));
	q.addVertex(glm::vec3(w, h, 0)); q.addTexCoord(glm::vec2(w, h));
	return q;
}

bool SceneGL::load(ofShader & shader, const std::string & frag)
{
#ifdef TARGET_OPENGLES
	return false;
#else
	const std::string dir = ofIsGLProgrammableRenderer() ? "shaders/scene/GL3/" : "shaders/scene/GL2/";
	const bool ok = shader.load(dir + "scene.vert", dir + frag + ".frag");
	if (!ok)
		ofLogError("Scene") << "could not load " << dir << frag << ".frag";
	return ok;
#endif
}

void SceneGL::GpuTimer::begin()
{
	if (!checked) {
		checked = true;
		supported = glewIsSupported("GL_ARB_timer_query") || glewIsSupported("GL_VERSION_3_3");
		if (supported)
			glGenQueries(2, ids);
	}
	if (!supported)
		return;
	// Collect the other query if its result is in (never wait for it).
	const int other = 1 - cur;
	if (pending[other]) {
		GLint available = 0;
		glGetQueryObjectiv(ids[other], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available) {
			GLuint64 ns = 0;
			glGetQueryObjectui64v(ids[other], GL_QUERY_RESULT, &ns);
			lastMs = (float)(ns / 1.0e6);
			pending[other] = false;
		}
	}
	if (pending[cur])
		return; // still in flight: skip this measurement
	glBeginQuery(GL_TIME_ELAPSED, ids[cur]);
}

void SceneGL::GpuTimer::end()
{
	if (!supported || pending[cur])
		return;
	glEndQuery(GL_TIME_ELAPSED);
	pending[cur] = true;
	cur = 1 - cur;
}
