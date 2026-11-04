#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "gltfloader.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace {

std::string Base64(const std::vector<unsigned char>& bytes)
{
	static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < bytes.size(); i += 3) {
		uint32_t n = (uint32_t)bytes[i] << 16;
		if (i + 1 < bytes.size()) n |= (uint32_t)bytes[i + 1] << 8;
		if (i + 2 < bytes.size()) n |= bytes[i + 2];
		out += table[(n >> 18) & 63];
		out += table[(n >> 12) & 63];
		out += i + 1 < bytes.size() ? table[(n >> 6) & 63] : '=';
		out += i + 2 < bytes.size() ? table[n & 63] : '=';
	}
	return out;
}

template <typename T>
void Append(std::vector<unsigned char>& buf, const std::vector<T>& values)
{
	const unsigned char* p = reinterpret_cast<const unsigned char*>(values.data());
	buf.insert(buf.end(), p, p + values.size() * sizeof(T));
}

// One triangle (positions + uint16 indices) in an embedded data: buffer.
// `nodes` and `mode` let tests vary the scene and primitive type.
std::string TriangleGltf(const std::string& nodes, int mode = 4, int maxIndex = 2,
						 const std::string& bufferUri = "")
{
	std::vector<unsigned char> buf;
	Append(buf, std::vector<float>{ 0, 0, 0, 1, 0, 0, 0, 1, 0 });	// 36 bytes
	Append(buf, std::vector<uint16_t>{ 0, 1, (uint16_t)maxIndex, 0 });	// 6 + 2 padding
	const std::string uri = bufferUri.empty() ? "data:application/octet-stream;base64," + Base64(buf) : bufferUri;
	return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)" + nodes + R"(,
		"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0,"mode":)" + std::to_string(mode) + R"(}]}],
		"materials":[{"name":"Red","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1],"metallicFactor":0.25,"roughnessFactor":0.75}}],
		"buffers":[{"byteLength":44,"uri":")" + uri + R"("}],
		"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
		"accessors":[
		  {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
		  {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
}

GltfModelData Load(const std::string& json, bool expectOk = true)
{
	GltfModelData model;
	std::string error;
	bool ok = LoadGltfMemory(json.data(), json.size(), "", model, error);
	CHECK_MESSAGE(ok == expectOk, error);
	return model;
}

bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) { return glm::length(a - b) < eps; }

std::string Asset(const std::string& relative)
{
	return std::string(ENGINE_SOURCE_DIR) + "/assets/third_party/gltf/" + relative;
}

bool Exists(const std::string& path) { return std::ifstream(path).good(); }

}

TEST_CASE("embedded triangle: material, generated normal and tangent")
{
	GltfModelData m = Load(TriangleGltf(R"("nodes":[{"mesh":0}])"));
	REQUIRE(m.vertices.size() == 3);
	CHECK(m.stats.triangles == 1);
	CHECK(m.stats.generatedNormals == 3);
	CHECK(m.stats.generatedTangents == 3);
	REQUIRE(m.materials.size() == 1);
	CHECK(m.materials[0].name == "Red");
	CHECK(m.materials[0].baseColorFactor == glm::vec4(1, 0, 0, 1));
	CHECK(m.materials[0].metallicFactor == doctest::Approx(0.25));
	CHECK(m.materials[0].roughnessFactor == doctest::Approx(0.75));
	CHECK(m.indicesByMaterial[0] == std::vector<uint32_t>{ 0, 1, 2 });
	for (const VertexPNTT& v : m.vertices) {
		CHECK(Near(v.normal, glm::vec3(0, 0, 1)));
		CHECK(std::fabs(glm::length(glm::vec3(v.tangent)) - 1.0f) < 1e-5f);
		CHECK(std::fabs(glm::dot(glm::vec3(v.tangent), v.normal)) < 1e-5f);
	}
}

TEST_CASE("node transforms are baked into the vertices")
{
	// Parent scales by 2, child translates by (10, 0, 0): world = S * T.
	GltfModelData m = Load(TriangleGltf(
		R"("nodes":[{"scale":[2,2,2],"children":[1]},{"translation":[10,0,0],"mesh":0}])"));
	REQUIRE(m.vertices.size() == 3);
	CHECK(Near(m.vertices[0].position, glm::vec3(20, 0, 0)));
	CHECK(Near(m.vertices[1].position, glm::vec3(22, 0, 0)));
	CHECK(Near(m.vertices[2].position, glm::vec3(20, 2, 0)));
	CHECK(m.stats.nodesWithMesh == 1);
}

TEST_CASE("mirroring transform flips winding-dependent data consistently")
{
	GltfModelData m = Load(TriangleGltf(R"("nodes":[{"scale":[-1,1,1],"mesh":0}])"));
	REQUIRE(m.vertices.size() == 3);
	// Generated normals follow the mirrored geometry (winding is now clockwise).
	CHECK(Near(m.vertices[0].normal, glm::vec3(0, 0, -1)));
}

TEST_CASE("unsupported or broken primitives are skipped")
{
	CHECK(Load(TriangleGltf(R"("nodes":[{"mesh":0}])", 0)).stats.skippedPrimitives == 1);	// POINTS
	// Index 3 is past the 3 vertices: cgltf_validate rejects it.
	Load(TriangleGltf(R"("nodes":[{"mesh":0}])", 4, 3), false);
}

TEST_CASE("external buffers need a base directory")
{
	Load(TriangleGltf(R"("nodes":[{"mesh":0}])", 4, 2, "missing.bin"), false);
}

TEST_CASE("garbage input is rejected without crashing")
{
	const char* inputs[] = { "", "{", "null", "glTF\x02\x00\x00\x00", R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}]})" };
	for (const char* s : inputs) {
		GltfModelData m;
		std::string error;
		LoadGltfMemory(s, std::strlen(s), "", m, error);	// may succeed or fail, must not crash
		CHECK(m.stats.triangles == 0);
	}
}

TEST_CASE("generated tangents follow +u and detect mirrored UVs")
{
	std::vector<VertexPNTT> v(3);
	v[0].position = { 0, 0, 0 }; v[1].position = { 1, 0, 0 }; v[2].position = { 0, 1, 0 };
	for (VertexPNTT& x : v) x.normal = { 0, 0, 1 };
	v[0].texcoord = { 0, 0 }; v[1].texcoord = { 1, 0 }; v[2].texcoord = { 0, 1 };
	GenerateTangents(v, { 0, 1, 2 }, 0, 3);
	CHECK(Near(glm::vec3(v[0].tangent), glm::vec3(1, 0, 0)));
	CHECK(v[0].tangent.w == 1.0f);

	v[1].texcoord = { -1, 0 };	// u runs the other way
	GenerateTangents(v, { 0, 1, 2 }, 0, 3);
	CHECK(Near(glm::vec3(v[0].tangent), glm::vec3(-1, 0, 0)));
	CHECK(v[0].tangent.w == -1.0f);
}

TEST_CASE("downloaded Khronos sample models (skipped if not fetched)")
{
	struct Expected { const char* file; size_t triangles; size_t images; bool hasTangents; };
	// Triangle counts match the index accessors read directly from each file.
	const Expected models[] = {
		{ "DamagedHelmet.glb", 15452, 5, false },
		{ "SciFiHelmet/SciFiHelmet.gltf", 23358, 4, true },
		{ "Lantern.glb", 5394, 4, true },
		{ "NormalTangentMirrorTest.glb", 5240, 3, true },
	};
	for (const Expected& e : models) {
		const std::string path = Asset(e.file);
		CAPTURE(path);
		if (!Exists(path)) {
			MESSAGE("not found, run tools/fetch_assets.sh: " << path);
			continue;
		}
		GltfModelData m;
		std::string error;
		REQUIRE_MESSAGE(LoadGltfFile(path, m, error), error);
		CHECK(m.stats.triangles == e.triangles);
		CHECK(m.stats.skippedPrimitives == 0);
		CHECK(m.stats.images == e.images);
		CHECK(m.stats.failedImages == 0);
		CHECK((m.stats.generatedTangents == 0) == e.hasTangents);
		size_t indices = 0;
		for (const auto& list : m.indicesByMaterial) {
			indices += list.size();
			for (uint32_t i : list)
				REQUIRE(i < m.vertices.size());
		}
		CHECK(indices == 3 * e.triangles);
		for (const VertexPNTT& v : m.vertices) {
			REQUIRE(std::fabs(v.tangent.w) == 1.0f);
			REQUIRE(std::fabs(glm::length(v.normal) - 1.0f) < 1e-3f);
		}
		for (const GltfImage& img : m.images)
			CHECK(img.rgba.size() == (size_t)img.width * img.height * 4);
	}
}
