#ifndef GLTFLOADER_H
#define GLTFLOADER_H

// glTF 2.0 (.gltf / .glb) loading with no OpenGL dependency: cgltf parses the
// file, this code flattens the scene (node transforms baked into vertices),
// converts accessors, fills in missing normals/tangents and decodes images.
// GltfModel turns the result into GPU buffers and textures.

#include <glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Interleaved vertex with a tangent (xyz, w = bitangent sign), 48 bytes.
struct VertexPNTT
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
	glm::vec2 texcoord = glm::vec2(0.0f);
	glm::vec4 tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
};

// Decoded RGBA8 image, top row first (glTF UV (0,0) is the top-left corner,
// so it is uploaded without flipping).
struct GltfImage
{
	std::string name;
	int width = 0;
	int height = 0;
	std::vector<unsigned char> rgba;
};

// Metallic-roughness material. Image indices point into GltfModelData::images
// (-1 = none). Colour textures (base colour, emissive) are sRGB-encoded.
struct GltfMaterial
{
	std::string name;
	glm::vec4 baseColorFactor = glm::vec4(1.0f);
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	glm::vec3 emissiveFactor = glm::vec3(0.0f);
	float normalScale = 1.0f;
	float occlusionStrength = 1.0f;
	int baseColorImage = -1;
	int metallicRoughnessImage = -1;	// G = roughness, B = metallic
	int normalImage = -1;
	int occlusionImage = -1;			// R
	int emissiveImage = -1;
	bool alphaBlendOrMask = false;		// drawn as opaque for now
};

struct GltfStats
{
	size_t nodesWithMesh = 0;
	size_t primitives = 0;
	size_t skippedPrimitives = 0;		// not triangles, no positions, or bad indices
	size_t triangles = 0;
	size_t generatedNormals = 0;		// vertices
	size_t generatedTangents = 0;		// vertices
	size_t images = 0;
	size_t failedImages = 0;
	std::vector<std::string> extensionsUsed;
	double parseMs = 0.0;		// cgltf parse + buffers + scene flattening
	double imageDecodeMs = 0.0;	// PNG/JPEG decoding
};

struct GltfModelData
{
	std::vector<VertexPNTT> vertices;
	std::vector<GltfMaterial> materials;
	// One index list per material (same order as materials), like the OBJ path.
	std::vector<std::vector<uint32_t>> indicesByMaterial;
	std::vector<GltfImage> images;
	GltfStats stats;
};

struct GltfLoadOptions
{
	bool decodeImages = true;
};

// Load a .gltf (with external or data: buffers/images) or a .glb file.
// Returns false with a message in `error` if the file cannot be parsed.
bool LoadGltfFile(const std::string& path, GltfModelData& out, std::string& error,
				  const GltfLoadOptions& options = GltfLoadOptions());

// Same, from memory (used by tests and fuzzing). External URIs resolve
// relative to baseDir; pass an empty baseDir to refuse them.
bool LoadGltfMemory(const void* data, size_t size, const std::string& baseDir, GltfModelData& out,
					std::string& error, const GltfLoadOptions& options = GltfLoadOptions());

// Center at the origin and scale the longest side to 1 (same as the OBJ path).
void NormalizeToUnitBox(GltfModelData& model);

// Per-vertex tangents from UV derivatives (accumulated per triangle, then
// Gram-Schmidt against the normal; w = handedness). Used when a primitive has
// no TANGENT attribute. Not MikkTSpace, so seams can differ slightly from the
// tangents an authoring tool would bake.
void GenerateTangents(std::vector<VertexPNTT>& vertices, const std::vector<uint32_t>& indices,
					  size_t firstVertex, size_t vertexCount);

#endif
