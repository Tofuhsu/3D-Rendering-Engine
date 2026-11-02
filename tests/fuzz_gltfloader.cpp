// Fuzz target for the glTF loader (cgltf parsing, accessor conversion, node
// traversal, tangent generation, embedded image decoding). Memory loads only:
// external URIs are refused, so the fuzzer never touches the file system.

#include "gltfloader.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>

namespace {

// How many inputs got past parsing and validation, printed at exit, to show how
// deep byte-level mutation of JSON/GLB actually reaches.
struct Counters
{
	long long total = 0, loaded = 0, withTriangles = 0;
	~Counters()
	{
		if (total > 0)
			std::fprintf(stderr, "glTF inputs: %lld total, %lld loaded (%.2f%%), %lld with triangles\n",
						 total, loaded, 100.0 * loaded / total, withTriangles);
	}
} counters;

void Check(bool ok, const char* what)
{
	if (!ok) {
		std::fprintf(stderr, "invariant violated: %s\n", what);
		std::abort();
	}
}

}

// Structure-aware mode: the input chooses accessor counts, component types,
// offsets, node transforms and supplies the binary chunk; the JSON stays
// well-formed, so mutations reach accessor reading, index validation, tangent
// generation and image decoding instead of dying in the JSON parser.
std::string BuildGlb(const uint8_t* data, size_t size)
{
	auto byte = [&](size_t i) -> unsigned { return i < size ? data[i] : 0u; };
	const unsigned vertexCount = 1 + byte(1) % 48;
	const unsigned indexCount = byte(2) % 160;
	static const int kIndexTypes[] = { 5121, 5123, 5125 };
	static const int kIndexSizes[] = { 1, 2, 4 };
	const int it = byte(3) % 3;
	const bool withUv = byte(4) & 1, withNormal = byte(4) & 2, withTangent = byte(4) & 4, withImage = byte(4) & 8;
	const int mode = (byte(4) >> 4) % 8;	// 4 = triangles; others are skipped
	const float scale = (float)((int)byte(5) - 128) / 16.0f;
	const size_t header = 6;
	std::string bin(data + std::min(size, header), data + size);

	// Lay out the views back to back; the BIN chunk is padded with zeros if short.
	size_t offset = 0;
	auto view = [&](size_t length) {
		std::string v = "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(length) + "}";
		offset += (length + 3) / 4 * 4;
		return v;
	};
	std::string views = view(vertexCount * 12);
	std::string accessors = "{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(vertexCount) + ",\"type\":\"VEC3\"}";
	std::string attributes = "\"POSITION\":0";
	int next = 1;
	auto addAttribute = [&](const char* name, const char* type, int components) {
		views += "," + view(vertexCount * 4 * components);
		accessors += ",{\"bufferView\":" + std::to_string(next) + ",\"componentType\":5126,\"count\":"
			+ std::to_string(vertexCount) + ",\"type\":\"" + type + "\"}";
		attributes += std::string(",\"") + name + "\":" + std::to_string(next);
		++next;
	};
	if (withNormal) addAttribute("NORMAL", "VEC3", 3);
	if (withUv) addAttribute("TEXCOORD_0", "VEC2", 2);
	if (withTangent) addAttribute("TANGENT", "VEC4", 4);
	std::string indices;
	if (indexCount > 0) {
		views += "," + view(indexCount * kIndexSizes[it]);
		accessors += ",{\"bufferView\":" + std::to_string(next) + ",\"componentType\":" + std::to_string(kIndexTypes[it])
			+ ",\"count\":" + std::to_string(indexCount) + ",\"type\":\"SCALAR\"}";
		indices = ",\"indices\":" + std::to_string(next);
		++next;
	}
	std::string images, textures, material = "{}";
	if (withImage) {
		const size_t imageLength = bin.size() > offset ? bin.size() - offset : 0;
		views += "," + view(std::max<size_t>(imageLength, 1));
		images = ",\"images\":[{\"bufferView\":" + std::to_string(next) + ",\"mimeType\":\"image/png\"}]";
		textures = ",\"textures\":[{\"source\":0}]";
		material = "{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}},\"normalTexture\":{\"index\":0}}";
	}
	if (bin.size() < offset)
		bin.resize(offset, '\0');
	while (bin.size() % 4) bin.push_back('\0');

	std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
		"\"nodes\":[{\"scale\":[" + std::to_string(scale) + ",1,1],\"children\":[1]},{\"mesh\":0}],"
		"\"meshes\":[{\"primitives\":[{\"attributes\":{" + attributes + "}" + indices + ",\"material\":0,\"mode\":" + std::to_string(mode) + "}]}],"
		"\"materials\":[" + material + "]" + textures + images + ","
		"\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
		"\"bufferViews\":[" + views + "],\"accessors\":[" + accessors + "]}";
	while (json.size() % 4) json.push_back(' ');

	auto u32 = [](uint32_t v) { return std::string(reinterpret_cast<const char*>(&v), 4); };
	return "glTF" + u32(2) + u32((uint32_t)(12 + 8 + json.size() + 8 + bin.size()))
		+ u32((uint32_t)json.size()) + "JSON" + json + u32((uint32_t)bin.size()) + std::string("BIN\0", 4) + bin;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	GltfModelData model;
	std::string error;
	++counters.total;
	// Raw mode (the input is the file) when the first byte is even, else structured.
	std::string glb;
	if (size > 0 && (data[0] & 1)) {
		glb = BuildGlb(data, size);
		data = reinterpret_cast<const uint8_t*>(glb.data());
		size = glb.size();
	}
	if (!LoadGltfMemory(data, size, "", model, error))
		return 0;
	++counters.loaded;
	counters.withTriangles += model.stats.triangles > 0 ? 1 : 0;
	NormalizeToUnitBox(model);

	Check(model.indicesByMaterial.size() == model.materials.size(), "one index list per material");
	size_t indices = 0;
	for (const auto& list : model.indicesByMaterial) {
		Check(list.size() % 3 == 0, "index count is a multiple of 3");
		for (uint32_t i : list)
			Check(i < model.vertices.size(), "index in range");
		indices += list.size();
	}
	Check(indices == 3 * model.stats.triangles, "triangle count matches indices");
	for (const VertexPNTT& v : model.vertices)
		Check(v.tangent.w == 1.0f || v.tangent.w == -1.0f, "tangent handedness is +-1");
	for (const GltfMaterial& m : model.materials) {
		for (int img : { m.baseColorImage, m.metallicRoughnessImage, m.normalImage, m.occlusionImage, m.emissiveImage })
			Check(img < (int)model.images.size(), "image index in range");
	}
	for (const GltfImage& img : model.images)
		Check(img.rgba.size() == (size_t)img.width * img.height * 4, "image size matches pixels");
	return 0;
}
