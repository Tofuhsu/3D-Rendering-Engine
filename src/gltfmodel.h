#ifndef GLTFMODEL_H
#define GLTFMODEL_H

#include "headers.h"
#include "gltfloader.h"
#include "shaderprog.h"

// GPU side of a loaded glTF model: one VBO (VertexPNTT), one VAO + IBO per
// material, one texture per image. Drawn with the PBR shader only.
class GltfModel
{
public:
	GltfModel();
	~GltfModel();

	// Upload `data` (vertices, indices, decoded images). The CPU copy can be dropped afterwards.
	void Upload(const GltfModelData& data);

	// Draw every material group. Texture units 4..7 are used for the material maps
	// (0..3 belong to the base-colour map and IBL). Returns triangles drawn per call.
	void Draw(ShaderProg& shader, std::vector<long long>& trianglesPerDraw);
	// Geometry only, with whatever program is bound (shadow depth pass).
	void DrawGeometryOnly(std::vector<long long>& trianglesPerDraw) const;

	int GetNumVertices() const { return numVertices; }
	int GetNumTriangles() const { return numTriangles; }
	int GetNumMaterials() const { return (int)groups.size(); }
	size_t GetTextureBytes() const { return textureBytes; }

private:
	struct Group
	{
		GltfMaterial material;
		GLuint vao = 0;
		GLuint ibo = 0;
		GLsizei indexCount = 0;
	};

	GLuint vbo;
	std::vector<Group> groups;
	std::vector<GLuint> textures;	// by image index (0 when decoding failed)
	int numVertices;
	int numTriangles;
	size_t textureBytes;	// RGBA8 level 0 + mip chain estimate (x4/3)
};

#endif
