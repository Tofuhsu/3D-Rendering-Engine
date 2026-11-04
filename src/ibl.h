#ifndef IBL_H
#define IBL_H

#include "headers.h"
#include "shaderprog.h"
#include "camera.h"

// Image-based lighting from one equirectangular HDR image. At start-up the
// image is turned into an environment cubemap, a diffuse irradiance cubemap,
// a pre-filtered specular cubemap (one roughness per mip) and a BRDF lookup
// table, all with fragment-shader passes (OpenGL 4.1 has no compute shaders).
class EnvironmentIBL
{
public:
	struct Settings
	{
		int environmentSize = 512;	// cubemap face size
		int irradianceSize = 32;
		int prefilterSize = 128;
		int prefilterMips = 5;
		int prefilterSamples = 1024;
		int brdfLutSize = 512;
	};

	// Wall-clock milliseconds for each precompute step (glFinish after each).
	struct Timings
	{
		double loadHdrMs = 0.0, toCubemapMs = 0.0, irradianceMs = 0.0,
			   prefilterMs = 0.0, brdfLutMs = 0.0;
		double Total() const { return loadHdrMs + toCubemapMs + irradianceMs + prefilterMs + brdfLutMs; }
	};

	EnvironmentIBL();
	~EnvironmentIBL();

	bool Build(const std::string& hdrPath, const Settings& settings);
	void RenderSkybox(Camera* camera, float exposure);

	GLuint GetIrradianceMap() const { return irradianceMap; }
	GLuint GetPrefilterMap() const { return prefilterMap; }
	GLuint GetBrdfLut() const { return brdfLut; }
	float GetPrefilterMaxLod() const { return (float)(settings.prefilterMips - 1); }
	const Timings& GetTimings() const { return timings; }
	int GetSkyboxTriangles() const { return 12; }

private:
	void DrawCube();
	GLuint CreateCubemap(int size, bool mipmapped);
	// Render `program` into all six faces of `cubemap` at the given mip level.
	void RenderToCubemap(ShaderProg& program, GLuint cubemap, int size, int mip);

	Settings settings;
	Timings timings;

	GLuint cubeVao, cubeVbo, emptyVao;
	GLuint captureFbo, captureRbo;
	GLuint equirectTexture;
	GLuint environmentMap, irradianceMap, prefilterMap, brdfLut;

	ShaderProg equirectToCube, irradiance, prefilter, brdf, skybox;
};

#endif
