#ifndef TRIANGLEMESH_H
#define TRIANGLEMESH_H

#include "headers.h"
#include "material.h"
#include <fstream>


#include <iostream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <math.h>
#include <windows.h>


// VertexPTN Declarations.
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

// SubMesh Declarations.
struct SubMesh
{
	SubMesh() {
		material = nullptr;
		iboId = 0;
	}
	PhongMaterial* material;
	GLuint iboId;
	std::vector<unsigned int> vertexIndices;
};


// TriangleMesh Declarations.
class TriangleMesh
{
public:
	// TriangleMesh Public Methods.
	TriangleMesh();
	~TriangleMesh();
	
	// Load the model from an *.OBJ file.
	bool LoadFromFile(const std::string& filePath, const bool normalized = true);
	bool LoadFromFilemtl(const std::string& filePath, const bool normalized = true);
	
	// Show model information.
	void ShowInfo();

	// -------------------------------------------------------
	// Feel free to add your methods or data here.
	// -------------------------------------------------------

	int GetNumVertices() const { return numVertices; }
	int GetNumTriangles() const { return numTriangles; }
	int GetNumSubMeshes() const { return (int)subMeshes.size(); }
	int GetNumPositions() const { return vertices.size(); }

	void SetNumTriangles(const int NumTriangles) { numTriangles = NumTriangles; }
	GLuint GetvboId() const { return vboId; }
	SubMesh GetSubMeshesPosition(const int i) { return subMeshes[i]; }
	void CreateVertexBuffer();
	void CreateSubmeshBuffer();

	glm::vec3 GetObjCenter() const { return objCenter; }
	glm::vec3 GetObjExtent() const { return objExtent; }

	std::vector<VertexPTN> vertices;

private:
	// -------------------------------------------------------
	// Feel free to add your methods or data here.
	// -------------------------------------------------------

	// TriangleMesh Private Data.
	GLuint vboId;

	// For supporting multiple materials per object, move to SubMesh.
	// GLuint iboId;
	//std::vector<unsigned int> vertexIndices;
	std::vector<SubMesh> subMeshes;

	int numVertices;
	int numTriangles;
	glm::vec3 objCenter;
	glm::vec3 objExtent;
};


#endif
