#include "gltfloader.h"

#include <cgltf.h>
#include <gtc/type_ptr.hpp>
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace {

std::string DirectoryOf(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// Decode one glTF image (embedded buffer view, data: URI or external file).
bool DecodeImage(const cgltf_image& image, const std::string& baseDir, GltfImage& out)
{
	std::vector<unsigned char> encoded;
	if (image.buffer_view != nullptr) {
		const uint8_t* bytes = cgltf_buffer_view_data(image.buffer_view);
		if (bytes == nullptr)
			return false;
		encoded.assign(bytes, bytes + image.buffer_view->size);
	} else if (image.uri != nullptr && std::strncmp(image.uri, "data:", 5) == 0) {
		const char* comma = std::strchr(image.uri, ',');
		if (comma == nullptr || std::strstr(image.uri, ";base64,") == nullptr)
			return false;
		const char* b64 = comma + 1;
		size_t len = std::strlen(b64);
		size_t size = len / 4 * 3;
		if (len >= 1 && b64[len - 1] == '=') --size;
		if (len >= 2 && b64[len - 2] == '=') --size;
		cgltf_options options = {};
		void* decoded = nullptr;
		if (cgltf_load_buffer_base64(&options, size, b64, &decoded) != cgltf_result_success)
			return false;
		encoded.assign((unsigned char*)decoded, (unsigned char*)decoded + size);
		std::free(decoded);
	} else if (image.uri != nullptr && !baseDir.empty()) {
		std::string uri = image.uri;
		uri.resize(cgltf_decode_uri(&uri[0]));	// %20 and friends
		std::ifstream ifs(baseDir + uri, std::ios::binary);
		if (!ifs)
			return false;
		encoded.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	} else {
		return false;
	}

	// glTF images are top row first and UV (0,0) is the top-left: no flip.
	stbi_set_flip_vertically_on_load(0);
	int w = 0, h = 0, channels = 0;
	unsigned char* pixels = stbi_load_from_memory(encoded.data(), (int)encoded.size(), &w, &h, &channels, 4);
	if (pixels == nullptr)
		return false;
	out.width = w;
	out.height = h;
	out.rgba.assign(pixels, pixels + (size_t)w * h * 4);
	stbi_image_free(pixels);
	return true;
}

int ImageIndex(const cgltf_data* data, const cgltf_texture* texture)
{
	if (texture == nullptr || texture->image == nullptr)
		return -1;
	return (int)(texture->image - data->images);
}

GltfMaterial ConvertMaterial(const cgltf_data* data, const cgltf_material& m)
{
	GltfMaterial out;
	out.name = m.name ? m.name : "";
	if (m.has_pbr_metallic_roughness) {
		const cgltf_pbr_metallic_roughness& pbr = m.pbr_metallic_roughness;
		out.baseColorFactor = glm::make_vec4(pbr.base_color_factor);
		out.metallicFactor = pbr.metallic_factor;
		out.roughnessFactor = pbr.roughness_factor;
		out.baseColorImage = ImageIndex(data, pbr.base_color_texture.texture);
		out.metallicRoughnessImage = ImageIndex(data, pbr.metallic_roughness_texture.texture);
	}
	out.normalImage = ImageIndex(data, m.normal_texture.texture);
	out.normalScale = m.normal_texture.texture ? m.normal_texture.scale : 1.0f;
	out.occlusionImage = ImageIndex(data, m.occlusion_texture.texture);
	out.occlusionStrength = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1.0f;
	out.emissiveImage = ImageIndex(data, m.emissive_texture.texture);
	out.emissiveFactor = glm::make_vec3(m.emissive_factor);
	out.alphaBlendOrMask = m.alpha_mode != cgltf_alpha_mode_opaque;
	return out;
}

struct Builder
{
	const cgltf_data* data;
	GltfModelData& out;
	size_t defaultMaterial = (size_t)-1;

	size_t MaterialSlot(const cgltf_material* m)
	{
		if (m != nullptr)
			return (size_t)(m - data->materials);
		if (defaultMaterial == (size_t)-1) {
			GltfMaterial fallback;
			fallback.name = "(default)";
			fallback.metallicFactor = 0.0f;
			fallback.roughnessFactor = 0.5f;
			out.materials.push_back(fallback);
			out.indicesByMaterial.emplace_back();
			defaultMaterial = out.materials.size() - 1;
		}
		return defaultMaterial;
	}

	void AddPrimitive(const cgltf_primitive& prim, const glm::mat4& world)
	{
		++out.stats.primitives;
		const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tan = nullptr, *uv = nullptr;
		for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
			const cgltf_attribute& attr = prim.attributes[a];
			if (attr.type == cgltf_attribute_type_position) pos = attr.data;
			else if (attr.type == cgltf_attribute_type_normal) nrm = attr.data;
			else if (attr.type == cgltf_attribute_type_tangent) tan = attr.data;
			else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uv = attr.data;
		}
		if (prim.type != cgltf_primitive_type_triangles || pos == nullptr || pos->count == 0) {
			++out.stats.skippedPrimitives;
			return;
		}
		const size_t count = pos->count;
		if ((nrm && nrm->count != count) || (tan && tan->count != count) || (uv && uv->count != count)) {
			++out.stats.skippedPrimitives;
			return;
		}

		// Indices (or 0..n-1), validated before anything is added.
		std::vector<uint32_t> local;
		if (prim.indices != nullptr) {
			local.resize(prim.indices->count);
			for (cgltf_size i = 0; i < prim.indices->count; ++i) {
				cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
				if (idx >= count) {
					++out.stats.skippedPrimitives;
					return;
				}
				local[i] = (uint32_t)idx;
			}
		} else {
			local.resize(count);
			for (size_t i = 0; i < count; ++i) local[i] = (uint32_t)i;
		}
		local.resize(local.size() / 3 * 3);

		const size_t first = out.vertices.size();
		out.vertices.resize(first + count);
		const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
		const float handedness = glm::determinant(glm::mat3(world)) < 0.0f ? -1.0f : 1.0f;
		for (size_t i = 0; i < count; ++i) {
			VertexPNTT& v = out.vertices[first + i];
			float f[4] = { 0, 0, 0, 1 };
			cgltf_accessor_read_float(pos, i, f, 3);
			v.position = glm::vec3(world * glm::vec4(f[0], f[1], f[2], 1.0f));
			if (nrm) {
				cgltf_accessor_read_float(nrm, i, f, 3);
				glm::vec3 n = normalMatrix * glm::vec3(f[0], f[1], f[2]);
				float len = glm::length(n);
				v.normal = len > 0.0f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
			}
			if (uv) {
				cgltf_accessor_read_float(uv, i, f, 2);
				v.texcoord = glm::vec2(f[0], f[1]);
			}
			if (tan) {
				float t[4] = { 1, 0, 0, 1 };
				cgltf_accessor_read_float(tan, i, t, 4);
				glm::vec3 tw = glm::mat3(world) * glm::vec3(t[0], t[1], t[2]);
				float len = glm::length(tw);
				v.tangent = glm::vec4(len > 0.0f ? tw / len : glm::vec3(1, 0, 0), (t[3] < 0.0f ? -1.0f : 1.0f) * handedness);
			}
		}

		if (!nrm) {
			// Area-weighted face normals.
			std::vector<glm::vec3> acc(count, glm::vec3(0.0f));
			for (size_t i = 0; i + 2 < local.size(); i += 3) {
				const glm::vec3& a = out.vertices[first + local[i]].position;
				const glm::vec3& b = out.vertices[first + local[i + 1]].position;
				const glm::vec3& c = out.vertices[first + local[i + 2]].position;
				glm::vec3 n = glm::cross(b - a, c - a);
				acc[local[i]] += n; acc[local[i + 1]] += n; acc[local[i + 2]] += n;
			}
			for (size_t i = 0; i < count; ++i) {
				float len = glm::length(acc[i]);
				out.vertices[first + i].normal = len > 0.0f ? acc[i] / len : glm::vec3(0.0f, 1.0f, 0.0f);
			}
			out.stats.generatedNormals += count;
		}
		if (!tan) {
			std::vector<uint32_t> shifted(local.size());
			for (size_t i = 0; i < local.size(); ++i) shifted[i] = local[i] + (uint32_t)first;
			GenerateTangents(out.vertices, shifted, first, count);
			out.stats.generatedTangents += count;
		}

		std::vector<uint32_t>& target = out.indicesByMaterial[MaterialSlot(prim.material)];
		for (uint32_t idx : local)
			target.push_back(idx + (uint32_t)first);
		out.stats.triangles += local.size() / 3;
	}

	void VisitNode(const cgltf_node* node, const glm::mat4& parent, int depth)
	{
		if (depth > 64)
			return;	// guards against malformed (cyclic) hierarchies
		float m[16];
		cgltf_node_transform_local(node, m);
		const glm::mat4 world = parent * glm::make_mat4(m);
		if (node->mesh != nullptr) {
			++out.stats.nodesWithMesh;
			for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
				AddPrimitive(node->mesh->primitives[p], world);
		}
		for (cgltf_size c = 0; c < node->children_count; ++c)
			VisitNode(node->children[c], world, depth + 1);
	}
};

bool Convert(cgltf_data* data, const std::string& baseDir, GltfModelData& out, std::string& error,
			 const GltfLoadOptions& options)
{
	cgltf_options cgOptions = {};
	// Refuse external files when no base directory is given (memory loads).
	for (cgltf_size b = 0; b < data->buffers_count; ++b) {
		const char* uri = data->buffers[b].uri;
		if (uri != nullptr && std::strncmp(uri, "data:", 5) != 0 && baseDir.empty()) {
			error = "external buffer not allowed without a base directory";
			return false;
		}
	}
	const std::string pathForBuffers = baseDir + "model.gltf";	// cgltf resolves URIs against its folder
	if (cgltf_load_buffers(&cgOptions, data, pathForBuffers.c_str()) != cgltf_result_success) {
		error = "cannot load buffers";
		return false;
	}
	if (cgltf_validate(data) != cgltf_result_success) {
		error = "glTF validation failed";
		return false;
	}

	out = GltfModelData();
	for (cgltf_size e = 0; e < data->extensions_used_count; ++e)
		out.stats.extensionsUsed.push_back(data->extensions_used[e]);
	for (cgltf_size m = 0; m < data->materials_count; ++m) {
		out.materials.push_back(ConvertMaterial(data, data->materials[m]));
		out.indicesByMaterial.emplace_back();
	}

	Builder builder{ data, out };
	const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
	if (scene != nullptr) {
		for (cgltf_size n = 0; n < scene->nodes_count; ++n)
			builder.VisitNode(scene->nodes[n], glm::mat4(1.0f), 0);
	} else {
		for (cgltf_size n = 0; n < data->nodes_count; ++n)
			if (data->nodes[n].parent == nullptr)
				builder.VisitNode(&data->nodes[n], glm::mat4(1.0f), 0);
	}

	if (options.decodeImages) {
		const auto t0 = std::chrono::steady_clock::now();
		out.images.resize(data->images_count);
		for (cgltf_size i = 0; i < data->images_count; ++i) {
			out.images[i].name = data->images[i].name ? data->images[i].name
								: (data->images[i].uri && std::strncmp(data->images[i].uri, "data:", 5) ? data->images[i].uri : "");
			if (DecodeImage(data->images[i], baseDir, out.images[i]))
				++out.stats.images;
			else
				++out.stats.failedImages;
		}
		out.stats.imageDecodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}
	return true;
}

}

bool LoadGltfMemory(const void* bytes, size_t size, const std::string& baseDir, GltfModelData& out,
					std::string& error, const GltfLoadOptions& options)
{
	const auto t0 = std::chrono::steady_clock::now();
	cgltf_options cgOptions = {};
	cgltf_data* data = nullptr;
	if (cgltf_parse(&cgOptions, bytes, size, &data) != cgltf_result_success) {
		error = "cannot parse glTF";
		return false;
	}
	bool ok = Convert(data, baseDir, out, error, options);
	cgltf_free(data);
	const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	out.stats.parseMs = totalMs - out.stats.imageDecodeMs;
	return ok;
}

bool LoadGltfFile(const std::string& path, GltfModelData& out, std::string& error, const GltfLoadOptions& options)
{
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		error = "cannot open " + path;
		return false;
	}
	std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	std::string dir = DirectoryOf(path);
	if (dir.empty())
		dir = "./";
	return LoadGltfMemory(bytes.data(), bytes.size(), dir, out, error, options);
}

void NormalizeToUnitBox(GltfModelData& model)
{
	if (model.vertices.empty())
		return;
	glm::vec3 lo = model.vertices[0].position, hi = lo;
	for (const VertexPNTT& v : model.vertices) {
		lo = glm::min(lo, v.position);
		hi = glm::max(hi, v.position);
	}
	const glm::vec3 center = 0.5f * (lo + hi);
	const glm::vec3 extent = hi - lo;
	const float longest = std::max(extent.x, std::max(extent.y, extent.z));
	const float scale = longest > 0.0f ? 1.0f / longest : 1.0f;
	for (VertexPNTT& v : model.vertices)
		v.position = (v.position - center) * scale;
}

void GenerateTangents(std::vector<VertexPNTT>& vertices, const std::vector<uint32_t>& indices,
					  size_t firstVertex, size_t vertexCount)
{
	std::vector<glm::vec3> tan(vertexCount, glm::vec3(0.0f)), bitan(vertexCount, glm::vec3(0.0f));
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
		const VertexPNTT &a = vertices[i0], &b = vertices[i1], &c = vertices[i2];
		const glm::vec3 e1 = b.position - a.position, e2 = c.position - a.position;
		const glm::vec2 d1 = b.texcoord - a.texcoord, d2 = c.texcoord - a.texcoord;
		const float det = d1.x * d2.y - d2.x * d1.y;
		if (std::fabs(det) < 1e-12f)
			continue;	// degenerate UVs
		const float r = 1.0f / det;
		const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
		const glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * r;
		for (uint32_t idx : { i0, i1, i2 }) {
			tan[idx - firstVertex] += t;
			bitan[idx - firstVertex] += bt;
		}
	}
	for (size_t i = 0; i < vertexCount; ++i) {
		VertexPNTT& v = vertices[firstVertex + i];
		const glm::vec3& n = v.normal;
		glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);	// Gram-Schmidt
		float len = glm::length(t);
		if (len < 1e-8f) {
			// No usable UV gradient: any vector perpendicular to the normal.
			t = std::fabs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0));
			len = glm::length(t);
		}
		t /= len;
		const float w = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
		v.tangent = glm::vec4(t, w);
	}
}
