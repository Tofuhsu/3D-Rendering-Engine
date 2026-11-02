#include "trianglemesh.h"

#include <algorithm>
#include <cmath>

// Constructor of a triangle mesh.
TriangleMesh::TriangleMesh()
{
	vboId = 0;
	numVertices = 0;
	numTriangles = 0;
	objCenter = glm::vec3(0.0f, 0.0f, 0.0f);
	objExtent = glm::vec3(0.0f, 0.0f, 0.0f);
}

// Destructor of a triangle mesh.
TriangleMesh::~TriangleMesh()
{
	for (SubMesh& sub : subMeshes) {
		if (sub.vaoId != 0) glDeleteVertexArrays(1, &sub.vaoId);
		if (sub.iboId != 0) glDeleteBuffers(1, &sub.iboId);
		delete sub.material;
	}
	subMeshes.clear();
	for (auto& entry : textures)
		delete entry.second;
	textures.clear();
	if (vboId != 0) glDeleteBuffers(1, &vboId);
	vertices.clear();
}

// Load the geometry from an OBJ file.
bool TriangleMesh::LoadFromFile(const std::string& filePath, const bool normalized, const ObjLoadOptions& options)
{
	objFilePath = filePath;
	objData = ObjMesh();
	if (!LoadObjFile(filePath, objData, options)) {
		std::cerr << "[ERROR] Cannot open OBJ file: " << filePath << std::endl;
		return false;
	}
	stats = objData.stats;
	if (stats.skippedFaces > 0 || stats.ignoredLines > 0)
		std::cerr << "[WARN] " << filePath << ": skipped " << stats.skippedFaces << " faces, ignored "
				  << stats.ignoredLines << " lines" << std::endl;

	glm::vec3 lo, hi;
	ComputeBounds(objData, lo, hi);
	objCenter = 0.5f * (lo + hi);
	objExtent = hi - lo;
	if (normalized)
		NormalizeToUnitBox(objData);

	vertices = std::move(objData.vertices);
	numVertices = (int)vertices.size();
	numTriangles = (int)stats.triangles;
	return true;
}

// Load materials and textures, then build one sub-mesh per material group.
void TriangleMesh::LoadMaterials()
{
	LoadObjMaterials(objFilePath, objData);
	for (ObjGroup& group : objData.groups) {
		if (group.indices.empty())
			continue;
		const ObjMaterial& src = objData.materials[(size_t)group.materialIndex];
		SubMesh sub;
		sub.material = new PhongMaterial();
		sub.material->SetName(src.name);
		sub.material->SetKa(src.Ka);
		sub.material->SetKd(src.Kd);
		sub.material->SetKs(src.Ks);
		sub.material->SetNs(src.Ns);
		sub.material->SetReflectivity(src.reflectivity);
		// PBR parameters: MTL Pm/Pr when present; otherwise dielectric, with
		// roughness from the Phong exponent (Blinn-Phong <-> Beckmann/GGX match
		// a = sqrt(2 / (Ns + 2)), and roughness = sqrt(a)).
		sub.material->SetMetallic(src.metallic >= 0.0f ? src.metallic : 0.0f);
		sub.material->SetRoughness(src.roughness >= 0.0f ? src.roughness
			: std::sqrt(std::sqrt(2.0f / (std::max(src.Ns, 0.0f) + 2.0f))));
		if (!src.mapKd.empty()) {
			ImageTexture*& tex = textures[src.mapKd];
			if (tex == nullptr)
				tex = new ImageTexture(src.mapKd);
			sub.material->SetMapKd(tex);
		}
		sub.vertexIndices = std::move(group.indices);
		subMeshes.push_back(std::move(sub));
	}
	objData = ObjMesh();
}

void TriangleMesh::CreateVertexBuffer() {

	if (vertices.empty())
		return;
	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPTN) * GetNumVertices(), &(vertices[0]), GL_STATIC_DRAW);

}
void TriangleMesh::CreateSubmeshBuffer() {

	for (int i = 0; i < subMeshes.size(); i++) {
		if (subMeshes[i].vertexIndices.empty() || vboId == 0) continue;
		// The VAO records the attribute layout and the element buffer,
		// so drawing a sub-mesh is a single bind.
		glGenVertexArrays(1, &subMeshes[i].vaoId);
		glBindVertexArray(subMeshes[i].vaoId);
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPTN), (const GLvoid*)offsetof(VertexPTN, position));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPTN), (const GLvoid*)offsetof(VertexPTN, normal));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPTN), (const GLvoid*)offsetof(VertexPTN, texcoord));
		glGenBuffers(1, &subMeshes[i].iboId);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMeshes[i].iboId);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * subMeshes[i].vertexIndices.size(), &(subMeshes[i].vertexIndices[0]), GL_STATIC_DRAW);
		glBindVertexArray(0);
	}

}


void TriangleMesh::DrawGeometryOnly(std::vector<long long>& trianglesPerDraw) const
{
	for (const SubMesh& sub : subMeshes) {
		if (sub.vaoId == 0) continue;
		glBindVertexArray(sub.vaoId);
		glDrawElements(GL_TRIANGLES, (GLsizei)sub.vertexIndices.size(), GL_UNSIGNED_INT, 0);
		trianglesPerDraw.push_back((long long)sub.vertexIndices.size() / 3);
	}
	glBindVertexArray(0);
}

// Show model information.
void TriangleMesh::ShowInfo()
{
	std::cout << "# Vertices: " << numVertices << std::endl;
	std::cout << "# Triangles: " << numTriangles << std::endl;
	std::cout << "# Face corners: " << stats.faceCorners << " (positions " << stats.positions
			  << ", texcoords " << stats.texcoords << ", normals " << stats.normals << ")" << std::endl;
	std::cout << "Total " << subMeshes.size() << " subMeshes loaded" << std::endl;
	for (unsigned int i = 0; i < subMeshes.size(); ++i) {
		const SubMesh& g = subMeshes[i];
		std::cout << "SubMesh " << i << " with material: " << g.material->GetName() << std::endl;
		std::cout << "Num. triangles in the subMesh: " << g.vertexIndices.size() / 3 << std::endl;
	}
	std::cout << "Model Center: " << objCenter.x << ", " << objCenter.y << ", " << objCenter.z << std::endl;
	std::cout << "Model Extent: " << objExtent.x << " x " << objExtent.y << " x " << objExtent.z << std::endl;
}
