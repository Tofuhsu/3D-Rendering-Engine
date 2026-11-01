#ifndef TRIANGLEMESH_H
#define TRIANGLEMESH_H

#include "headers.h"
#include "material.h"
#include "objloader.h"

#include <map>

// SubMesh Declarations.
struct SubMesh
{
	SubMesh() {
		material = nullptr;
		iboId = 0;
		vaoId = 0;
	}
	PhongMaterial* material;
	GLuint iboId;
	GLuint vaoId;	// VBO attribute layout + this sub-mesh's IBO.
	std::vector<unsigned int> vertexIndices;
};


// TriangleMesh Declarations.
class TriangleMesh
{
public:
	// TriangleMesh Public Methods.
	TriangleMesh();
	~TriangleMesh();

	// Load the geometry from an *.OBJ file (see objloader.h).
	bool LoadFromFile(const std::string& filePath, const bool normalized = true,
					  const ObjLoadOptions& options = ObjLoadOptions());
	// Load the OBJ's materials (mtllib) and their textures; call after LoadFromFile.
	void LoadMaterials();

	// Show model information.
	void ShowInfo();

	int GetNumVertices() const { return numVertices; }
	int GetNumTriangles() const { return numTriangles; }
	int GetNumSubMeshes() const { return (int)subMeshes.size(); }
	const ObjStats& GetStats() const { return stats; }

	GLuint GetvboId() const { return vboId; }
	const SubMesh& GetSubMeshesPosition(const int i) const { return subMeshes[i]; }
	void CreateVertexBuffer();
	void CreateSubmeshBuffer();

	glm::vec3 GetObjCenter() const { return objCenter; }
	glm::vec3 GetObjExtent() const { return objExtent; }

	std::vector<VertexPTN> vertices;

private:
	// TriangleMesh Private Data.
	GLuint vboId;
	std::vector<SubMesh> subMeshes;
	// Textures are shared between materials that use the same file.
	std::map<std::string, ImageTexture*> textures;

	std::string objFilePath;
	ObjMesh objData;	// parsed groups/materials until LoadMaterials() builds sub-meshes
	ObjStats stats;

	int numVertices;
	int numTriangles;
	glm::vec3 objCenter;
	glm::vec3 objExtent;
};


#endif
