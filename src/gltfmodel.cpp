#include "gltfmodel.h"

GltfModel::GltfModel()
	: vbo(0), numVertices(0), numTriangles(0), textureBytes(0)
{
}

GltfModel::~GltfModel()
{
	for (Group& g : groups) {
		glDeleteVertexArrays(1, &g.vao);
		glDeleteBuffers(1, &g.ibo);
	}
	if (!textures.empty())
		glDeleteTextures((GLsizei)textures.size(), textures.data());
	glDeleteBuffers(1, &vbo);
}

void GltfModel::Upload(const GltfModelData& data)
{
	numVertices = (int)data.vertices.size();
	if (numVertices == 0)
		return;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPNTT) * data.vertices.size(), data.vertices.data(), GL_STATIC_DRAW);

	textures.assign(data.images.size(), 0);
	for (size_t i = 0; i < data.images.size(); ++i) {
		const GltfImage& img = data.images[i];
		if (img.rgba.empty())
			continue;
		glGenTextures(1, &textures[i]);
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
		// glTF samplers are not read yet: repeat + trilinear for every texture.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glGenerateMipmap(GL_TEXTURE_2D);
		textureBytes += img.rgba.size() * 4 / 3;
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	for (size_t m = 0; m < data.materials.size(); ++m) {
		const std::vector<uint32_t>& indices = data.indicesByMaterial[m];
		if (indices.empty())
			continue;
		Group g;
		g.material = data.materials[m];
		g.indexCount = (GLsizei)indices.size();
		glGenVertexArrays(1, &g.vao);
		glBindVertexArray(g.vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPNTT), (const GLvoid*)offsetof(VertexPNTT, position));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexPNTT), (const GLvoid*)offsetof(VertexPNTT, normal));
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPNTT), (const GLvoid*)offsetof(VertexPNTT, texcoord));
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(VertexPNTT), (const GLvoid*)offsetof(VertexPNTT, tangent));
		glGenBuffers(1, &g.ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint32_t) * indices.size(), indices.data(), GL_STATIC_DRAW);
		glBindVertexArray(0);
		numTriangles += g.indexCount / 3;
		groups.push_back(g);
	}
}

void GltfModel::Draw(ShaderProg& p, std::vector<long long>& trianglesPerDraw)
{
	// Bind `image` to `unit` and tell the shader whether the map exists.
	auto bindMap = [&](int image, int unit, const char* sampler, const char* flag) {
		const GLuint tex = (image >= 0 && image < (int)textures.size()) ? textures[image] : 0;
		glUniform1i(p.Uniform(sampler), unit);
		glUniform1i(p.Uniform(flag), tex != 0 ? 1 : 0);
		if (tex != 0) {
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(GL_TEXTURE_2D, tex);
		}
	};
	for (const Group& g : groups) {
		const GltfMaterial& m = g.material;
		glUniform3fv(p.Uniform("baseColor"), 1, glm::value_ptr(glm::vec3(m.baseColorFactor)));
		glUniform1f(p.Uniform("metallic"), m.metallicFactor);
		glUniform1f(p.Uniform("roughness"), m.roughnessFactor);
		glUniform3fv(p.Uniform("emissiveFactor"), 1, glm::value_ptr(m.emissiveFactor));
		glUniform1f(p.Uniform("normalScale"), m.normalScale);
		glUniform1f(p.Uniform("occlusionStrength"), m.occlusionStrength);
		bindMap(m.baseColorImage, 0, "mapKd", "hasMapKd");
		bindMap(m.metallicRoughnessImage, 4, "metallicRoughnessMap", "hasMetallicRoughnessMap");
		bindMap(m.normalImage, 5, "normalMap", "hasNormalMap");
		bindMap(m.occlusionImage, 6, "occlusionMap", "hasOcclusionMap");
		bindMap(m.emissiveImage, 7, "emissiveMap", "hasEmissiveMap");
		glBindVertexArray(g.vao);
		glDrawElements(GL_TRIANGLES, g.indexCount, GL_UNSIGNED_INT, 0);
		trianglesPerDraw.push_back(g.indexCount / 3);
	}
	glBindVertexArray(0);
	glActiveTexture(GL_TEXTURE0);
}
