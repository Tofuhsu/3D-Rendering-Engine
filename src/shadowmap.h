#ifndef SHADOWMAP_H
#define SHADOWMAP_H

#include "headers.h"
#include "shaderprog.h"

// Shadow map for the directional light: a depth texture rendered from the light
// with an orthographic projection fitted to a bounding sphere, sampled with
// hardware depth comparison (sampler2DShadow) and an N x N PCF kernel.
class ShadowMap
{
public:
	explicit ShadowMap(int size);
	~ShadowMap();

	// Set up the light camera for a scene inside the sphere (center, radius) and
	// bind the shadow framebuffer. Draw casters with GetDepthShader(), then End().
	void Begin(const glm::vec3& lightDirection, const glm::vec3& center, float radius);
	void End(int viewportWidth, int viewportHeight);

	ShaderProg& GetDepthShader() { return depthShader; }
	const glm::mat4& GetLightViewProj() const { return lightViewProj; }
	GLuint GetTexture() const { return depthTexture; }
	int GetSize() const { return size; }
	// World-space size of one shadow-map texel (used for the normal-offset bias).
	float GetTexelWorldSize() const { return texelWorldSize; }

private:
	int size;
	GLuint fbo;
	GLuint depthTexture;
	glm::mat4 lightViewProj;
	float texelWorldSize;
	ShaderProg depthShader;
};

#endif
