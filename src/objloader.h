#ifndef OBJLOADER_H
#define OBJLOADER_H

// Wavefront OBJ / MTL parsing with no OpenGL dependency, so it can be unit
// tested and fuzzed on its own. TriangleMesh turns the result into GPU buffers.

#include <glm.hpp>

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

// Interleaved vertex: position, normal, texture coordinate (32 bytes).
struct VertexPTN
{
	VertexPTN() {
		position = glm::vec3(0.0f, 0.0f, 0.0f);
		normal = glm::vec3(0.0f, 1.0f, 0.0f);
		texcoord = glm::vec2(0.0f, 0.0f);
	}
	VertexPTN(glm::vec3 p, glm::vec3 n, glm::vec2 uv) {
		position = p;
		normal = n;
		texcoord = uv;
	}
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texcoord;
};

// One MTL material. Defaults apply to keys the file leaves out.
struct ObjMaterial
{
	std::string name;
	glm::vec3 Ka = glm::vec3(1.0f);
	glm::vec3 Kd = glm::vec3(0.8f);
	glm::vec3 Ks = glm::vec3(0.0f);
	float Ns = 10.0f;
	std::string mapKd;	// texture path, already joined with the MTL file's folder
};

// Triangles that share one material (from consecutive or repeated `usemtl`).
struct ObjGroup
{
	std::string materialName;
	int materialIndex = -1;	// into ObjMesh::materials, set by LinkMaterials()
	std::vector<uint32_t> indices;
};

struct ObjStats
{
	size_t positions = 0;
	size_t texcoords = 0;
	size_t normals = 0;
	size_t faces = 0;			// faces kept (any polygon size)
	size_t triangles = 0;		// after fan triangulation
	size_t faceCorners = 0;		// vertex references in kept faces
	size_t skippedFaces = 0;	// faces with a bad or out-of-range index
	size_t ignoredLines = 0;	// unknown or malformed statements
	size_t generatedNormals = 0;	// output vertices that got a computed normal
};

struct ObjMesh
{
	std::vector<VertexPTN> vertices;
	std::vector<ObjGroup> groups;
	std::vector<ObjMaterial> materials;
	std::vector<std::string> materialLibraries;	// `mtllib` names, as written
	ObjStats stats;
};

struct ObjLoadOptions
{
	// Merge face corners with the same position/texcoord/normal indices into one
	// vertex. Off reproduces the old one-vertex-per-corner layout.
	bool deduplicate = true;
	// Corners without a normal get the area-weighted average of the face normals
	// around their position.
	bool generateNormals = true;
};

// Parse OBJ text. Never throws on bad input: malformed lines are counted in
// stats and skipped. Returns false only if the stream cannot be read.
bool ParseObj(std::istream& in, ObjMesh& mesh, const ObjLoadOptions& options = ObjLoadOptions());

// Parse MTL text and append its materials. Texture paths are joined with baseDir.
bool ParseMtl(std::istream& in, const std::string& baseDir, std::vector<ObjMaterial>& materials);

// Resolve each group's material by name. Unknown names (and faces before any
// `usemtl`) get a default material so every group can be drawn.
void LinkMaterials(ObjMesh& mesh);

// File helpers. LoadObjFile parses geometry; LoadObjMaterials reads every
// `mtllib` (or <name>.mtl next to the OBJ if there is none) and links them.
bool LoadObjFile(const std::string& objPath, ObjMesh& mesh, const ObjLoadOptions& options = ObjLoadOptions());
void LoadObjMaterials(const std::string& objPath, ObjMesh& mesh);

// Bounding box of all vertex positions (min, max). Both zero for an empty mesh.
void ComputeBounds(const ObjMesh& mesh, glm::vec3& minCorner, glm::vec3& maxCorner);

// Center the mesh at the origin and scale its longest side to 1.
void NormalizeToUnitBox(ObjMesh& mesh);

#endif
