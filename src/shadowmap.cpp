#include "shadowmap.h"

ShadowMap::ShadowMap(int mapSize)
	: size(mapSize), fbo(0), depthTexture(0), lightViewProj(1.0f), texelWorldSize(0.0f)
{
	glGenTextures(1, &depthTexture);
	glBindTexture(GL_TEXTURE_2D, depthTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	// Linear filtering + compare mode gives a bilinear 2x2 PCF per tap in hardware.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	// Outside the map counts as lit.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	const float border[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cerr << "[ERROR] Shadow framebuffer incomplete" << std::endl;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!depthShader.LoadFromFiles("shaders/shadow_depth.vs", "shaders/shadow_depth.fs"))
		std::cerr << "[ERROR] Cannot load the shadow depth shader" << std::endl;
}

ShadowMap::~ShadowMap()
{
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &depthTexture);
}

void ShadowMap::Begin(const glm::vec3& lightDirection, const glm::vec3& center, float radius)
{
	const glm::vec3 dir = glm::normalize(lightDirection);
	const glm::vec3 up = std::fabs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
	const glm::mat4 view = glm::lookAt(center - dir * (2.0f * radius), center, up);
	const glm::mat4 proj = glm::ortho(-radius, radius, -radius, radius, 0.5f * radius, 3.5f * radius);
	lightViewProj = proj * view;
	texelWorldSize = 2.0f * radius / (float)size;

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, size, size);
	glClear(GL_DEPTH_BUFFER_BIT);
	// Push depths back a little to reduce acne on surfaces facing the light.
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.0f, 4.0f);
	depthShader.Bind();
}

void ShadowMap::End(int viewportWidth, int viewportHeight)
{
	depthShader.UnBind();
	glDisable(GL_POLYGON_OFFSET_FILL);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, viewportWidth, viewportHeight);
}
