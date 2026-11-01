#ifdef _WIN32
#include <Windows.h>
#endif

#include "headers.h"
#include "trianglemesh.h"
#include "camera.h"
#include "shaderprog.h"
#include "light.h"
#include "imagetexture.h"
#include "skybox.h"
#include "profiler.h"
#include "ibl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>


using std::vector;
using namespace std;
GLuint vbo;
GLuint ibo;
GLuint vertexShader;
GLuint fragmentShader;


// Global variables.
GLFWwindow* window = nullptr;
bool reportLiveObjects = false;
int screenWidth = 600;
int screenHeight = 600;
// Triangle mesh.
TriangleMesh* mesh = nullptr;
// Lights.
DirectionalLight* dirLight = nullptr;
PointLight* pointLight = nullptr;
SpotLight* spotLight = nullptr;
glm::vec3 dirLightDirection = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 dirLightRadiance = glm::vec3(0.6f, 0.6f, 0.6f);
glm::vec3 pointLightPosition = glm::vec3(0.8f, 0.0f, 0.8f);
glm::vec3 pointLightIntensity = glm::vec3(0.5f, 0.1f, 0.1f);
glm::vec3 spotLightPosition = glm::vec3(0.0f, 1.0f, 0.0f);
glm::vec3 spotLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
glm::vec3 spotLightIntensity = glm::vec3(0.25f, 0.25f, 0.1f);
float spotLightCutoffStartInDegree = 30.0f;
float spotLightTotalWidthInDegree = 45.0f;
glm::vec3 ambientLight = glm::vec3(0.2f, 0.2f, 0.2f);
// Camera.
Camera* camera = nullptr;
glm::vec3 cameraPos = glm::vec3(0.0f, 1.0f, 5.0f);
glm::vec3 cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
float fovy = 30.0f;
float zNear = 0.1f;
float zFar = 1000.0f;
float constant = 1.0f;
float linear = 0.09;
float quadratic = 0.032;
// Shader.
FillColorShaderProg* fillColorShader = nullptr;
PhongShadingDemoShaderProg* phongShadingShader = nullptr;
SkyboxShaderProg* skyboxShader = nullptr;
// UI.
const float lightMoveSpeed = 0.2f;
// Skybox.
Skybox* skybox = nullptr;

ImageTexture* imageTex = nullptr;



// SceneObject.
struct SceneObject
{
    SceneObject() {
        mesh = nullptr;
        worldMatrix = glm::mat4x4(1.0f);
    }
    TriangleMesh* mesh;
    glm::mat4x4 worldMatrix;
    ImageTexture* tex = nullptr;
};
SceneObject sceneObj;

// ScenePointLight (for visualization of a point light).
struct ScenePointLight
{
    ScenePointLight() {
        light = nullptr;
        worldMatrix = glm::mat4x4(1.0f);
        visColor = glm::vec3(1.0f, 1.0f, 1.0f);
    }
    PointLight* light;
    glm::mat4x4 worldMatrix;
    glm::vec3 visColor;
};
ScenePointLight pointLightObj;
ScenePointLight spotLightObj;

// Frame capture (--capture <file.ppm>): write one frame and exit.
std::string captureFilePath;
// Profiling (always on). --bench <frames> renders a fixed animation with vsync
// off, writes a CSV (--csv <file>) and prints mean / median / p95, then exits.
FrameProfiler* profiler = nullptr;
int benchFrames = 0;
int benchWarmupFrames = 60;
std::string benchCsvPath = "bench.csv";
// Note: on tile-based GPUs (Apple silicon) fragment work for the whole frame may
// execute when the render pass is flushed, so the per-pass GPU split is approximate;
// cpu_frame with vsync off is the end-to-end number.
bool vsyncEnabled = true;
std::chrono::steady_clock::time_point lastTitleUpdate;
double loadMtlMs = 0.0, loadObjMs = 0.0, loadUploadMs = 0.0;
void FinishBenchmark();
// Model to load (--model <file.obj>); its .mtl must sit next to it with the same name.
std::string modelFilePath = "assets/Rose/Rose.obj";
// --dedup off keeps one vertex per face corner (the original layout), for comparison.
bool deduplicateVertices = true;
// --reflect k: environment reflection strength for every material (0..1).
// Negative (default) keeps each material's own value from the MTL (illum 3-7).
float reflectOverride = -1.0f;
// --shading pbr: Cook-Torrance + image-based lighting from --env <file.hdr>
// (replaces the Phong shader and the sphere skybox). --metallic/--roughness
// override every material; --exposure scales before tone mapping.
bool usePbr = false;
std::string environmentFilePath = "assets/third_party/hdri/studio_small_08_2k.hdr";
float exposure = 1.0f;
float iblIntensity = 1.0f;
float metallicOverride = -1.0f;
float roughnessOverride = -1.0f;
EnvironmentIBL* environment = nullptr;
ShaderProg* pbrShader = nullptr;
void DrawMeshPBR(TriangleMesh* mesh, const glm::mat4& normalMatrix, const glm::mat4& MVP);
// --skybox <image>: equirectangular panorama for the background and reflections.
std::string skyboxFilePath = "assets/textures/photostudio_02_2k.png";
int framesRendered = 0;
const int captureAfterFrames = 10;
void CaptureFrame(const std::string& path);
void ReportLiveGLObjects();

// Function prototypes.
void ReleaseResources();
// Callback functions.
void RenderSceneCB();
void FramebufferSizeCB(GLFWwindow*, int, int);
void KeyCB(GLFWwindow*, int, int, int, int);
void SetupRenderState();
void LoadObjects();
void CreateCamera();
void CreateSkybox(const std::string);
void CreateShaderLib();



void ReleaseResources()
{
    // Delete scene objects and lights.
    if (mesh != nullptr) {
        delete mesh;
        mesh = nullptr;
    }
    if (pointLight != nullptr) {
        delete pointLight;
        pointLight = nullptr;
    }
    if (dirLight != nullptr) {
        delete dirLight;
        dirLight = nullptr;
    }
    if (spotLight != nullptr) {
        delete spotLight;
        spotLight = nullptr;
    }
    sceneObj.mesh = nullptr;
    pointLightObj.light = nullptr;
    spotLightObj.light = nullptr;
    if (skybox != nullptr) {
        delete skybox;
        skybox = nullptr;
    }
    if (profiler != nullptr) {
        delete profiler;
        profiler = nullptr;
    }
    if (environment != nullptr) {
        delete environment;
        environment = nullptr;
    }
    if (pbrShader != nullptr) {
        delete pbrShader;
        pbrShader = nullptr;
    }
    // Delete camera.
    if (camera != nullptr) {
        delete camera;
        camera = nullptr;
    }
    // Delete shaders.
    if (fillColorShader != nullptr) {
        delete fillColorShader;
        fillColorShader = nullptr;
    }
    if (phongShadingShader != nullptr) {
        delete phongShadingShader;
        phongShadingShader = nullptr;
    }
    if (skyboxShader != nullptr) {
        delete skyboxShader;
        skyboxShader = nullptr;
    }
}

static float initialObjRotationY = 30.0f;
static float curObjRotationY = initialObjRotationY;
const float rotStep = 0.002f;
void RenderSceneCB()
{
    profiler->BeginFrame();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Render a triangle mesh with Gouraud shading. ----------------------------------------------
    TriangleMesh* pMesh = sceneObj.mesh;
    if (pMesh != nullptr) {
        // Update transform. Benchmarks turn the model a fixed 0.36 degrees per frame
        // so every run renders the same sequence of views.
        if (benchFrames > 0)
            curObjRotationY = initialObjRotationY + 0.36f * (float)framesRendered;
        glm::mat4x4 S = glm::scale(glm::mat4x4(1.0f), glm::vec3(1.5f, 1.5f, 1.5f));
        glm::mat4x4 R = glm::rotate(glm::mat4x4(1.0f), glm::radians(curObjRotationY), glm::vec3(0, 1, 0));
        sceneObj.worldMatrix = S * R;
        // Lighting is computed in world space, so normals use the world matrix only.
        glm::mat4x4 normalMatrix = glm::transpose(glm::inverse(sceneObj.worldMatrix));
        glm::mat4x4 MVP = camera->GetProjMatrix() * camera->GetViewMatrix() * sceneObj.worldMatrix;

        profiler->BeginPass(PASS_MESH);
        if (usePbr) {
            DrawMeshPBR(pMesh, normalMatrix, MVP);
        } else {
            phongShadingShader->Bind();

            glUniformMatrix4fv(phongShadingShader->GetLocM(), 1, GL_FALSE, glm::value_ptr(sceneObj.worldMatrix));
            glUniformMatrix4fv(phongShadingShader->GetLocNM(), 1, GL_FALSE, glm::value_ptr(normalMatrix));
            glUniformMatrix4fv(phongShadingShader->GetLocMVP(), 1, GL_FALSE, glm::value_ptr(MVP));

            // The light objects are the single source of truth for light state.
            glUniform3fv(phongShadingShader->GetLocAmbientLight(), 1, glm::value_ptr(ambientLight));
            glUniform3fv(phongShadingShader->GetLocCameraPos(), 1, glm::value_ptr(cameraPos));
            glUniform3fv(phongShadingShader->GetLocDirLightDir(), 1, glm::value_ptr(dirLight->GetDirection()));
            glUniform3fv(phongShadingShader->GetLocDirLightRadiance(), 1, glm::value_ptr(dirLight->GetRadiance()));
            glUniform3fv(phongShadingShader->GetLocPointLightIntensity(), 1, glm::value_ptr(pointLight->GetIntensity()));
            glUniform3fv(phongShadingShader->GetLocPointLightPos(), 1, glm::value_ptr(pointLight->GetPosition()));
            glUniform3fv(phongShadingShader->GetLocSpotLightDir(), 1, glm::value_ptr(spotLight->GetDirection()));
            glUniform3fv(phongShadingShader->GetLocSpotLightIntensity(), 1, glm::value_ptr(spotLight->GetIntensity()));
            glUniform3fv(phongShadingShader->GetLocSpotLightPos(), 1, glm::value_ptr(spotLight->GetPosition()));
            glUniform1f(phongShadingShader->GetLoclightconstant(), constant);
            glUniform1f(phongShadingShader->GetLoclightlinear(), linear);
            glUniform1f(phongShadingShader->GetLoclightquadratic(), quadratic);
            glUniform1f(phongShadingShader->GetLocspotLightCutoffStartInDegree(), spotLight->GetCutoffStartInDegree());
            glUniform1f(phongShadingShader->GetLocspotLightTotalWidthInDegree(), spotLight->GetTotalWidthInDegree());
            glUniform1i(phongShadingShader->GetLocmapKd(), 0);
            // The skybox panorama doubles as the environment map (texture unit 1).
            glUniform1i(phongShadingShader->GetLocEnvMap(), 1);
            if (skybox != nullptr)
                skybox->GetTexture()->Bind(GL_TEXTURE1);

            for (int i = 0; i < pMesh->GetNumSubMeshes(); i++) {
                const SubMesh& sub = pMesh->GetSubMeshesPosition(i);
                if (sub.vaoId == 0) continue;

                glUniform3fv(phongShadingShader->GetLocKa(), 1, glm::value_ptr(sub.material->GetKa()));
                glUniform3fv(phongShadingShader->GetLocKd(), 1, glm::value_ptr(sub.material->GetKd()));
                glUniform3fv(phongShadingShader->GetLocKs(), 1, glm::value_ptr(sub.material->GetKs()));
                glUniform1f(phongShadingShader->GetLocNs(), sub.material->GetNs());
                const float reflectivity = (skybox == nullptr) ? 0.0f
                    : (reflectOverride >= 0.0f ? reflectOverride : sub.material->GetReflectivity());
                glUniform1f(phongShadingShader->GetLocReflectivity(), reflectivity);
                const bool hasMapKd = sub.material->GetMapKd() != nullptr;
                glUniform1i(phongShadingShader->GetLocHasMapKd(), hasMapKd ? 1 : 0);
                if (hasMapKd)
                    sub.material->GetMapKd()->Bind(GL_TEXTURE0);

                glBindVertexArray(sub.vaoId);
                glDrawElements(GL_TRIANGLES, (GLsizei)sub.vertexIndices.size(), GL_UNSIGNED_INT, 0);
                profiler->CountDraw((long long)sub.vertexIndices.size() / 3);
            }
            glBindVertexArray(0);
            phongShadingShader->UnBind();
        }
        profiler->EndPass(PASS_MESH);


    }
    // -------------------------------------------------------------------------------------------

    // Visualize the light with fill color. ------------------------------------------------------
    profiler->BeginPass(PASS_LIGHTS);
    PointLight* pointLight = pointLightObj.light;
    if (pointLight != nullptr) {
        glm::mat4x4 T = glm::translate(glm::mat4x4(1.0f), pointLight->GetPosition());
        pointLightObj.worldMatrix = T;
        glm::mat4x4 MVP = camera->GetProjMatrix() * camera->GetViewMatrix() * pointLightObj.worldMatrix;
        fillColorShader->Bind();
        glUniformMatrix4fv(fillColorShader->GetLocMVP(), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform3fv(fillColorShader->GetLocFillColor(), 1, glm::value_ptr(pointLightObj.visColor));
        // Render the point light.
        pointLight->Draw();
        profiler->CountDraw(0);
        fillColorShader->UnBind();
    }
    SpotLight* spotLight = (SpotLight*)(spotLightObj.light);
    if (spotLight != nullptr) {
        glm::mat4x4 T = glm::translate(glm::mat4x4(1.0f), spotLight->GetPosition());
        spotLightObj.worldMatrix = T;
        glm::mat4x4 MVP = camera->GetProjMatrix() * camera->GetViewMatrix() * spotLightObj.worldMatrix;
        fillColorShader->Bind();
        glUniformMatrix4fv(fillColorShader->GetLocMVP(), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform3fv(fillColorShader->GetLocFillColor(), 1, glm::value_ptr(spotLightObj.visColor));
        // Render the spot light.
        spotLight->Draw();
        profiler->CountDraw(0);
        fillColorShader->UnBind();
    }
    profiler->EndPass(PASS_LIGHTS);
    // -------------------------------------------------------------------------------------------
    
    // Render skybox. ----------------------------------------------------------------------------
    if (skybox != nullptr) {
        // -------------------------------------------------------
	    // Add your code to rotate the skybox.
        // -------------------------------------------------------
        profiler->BeginPass(PASS_SKYBOX);
        skybox->Render(camera, skyboxShader);
        profiler->CountDraw(skybox->GetNumTriangles());
        profiler->EndPass(PASS_SKYBOX);
    }
    if (environment != nullptr) {
        profiler->BeginPass(PASS_SKYBOX);
        environment->RenderSkybox(camera, exposure);
        profiler->CountDraw(environment->GetSkyboxTriangles());
        profiler->EndPass(PASS_SKYBOX);
    }
    // -------------------------------------------------------------------------------------------

    ++framesRendered;
    if (!captureFilePath.empty() && framesRendered == captureAfterFrames) {
        CaptureFrame(captureFilePath);
        reportLiveObjects = true;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    profiler->EndFrame();
    glfwSwapBuffers(window);

    if (benchFrames > 0) {
        // Drop warm-up frames (shader compilation, texture residency, clocks ramping).
        if (framesRendered == benchWarmupFrames) {
            profiler->Finish();
            profiler->ClearSamples();
        }
        if (framesRendered == benchWarmupFrames + benchFrames)
            FinishBenchmark();
        return;
    }

    // Interactive mode: show a one-second rolling average in the window title.
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - lastTitleUpdate).count() >= 1.0) {
        lastTitleUpdate = now;
        std::vector<double> cpu, gpu;
        for (const FrameSample& f : profiler->GetSamples()) {
            if (f.cpuFrameMs > 0.0) cpu.push_back(f.cpuFrameMs);
            gpu.push_back(f.gpuTotalMs);
        }
        Stats c = ComputeStats(cpu), g = ComputeStats(gpu);
        std::ostringstream title;
        title << std::fixed << std::setprecision(2) << "Rendering Engine | frame " << c.mean
              << " ms | GPU " << g.mean << " ms | draws " << (profiler->GetSamples().empty() ? 0 : profiler->GetSamples().back().drawCalls);
        glfwSetWindowTitle(window, title.str().c_str());
        profiler->ClearSamples();
    }
}

void DrawMeshPBR(TriangleMesh* mesh, const glm::mat4& normalMatrix, const glm::mat4& MVP)
{
    ShaderProg& p = *pbrShader;
    p.Bind();
    glUniformMatrix4fv(p.Uniform("worldMatrix"), 1, GL_FALSE, glm::value_ptr(sceneObj.worldMatrix));
    glUniformMatrix4fv(p.Uniform("normalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMatrix));
    glUniformMatrix4fv(p.Uniform("MVP"), 1, GL_FALSE, glm::value_ptr(MVP));
    glUniform3fv(p.Uniform("cameraPos"), 1, glm::value_ptr(cameraPos));
    glUniform3fv(p.Uniform("dirLightDir"), 1, glm::value_ptr(dirLight->GetDirection()));
    glUniform3fv(p.Uniform("dirLightRadiance"), 1, glm::value_ptr(dirLight->GetRadiance()));
    glUniform3fv(p.Uniform("pointLightPos"), 1, glm::value_ptr(pointLight->GetPosition()));
    glUniform3fv(p.Uniform("pointLightIntensity"), 1, glm::value_ptr(pointLight->GetIntensity()));
    glUniform3fv(p.Uniform("spotLightPosition"), 1, glm::value_ptr(spotLight->GetPosition()));
    glUniform3fv(p.Uniform("spotLightDirection"), 1, glm::value_ptr(spotLight->GetDirection()));
    glUniform3fv(p.Uniform("spotLightIntensity"), 1, glm::value_ptr(spotLight->GetIntensity()));
    glUniform1f(p.Uniform("constant"), constant);
    glUniform1f(p.Uniform("linear"), linear);
    glUniform1f(p.Uniform("quadratic"), quadratic);
    glUniform1f(p.Uniform("spotLightCutoffStartInDegree"), spotLight->GetCutoffStartInDegree());
    glUniform1f(p.Uniform("spotLightTotalWidthInDegree"), spotLight->GetTotalWidthInDegree());
    glUniform1f(p.Uniform("exposure"), exposure);
    glUniform1f(p.Uniform("iblIntensity"), iblIntensity);
    glUniform1f(p.Uniform("prefilterMaxLod"), environment->GetPrefilterMaxLod());
    // Texture units: 0 base colour, 1 irradiance, 2 pre-filtered specular, 3 BRDF LUT.
    glUniform1i(p.Uniform("mapKd"), 0);
    glUniform1i(p.Uniform("irradianceMap"), 1);
    glUniform1i(p.Uniform("prefilterMap"), 2);
    glUniform1i(p.Uniform("brdfLUT"), 3);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment->GetIrradianceMap());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment->GetPrefilterMap());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, environment->GetBrdfLut());

    for (int i = 0; i < mesh->GetNumSubMeshes(); i++) {
        const SubMesh& sub = mesh->GetSubMeshesPosition(i);
        if (sub.vaoId == 0) continue;
        const PhongMaterial* m = sub.material;
        glUniform3fv(p.Uniform("baseColor"), 1, glm::value_ptr(m->GetKd()));
        glUniform1f(p.Uniform("metallic"), metallicOverride >= 0.0f ? metallicOverride : m->GetMetallic());
        glUniform1f(p.Uniform("roughness"), roughnessOverride >= 0.0f ? roughnessOverride : m->GetRoughness());
        const bool hasMapKd = m->GetMapKd() != nullptr;
        glUniform1i(p.Uniform("hasMapKd"), hasMapKd ? 1 : 0);
        if (hasMapKd)
            m->GetMapKd()->Bind(GL_TEXTURE0);
        glBindVertexArray(sub.vaoId);
        glDrawElements(GL_TRIANGLES, (GLsizei)sub.vertexIndices.size(), GL_UNSIGNED_INT, 0);
        profiler->CountDraw((long long)sub.vertexIndices.size() / 3);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    p.UnBind();
}

void FinishBenchmark()
{
    std::vector<FrameSample> samples = profiler->Finish();
    // The last frame's wall time is unknown (no next frame started); keep the
    // first benchFrames complete frames.
    if ((int)samples.size() > benchFrames) samples.resize(benchFrames);
    if (!samples.empty() && samples.back().cpuFrameMs == 0.0) samples.pop_back();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::cout << "\n=== Benchmark ===\n"
              << "model: " << modelFilePath << "\n"
              << "renderer: " << glGetString(GL_RENDERER) << " | " << glGetString(GL_VERSION) << "\n"
              << "viewport: " << viewport[2] << "x" << viewport[3]
              << " | vsync: " << (vsyncEnabled ? "on" : "off")
              << " | warm-up frames: " << benchWarmupFrames << "\n"
              << std::fixed << std::setprecision(1)
              << "load: MTL+textures " << loadMtlMs << " ms, OBJ parse " << loadObjMs
              << " ms, GPU upload " << loadUploadMs << " ms\n";
    if (environment != nullptr) {
        const EnvironmentIBL::Timings& t = environment->GetTimings();
        std::cout << "shading: PBR + IBL (" << environmentFilePath << "), precompute " << t.Total()
                  << " ms: HDR load " << t.loadHdrMs << ", cubemap " << t.toCubemapMs
                  << ", irradiance " << t.irradianceMs << ", prefilter " << t.prefilterMs
                  << ", BRDF LUT " << t.brdfLutMs << "\n";
    } else {
        std::cout << "shading: Phong\n";
    }
    PrintBenchSummary(std::cout, samples);
    if (WriteBenchCsv(benchCsvPath, samples))
        std::cout << "CSV: " << benchCsvPath << std::endl;
    else
        std::cerr << "[ERROR] Cannot write " << benchCsvPath << std::endl;
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void ReportLiveGLObjects()
{
    // Names are handed out sequentially from 1, so a scan of the low range finds
    // anything still alive after ReleaseResources().
    int buffers = 0, textures = 0, vertexArrays = 0, programs = 0, framebuffers = 0;
    for (GLuint id = 1; id <= 4096; ++id) {
        buffers += glIsBuffer(id) ? 1 : 0;
        textures += glIsTexture(id) ? 1 : 0;
        vertexArrays += glIsVertexArray(id) ? 1 : 0;
        programs += glIsProgram(id) ? 1 : 0;
        framebuffers += (glIsFramebuffer(id) || glIsRenderbuffer(id)) ? 1 : 0;
    }
    std::cout << "Live GL objects after release: " << buffers << " buffers, "
              << textures << " textures, " << vertexArrays << " vertex arrays, " << programs << " programs, "
              << framebuffers << " framebuffers/renderbuffers" << std::endl;
}

void CaptureFrame(const std::string& path)
{
    // Read the back buffer (before swap) and save it as a binary PPM, top row first.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int w = viewport[2], h = viewport[3];
    std::vector<unsigned char> pixels((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream ofs(path, std::ios::binary);
    ofs << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        ofs.write((const char*)&pixels[(size_t)y * w * 3], (std::streamsize)w * 3);
    std::cout << "Captured " << w << "x" << h << " frame to " << path << std::endl;
}

void FramebufferSizeCB(GLFWwindow*, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;	// minimized
    // Update viewport.
    screenWidth = w;
    screenHeight = h;
    glViewport(0, 0, screenWidth, screenHeight);
    // Adjust camera and projection.
    float aspectRatio = (float)screenWidth / (float)screenHeight;
    camera->UpdateProjection(fovy, aspectRatio, zNear, zFar);
}

void KeyCB(GLFWwindow* win, int key, int, int action, int)
{
    if (action == GLFW_RELEASE)
        return;
    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        break;
    // Rendering mode: points, lines, fill.
    case GLFW_KEY_F1: glPolygonMode(GL_FRONT_AND_BACK, GL_POINT); break;
    case GLFW_KEY_F2: glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); break;
    case GLFW_KEY_F3: glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); break;
    // Point light: arrow keys.
    case GLFW_KEY_LEFT:  if (pointLight != nullptr) pointLight->MoveLeft(lightMoveSpeed); break;
    case GLFW_KEY_RIGHT: if (pointLight != nullptr) pointLight->MoveRight(lightMoveSpeed); break;
    case GLFW_KEY_UP:    if (pointLight != nullptr) pointLight->MoveUp(lightMoveSpeed); break;
    case GLFW_KEY_DOWN:  if (pointLight != nullptr) pointLight->MoveDown(lightMoveSpeed); break;
    // Spot light: WASD.
    case GLFW_KEY_A: if (spotLight != nullptr) spotLight->MoveLeft(lightMoveSpeed); break;
    case GLFW_KEY_D: if (spotLight != nullptr) spotLight->MoveRight(lightMoveSpeed); break;
    case GLFW_KEY_W: if (spotLight != nullptr) spotLight->MoveUp(lightMoveSpeed); break;
    case GLFW_KEY_S: if (spotLight != nullptr) spotLight->MoveDown(lightMoveSpeed); break;
    default: break;
    }
}

void SetupRenderState()
{
    glEnable(GL_DEPTH_TEST);

    glm::vec4 clearColor = glm::vec4(0.44f, 0.57f, 0.75f, 1.00f);
    glClearColor(
        (GLclampf)(clearColor.r), 
        (GLclampf)(clearColor.g), 
        (GLclampf)(clearColor.b), 
        (GLclampf)(clearColor.a)
    );
}

void LoadObjects()
{
    // -------------------------------------------------------
	// Note: you can change the code below if you want to load
    //       the model dynamically.
	// -------------------------------------------------------

    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    mesh = new TriangleMesh();
    ObjLoadOptions options;
    options.deduplicate = deduplicateVertices;
    if (!mesh->LoadFromFile(modelFilePath, true, options))
        exit(1);
    auto tObj = Clock::now();
    mesh->LoadMaterials();
    auto t1 = Clock::now();
    mesh->CreateVertexBuffer();
    mesh->CreateSubmeshBuffer();
    glFinish();
    auto t2 = Clock::now();
    loadObjMs = std::chrono::duration<double, std::milli>(tObj - t0).count();
    loadMtlMs = std::chrono::duration<double, std::milli>(t1 - tObj).count();
    loadUploadMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    mesh->ShowInfo();
    std::cout << std::fixed << std::setprecision(1) << "Load: MTL+textures " << loadMtlMs
              << " ms, OBJ parse " << loadObjMs << " ms, GPU upload " << loadUploadMs << " ms" << std::endl;
    sceneObj.mesh = mesh;
}

void CreateLights()
{
    // Create a directional light.
    dirLight = new DirectionalLight(dirLightDirection, dirLightRadiance);
    // Create a point light.
    pointLight = new PointLight(pointLightPosition, pointLightIntensity);
    pointLightObj.light = pointLight;
    pointLightObj.visColor = glm::normalize((pointLightObj.light)->GetIntensity());
    // Create a spot light.
    spotLight = new SpotLight(spotLightPosition, spotLightIntensity, spotLightDirection, 
            spotLightCutoffStartInDegree, spotLightTotalWidthInDegree);
    spotLightObj.light = spotLight;
    spotLightObj.visColor = glm::normalize((spotLightObj.light)->GetIntensity());
}

void CreateCamera()
{
    // Create a camera and update view and proj matrices.
    camera = new Camera((float)screenWidth / (float)screenHeight);
    camera->UpdateView(cameraPos, cameraTarget, cameraUp);
    float aspectRatio = (float)screenWidth / (float)screenHeight;
    camera->UpdateProjection(fovy, aspectRatio, zNear, zFar);
}

void CreateSkybox(const std::string texFilePath)
{
    // -------------------------------------------------------
	// Note: you can change the code below if you want to change
    //       the skybox texture dynamically.
	// -------------------------------------------------------

    const int numSlices = 36;
    const int numStacks = 18;
    const float radius = 50.0f;
    skybox = new Skybox(texFilePath, numSlices, numStacks, radius);
}

void CreateShaderLib()
{
    fillColorShader = new FillColorShaderProg();
    if (!fillColorShader->LoadFromFiles("shaders/fixed_color.vs", "shaders/fixed_color.fs"))
        exit(1);

    phongShadingShader = new PhongShadingDemoShaderProg();
    if (!phongShadingShader->LoadFromFiles("shaders/phong_shading_demo.vs", "shaders/phong_shading_demo.fs"))
        exit(1);

    skyboxShader = new SkyboxShaderProg();
    if (!skyboxShader->LoadFromFiles("shaders/skybox.vs", "shaders/skybox.fs"))
        exit(1);
}

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string arg = argv[i];
        const std::string value = argv[i + 1];
        if (arg == "--capture")      captureFilePath = value;
        else if (arg == "--model")   modelFilePath = value;
        else if (arg == "--bench")   benchFrames = std::max(1, std::atoi(value.c_str()));
        else if (arg == "--warmup")  benchWarmupFrames = std::max(0, std::atoi(value.c_str()));
        else if (arg == "--csv")     benchCsvPath = value;
        else if (arg == "--vsync")   vsyncEnabled = (value != "off");
        else if (arg == "--dedup")   deduplicateVertices = (value != "off");
        else if (arg == "--camera")    std::sscanf(value.c_str(), "%f,%f,%f", &cameraPos.x, &cameraPos.y, &cameraPos.z);
        else if (arg == "--target")    std::sscanf(value.c_str(), "%f,%f,%f", &cameraTarget.x, &cameraTarget.y, &cameraTarget.z);
        else if (arg == "--light-dir") std::sscanf(value.c_str(), "%f,%f,%f", &dirLightDirection.x, &dirLightDirection.y, &dirLightDirection.z);
        else if (arg == "--reflect")   reflectOverride = std::min(1.0f, (float)std::atof(value.c_str()));
        else if (arg == "--skybox")    skyboxFilePath = value;
        else if (arg == "--shading")   usePbr = (value == "pbr");
        else if (arg == "--env")       environmentFilePath = value;
        else if (arg == "--exposure")  exposure = (float)std::atof(value.c_str());
        else if (arg == "--ibl-intensity") iblIntensity = (float)std::atof(value.c_str());
        else if (arg == "--metallic")  metallicOverride = std::min(1.0f, (float)std::atof(value.c_str()));
        else if (arg == "--roughness") roughnessOverride = std::min(1.0f, (float)std::atof(value.c_str()));
        else if (arg == "--rotate")    curObjRotationY = initialObjRotationY = (float)std::atof(value.c_str());
        else if (arg == "--size")    std::sscanf(value.c_str(), "%dx%d", &screenWidth, &screenHeight);
        else continue;
        ++i;
    }
    if (benchFrames > 0 && !std::count(argv, argv + argc, std::string("--vsync")))
        vsyncEnabled = false;

    // Window and OpenGL context.
    if (!glfwInit()) {
        std::cerr << "[ERROR] glfwInit failed" << std::endl;
        return 1;
    }
#ifdef __APPLE__
    // macOS only offers core profiles up to 4.1, and only forward-compatible ones.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    // One framebuffer pixel per window point, so --size means pixels and numbers
    // stay comparable with the earlier GLUT runs.
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(screenWidth, screenHeight, "Rendering Engine", nullptr, nullptr);
    if (window == nullptr) {
        const char* description = nullptr;
        glfwGetError(&description);
        std::cerr << "[ERROR] Cannot create window: " << (description ? description : "unknown") << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

#if defined(ENGINE_USE_GLEW)
    // Core profiles need glewExperimental; glewInit can leave GL_INVALID_ENUM behind.
    glewExperimental = GL_TRUE;
    GLenum res = glewInit();
    if (res != GLEW_OK) {
        std::cerr << "GLEW initialization error: "
                  << glewGetErrorString(res) << std::endl;
        return 1;
    }
    glGetError();
#endif
    std::cout << "GL_VERSION: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GL_RENDERER: " << glGetString(GL_RENDERER) << std::endl;

    glfwSwapInterval(vsyncEnabled ? 1 : 0);
    profiler = new FrameProfiler();
    lastTitleUpdate = std::chrono::steady_clock::now();

    // Initialization.
    SetupRenderState();
    LoadObjects();
    CreateLights();
    CreateCamera();
    if (usePbr) {
        environment = new EnvironmentIBL();
        if (!environment->Build(environmentFilePath, EnvironmentIBL::Settings())) {
            std::cerr << "[ERROR] IBL setup failed (run tools/fetch_assets.sh for the HDR images)" << std::endl;
            return 1;
        }
        const EnvironmentIBL::Timings& t = environment->GetTimings();
        std::cout << std::fixed << std::setprecision(1) << "IBL precompute: " << t.Total() << " ms (HDR load "
                  << t.loadHdrMs << ", cubemap " << t.toCubemapMs << ", irradiance " << t.irradianceMs
                  << ", prefilter " << t.prefilterMs << ", BRDF LUT " << t.brdfLutMs << ")" << std::endl;
        pbrShader = new ShaderProg();
        if (!pbrShader->LoadFromFiles("shaders/phong_shading_demo.vs", "shaders/pbr.fs"))
            return 1;
    } else {
        CreateSkybox(skyboxFilePath);
    }
    CreateShaderLib();

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCB);
    glfwSetKeyCallback(window, KeyCB);
    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    FramebufferSizeCB(window, fbWidth, fbHeight);

    // Rendering loop.
    while (!glfwWindowShouldClose(window)) {
        RenderSceneCB();
        glfwPollEvents();
    }

    ReleaseResources();
    if (reportLiveObjects)
        ReportLiveGLObjects();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
