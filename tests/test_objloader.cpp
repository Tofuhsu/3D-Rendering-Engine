#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "objloader.h"

#include <cmath>
#include <sstream>

namespace {

ObjMesh Parse(const std::string& text, ObjLoadOptions options = ObjLoadOptions())
{
	ObjMesh mesh;
	std::istringstream in(text);
	REQUIRE(ParseObj(in, mesh, options));
	return mesh;
}

size_t TotalIndices(const ObjMesh& mesh)
{
	size_t n = 0;
	for (const ObjGroup& g : mesh.groups)
		n += g.indices.size();
	return n;
}

bool AllIndicesInRange(const ObjMesh& mesh)
{
	for (const ObjGroup& g : mesh.groups)
		for (uint32_t i : g.indices)
			if (i >= mesh.vertices.size())
				return false;
	return true;
}

bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f)
{
	return glm::length(a - b) < eps;
}

const char* kTriangle =
	"v 0 0 0\n"
	"v 1 0 0\n"
	"v 0 1 0\n"
	"vt 0 0\n"
	"vt 1 0\n"
	"vt 0 1\n"
	"vn 0 0 1\n"
	"f 1/1/1 2/2/1 3/3/1\n";

// Unit cube: 8 positions, 6 normals, 6 quads written as v//vn.
const char* kCubeWithNormals =
	"v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
	"vn 0 0 -1\nvn 0 0 1\nvn 0 -1 0\nvn 0 1 0\nvn -1 0 0\nvn 1 0 0\n"
	"f 1//1 4//1 3//1 2//1\n"
	"f 5//2 6//2 7//2 8//2\n"
	"f 1//3 2//3 6//3 5//3\n"
	"f 4//4 8//4 7//4 3//4\n"
	"f 1//5 5//5 8//5 4//5\n"
	"f 2//6 3//6 7//6 6//6\n";

}

TEST_CASE("triangle with position/texcoord/normal")
{
	ObjMesh m = Parse(kTriangle);
	CHECK(m.vertices.size() == 3);
	CHECK(m.stats.triangles == 1);
	REQUIRE(m.groups.size() == 1);
	CHECK(m.groups[0].indices == std::vector<uint32_t>{0, 1, 2});
	CHECK(Near(m.vertices[1].position, glm::vec3(1, 0, 0)));
	CHECK(m.vertices[2].texcoord == glm::vec2(0, 1));
	CHECK(Near(m.vertices[0].normal, glm::vec3(0, 0, 1)));
	CHECK(m.stats.generatedNormals == 0);
}

TEST_CASE("quad and n-gon are fan triangulated")
{
	ObjMesh m = Parse("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv -1 1 0\nf 1 2 3 4\nf 1 2 3 4 5\n");
	CHECK(m.stats.faces == 2);
	CHECK(m.stats.triangles == 2 + 3);
	CHECK(m.groups[0].indices == std::vector<uint32_t>{0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3, 0, 3, 4});
}

TEST_CASE("all face index forms: v, v/vt, v//vn, v/vt/vn")
{
	ObjMesh m = Parse(
		"v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0.5 0.5\nvn 0 0 1\n"
		"f 1 2 3\n"
		"f 1/1 2/1 3/1\n"
		"f 1//1 2//1 3//1\n"
		"f 1/1/1 2/1/1 3/1/1\n");
	CHECK(m.stats.faces == 4);
	CHECK(m.stats.skippedFaces == 0);
	// Four different (v, vt, vn) combinations per position -> 12 unique vertices.
	CHECK(m.vertices.size() == 12);
	CHECK(AllIndicesInRange(m));
}

TEST_CASE("negative indices are relative to the current end")
{
	ObjMesh m = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\nv 5 5 5\nf -4 -3 -1\n");
	CHECK(m.stats.skippedFaces == 0);
	REQUIRE(m.vertices.size() == 4);
	CHECK(Near(m.vertices[3].position, glm::vec3(5, 5, 5)));
	CHECK(m.groups[0].indices == std::vector<uint32_t>{0, 1, 2, 0, 1, 3});
}

TEST_CASE("comments, CRLF, tabs and repeated spaces")
{
	ObjMesh m = Parse(
		"# header comment\r\n"
		"v\t0 0 0   # trailing comment\r\n"
		"v  1   0\t0\r\n"
		"\r\n"
		"v 0 1 0\r\n"
		"   f 1 2 3\r\n");
	CHECK(m.stats.positions == 3);
	CHECK(m.stats.triangles == 1);
	CHECK(m.stats.ignoredLines == 0);
}

TEST_CASE("deduplication shares cube corners")
{
	ObjMesh dedup = Parse(kCubeWithNormals);
	CHECK(dedup.stats.faceCorners == 24);
	CHECK(dedup.vertices.size() == 24);	// each corner has 3 normals -> 24 unique
	CHECK(dedup.stats.triangles == 12);

	ObjLoadOptions noDedup;
	noDedup.deduplicate = false;
	ObjMesh raw = Parse(kCubeWithNormals, noDedup);
	CHECK(raw.vertices.size() == 24);
	CHECK(TotalIndices(raw) == TotalIndices(dedup));

	// Without normals the 8 corners collapse to 8 vertices (vs 24 corners).
	ObjMesh positionsOnly = Parse(
		"v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
		"f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 1 5 8 4\nf 2 3 7 6\n");
	CHECK(positionsOnly.vertices.size() == 8);
	CHECK(positionsOnly.stats.faceCorners == 24);
}

TEST_CASE("missing normals are generated from faces")
{
	ObjMesh m = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
	CHECK(m.stats.generatedNormals == 3);
	for (const VertexPTN& v : m.vertices)
		CHECK(Near(v.normal, glm::vec3(0, 0, 1)));

	ObjLoadOptions off;
	off.generateNormals = false;
	ObjMesh raw = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", off);
	CHECK(raw.stats.generatedNormals == 0);
}

TEST_CASE("bad faces are skipped, not fatal")
{
	ObjMesh m = Parse(
		"v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\n"
		"f 1 2 4\n"			// position out of range
		"f 0 1 2\n"			// zero is not a valid OBJ index
		"f 1/9 2/9 3/9\n"	// texcoord out of range
		"f 1//2 2//1 3//1\n"	// normal out of range
		"f 1 2\n"			// fewer than 3 corners
		"f a b c\n"			// not numbers
		"f 1/1/1/1 2 3\n"	// too many slashes
		"f -4 -3 -1\n"		// relative index before the start
		"f 1 2 3\n");		// the only valid face
	CHECK(m.stats.skippedFaces == 8);
	CHECK(m.stats.faces == 1);
	CHECK(m.vertices.size() == 3);
	CHECK(AllIndicesInRange(m));
}

TEST_CASE("malformed numbers keep vertex numbering intact")
{
	ObjMesh m = Parse("v 0 0 0\nv x y z\nv 0 1 0\nv nan 0 0\nv 1e99 0 0\nf 1 2 3\n");
	CHECK(m.stats.positions == 5);
	CHECK(m.stats.ignoredLines == 3);
	CHECK(m.stats.triangles == 1);
	CHECK(Near(m.vertices[1].position, glm::vec3(0, 0, 0)));
}

TEST_CASE("empty and garbage input")
{
	CHECK(Parse("").vertices.empty());
	ObjMesh g = Parse("\x01\x02\xff garbage\nf\nv\nvt\nvn\nusemtl\nmtllib\n");
	CHECK(g.vertices.empty());
	CHECK(g.stats.faces == 0);
}

TEST_CASE("'#' inside a name is not a comment")
{
	ObjMesh m = Parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl Material_#36 # real comment\nf 1 2 3\n");
	REQUIRE(m.groups.size() == 1);
	CHECK(m.groups[0].materialName == "Material_#36");

	std::vector<ObjMaterial> mats;
	std::istringstream in("newmtl Material_#36\nKd 1 1 1\n");
	ParseMtl(in, "", mats);
	REQUIRE(mats.size() == 1);
	CHECK(mats[0].name == "Material_#36");
}

TEST_CASE("usemtl groups and material linking")
{
	ObjMesh m = Parse(
		"mtllib a.mtl b.mtl\n"
		"v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
		"f 1 2 3\n"				// before any usemtl -> default group
		"usemtl Red\nf 2 4 3\n"
		"usemtl Blue\nf 1 2 4\n"
		"usemtl Red\nf 1 3 4\n");	// appends to the existing Red group
	CHECK(m.materialLibraries == std::vector<std::string>{"a.mtl", "b.mtl"});
	REQUIRE(m.groups.size() == 3);
	CHECK(m.groups[0].materialName.empty());
	CHECK(m.groups[1].materialName == "Red");
	CHECK(m.groups[1].indices.size() == 6);
	CHECK(m.groups[2].indices.size() == 3);

	std::istringstream mtl("newmtl Red\nKd 1 0 0\n");
	REQUIRE(ParseMtl(mtl, "", m.materials));
	LinkMaterials(m);
	CHECK(m.materials[(size_t)m.groups[1].materialIndex].Kd == glm::vec3(1, 0, 0));
	// "Blue" and the unnamed group were not in the MTL: they get defaults.
	CHECK(m.materials.size() == 3);
	CHECK(m.materials[(size_t)m.groups[2].materialIndex].name == "Blue");
	CHECK(m.materials[(size_t)m.groups[2].materialIndex].Kd == glm::vec3(0.8f));
}

TEST_CASE("MTL values, defaults and texture paths")
{
	std::vector<ObjMaterial> mats;
	std::istringstream in(
		"Kd 9 9 9\n"	// before newmtl: ignored
		"newmtl A\r\n"
		"Ka 0.1 0.2 0.3\nKd 0.4 0.5 0.6\nKs 1 1 1\nNs 32\n"
		"map_Kd -s 1 1 1 textures\\body.png\n"
		"newmtl B\n"
		"Kd 1 2\n");	// malformed: keeps the default
	REQUIRE(ParseMtl(in, "assets/Model/", mats));
	REQUIRE(mats.size() == 2);
	CHECK(mats[0].name == "A");
	CHECK(Near(mats[0].Ka, glm::vec3(0.1f, 0.2f, 0.3f)));
	CHECK(Near(mats[0].Kd, glm::vec3(0.4f, 0.5f, 0.6f)));
	CHECK(mats[0].Ns == doctest::Approx(32.0));
	CHECK(mats[0].mapKd == "assets/Model/textures/body.png");
	CHECK(mats[1].Kd == glm::vec3(0.8f));
	CHECK(mats[1].mapKd.empty());
}

TEST_CASE("zero Kd/Ka with a texture means texture only (Maya exports)")
{
	std::vector<ObjMaterial> mats;
	std::istringstream in(
		"newmtl Textured\nKd 0 0 0\nKa 0 0 0\nmap_Kd a.jpg\n"
		"newmtl Black\nKd 0 0 0\nKa 0 0 0\n");
	ParseMtl(in, "", mats);
	REQUIRE(mats.size() == 2);
	CHECK(mats[0].Kd == glm::vec3(1.0f));
	CHECK(mats[0].Ka == glm::vec3(1.0f));
	CHECK(mats[1].Kd == glm::vec3(0.0f));	// untextured black stays black
}

TEST_CASE("illum 3-7 turns on reflection with Ks as its strength")
{
	std::vector<ObjMaterial> mats;
	std::istringstream in(
		"newmtl Chrome\nKs 0.9 0.8 0.7\nillum 3\n"
		"newmtl Plastic\nKs 0.5 0.5 0.5\nillum 2\n"
		"newmtl Over\nKs 2 2 2\nillum 5\n");
	ParseMtl(in, "", mats);
	REQUIRE(mats.size() == 3);
	CHECK(mats[0].illum == 3);
	CHECK(mats[0].reflectivity == doctest::Approx(0.9));
	CHECK(mats[1].reflectivity == 0.0f);
	CHECK(mats[2].reflectivity == 1.0f);	// clamped
}

TEST_CASE("normalization centers the mesh and scales the longest side to 1")
{
	ObjMesh m = Parse("v 2 2 2\nv 6 2 2\nv 2 4 3\nf 1 2 3\n");
	NormalizeToUnitBox(m);
	glm::vec3 lo, hi;
	ComputeBounds(m, lo, hi);
	CHECK(Near(0.5f * (lo + hi), glm::vec3(0.0f)));
	CHECK((hi - lo).x == doctest::Approx(1.0));
	CHECK((hi - lo).y == doctest::Approx(0.5));
}

TEST_CASE("repository models load with consistent counts")
{
	struct Expected { const char* path; size_t triangles; size_t subMeshes; };
	// Triangle and material counts cross-checked with an independent awk count.
	const Expected models[] = {
		{"assets/TexCube/TexCube.obj", 12, 1},
		{"assets/Rose/Rose.obj", 166844, 3},
		{"assets/third_party/cornell_box/CornellBox-Original.obj", 36, 8},
		{"assets/third_party/cornell_box/CornellBox-Sphere.obj", 2188, 8},
		{"assets/third_party/mori_knob/testObj.obj", 11970, 5},
	};
	for (const Expected& e : models) {
		const std::string path = std::string(ENGINE_SOURCE_DIR) + "/" + e.path;
		CAPTURE(path);
		ObjMesh m;
		REQUIRE(LoadObjFile(path, m));
		LoadObjMaterials(path, m);
		CHECK(m.stats.triangles == e.triangles);
		CHECK(m.stats.skippedFaces == 0);
		size_t nonEmpty = 0;
		for (const ObjGroup& g : m.groups)
			nonEmpty += g.indices.empty() ? 0 : 1;
		CHECK(nonEmpty == e.subMeshes);
		CHECK(TotalIndices(m) == 3 * e.triangles);
		CHECK(AllIndicesInRange(m));
		CHECK(m.vertices.size() <= m.stats.faceCorners);
	}
}
