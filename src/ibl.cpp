#include "ibl.h"

#include <algorithm>
#include <chrono>

#include <stb_image.h>

namespace {

double MsSince(std::chrono::steady_clock::time_point t0)
{
	glFinish();
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Unit cube, 36 vertices, counter-clockwise from outside (culling is off anyway).
const float kCubeVertices[] = {
	-1, -1, -1,  1,  1, -1,  1, -1, -1,   1,  1, -1, -1, -1, -1, -1,  1, -1,
	-1, -1,  1,  1, -1,  1,  1,  1,  1,   1,  1,  1, -1,  1,  1, -1, -1,  1,
	-1,  1,  1, -1,  1, -1, -1, -1, -1,  -1, -1, -1, -1, -1,  1, -1,  1,  1,
	 1,  1,  1,  1, -1, -1,  1,  1, -1,   1, -1, -1,  1,  1,  1,  1, -1,  1,
	-1, -1, -1,  1, -1, -1,  1, -1,  1,   1, -1,  1, -1, -1,  1, -1, -1, -1,
	-1,  1, -1,  1,  1,  1,  1,  1, -1,   1,  1,  1, -1,  1, -1, -1,  1,  1,
};

// Standard cubemap face orientation (+X, -X, +Y, -Y, +Z, -Z).
glm::mat4 FaceView(int face)
{
	static const glm::vec3 dirs[6] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
	static const glm::vec3 ups[6] = { {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0} };
	return glm::lookAt(glm::vec3(0.0f), dirs[face], ups[face]);
}

}

EnvironmentIBL::EnvironmentIBL()
	: cubeVao(0), cubeVbo(0), emptyVao(0), captureFbo(0), captureRbo(0), equirectTexture(0),
	  environmentMap(0), irradianceMap(0), prefilterMap(0), brdfLut(0)
{
}

EnvironmentIBL::~EnvironmentIBL()
{
	GLuint textures[] = { equirectTexture, environmentMap, irradianceMap, prefilterMap, brdfLut };
	glDeleteTextures(5, textures);
	glDeleteFramebuffers(1, &captureFbo);
	glDeleteRenderbuffers(1, &captureRbo);
	glDeleteBuffers(1, &cubeVbo);
	GLuint vaos[] = { cubeVao, emptyVao };
	glDeleteVertexArrays(2, vaos);
}

GLuint EnvironmentIBL::CreateCubemap(int size, bool mipmapped)
{
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
	for (int face = 0; face < 6; ++face)
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (mipmapped)
		glGenerateMipmap(GL_TEXTURE_CUBE_MAP);	// allocate the chain
	return tex;
}

void EnvironmentIBL::DrawCube()
{
	glBindVertexArray(cubeVao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

void EnvironmentIBL::RenderToCubemap(ShaderProg& program, GLuint cubemap, int size, int mip)
{
	const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
	glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRbo);
	glViewport(0, 0, size, size);
	program.Bind();
	for (int face = 0; face < 6; ++face) {
		glm::mat4 viewProj = proj * FaceView(face);
		glUniformMatrix4fv(program.Uniform("viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cubemap, mip);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		DrawCube();
	}
	program.UnBind();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool EnvironmentIBL::Build(const std::string& hdrPath, const Settings& s)
{
	settings = s;
	using Clock = std::chrono::steady_clock;

	if (!equirectToCube.LoadFromFiles("shaders/ibl_cube.vs", "shaders/ibl_equirect_to_cube.fs") ||
		!irradiance.LoadFromFiles("shaders/ibl_cube.vs", "shaders/ibl_irradiance.fs") ||
		!prefilter.LoadFromFiles("shaders/ibl_cube.vs", "shaders/ibl_prefilter.fs") ||
		!brdf.LoadFromFiles("shaders/ibl_brdf_lut.vs", "shaders/ibl_brdf_lut.fs") ||
		!skybox.LoadFromFiles("shaders/skybox_cube.vs", "shaders/skybox_cube.fs"))
		return false;

	glGenVertexArrays(1, &cubeVao);
	glBindVertexArray(cubeVao);
	glGenBuffers(1, &cubeVbo);
	glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
	glBindVertexArray(0);
	glGenVertexArrays(1, &emptyVao);
	glGenFramebuffers(1, &captureFbo);
	glGenRenderbuffers(1, &captureRbo);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	// 1. Load the HDR image as floating point, bottom row first.
	auto t = Clock::now();
	stbi_set_flip_vertically_on_load(1);
	int w = 0, h = 0, channels = 0;
	float* pixels = stbi_loadf(hdrPath.c_str(), &w, &h, &channels, 3);
	if (pixels == nullptr) {
		std::cerr << "[ERROR] Cannot load HDR image " << hdrPath << " (" << stbi_failure_reason() << ")" << std::endl;
		return false;
	}
	glGenTextures(1, &equirectTexture);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, pixels);
	stbi_image_free(pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	timings.loadHdrMs = MsSince(t);

	// 2. Equirectangular -> environment cubemap (+ mips for the later passes).
	t = Clock::now();
	environmentMap = CreateCubemap(settings.environmentSize, true);
	equirectToCube.Bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, equirectTexture);
	glUniform1i(equirectToCube.Uniform("equirectMap"), 0);
	RenderToCubemap(equirectToCube, environmentMap, settings.environmentSize, 0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
	timings.toCubemapMs = MsSince(t);

	// 3. Diffuse irradiance.
	t = Clock::now();
	irradianceMap = CreateCubemap(settings.irradianceSize, false);
	irradiance.Bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap);
	glUniform1i(irradiance.Uniform("environmentMap"), 0);
	RenderToCubemap(irradiance, irradianceMap, settings.irradianceSize, 0);
	timings.irradianceMs = MsSince(t);

	// 4. Specular pre-filter: roughness 0..1 across the mip chain.
	t = Clock::now();
	prefilterMap = CreateCubemap(settings.prefilterSize, true);
	prefilter.Bind();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap);
	glUniform1i(prefilter.Uniform("environmentMap"), 0);
	glUniform1f(prefilter.Uniform("environmentResolution"), (float)settings.environmentSize);
	glUniform1i(prefilter.Uniform("sampleCount"), settings.prefilterSamples);
	for (int mip = 0; mip < settings.prefilterMips; ++mip) {
		const int size = std::max(1, settings.prefilterSize >> mip);
		prefilter.Bind();
		glUniform1f(prefilter.Uniform("roughness"), (float)mip / (float)(settings.prefilterMips - 1));
		RenderToCubemap(prefilter, prefilterMap, size, mip);
	}
	glBindTexture(GL_TEXTURE_CUBE_MAP, prefilterMap);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, settings.prefilterMips - 1);
	timings.prefilterMs = MsSince(t);

	// 5. BRDF integration lookup table.
	t = Clock::now();
	glGenTextures(1, &brdfLut);
	glBindTexture(GL_TEXTURE_2D, brdfLut);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, settings.brdfLutSize, settings.brdfLutSize, 0, GL_RG, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLut, 0);
	glViewport(0, 0, settings.brdfLutSize, settings.brdfLutSize);
	glClear(GL_COLOR_BUFFER_BIT);
	brdf.Bind();
	glBindVertexArray(emptyVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	brdf.UnBind();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	timings.brdfLutMs = MsSince(t);

	// The source image is no longer needed.
	glDeleteTextures(1, &equirectTexture);
	equirectTexture = 0;
	return true;
}

void EnvironmentIBL::RenderSkybox(Camera* camera, float exposure)
{
	// Rotation-only view so the sky stays at infinity; depth is written as 1.0.
	glm::mat4 view = glm::mat4(glm::mat3(camera->GetViewMatrix()));
	glm::mat4 viewProj = camera->GetProjMatrix() * view;
	glDepthFunc(GL_LEQUAL);
	skybox.Bind();
	glUniformMatrix4fv(skybox.Uniform("viewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));
	glUniform1f(skybox.Uniform("exposure"), exposure);
	glUniform1i(skybox.Uniform("environmentMap"), 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, environmentMap);
	DrawCube();
	skybox.UnBind();
	glDepthFunc(GL_LESS);
}
