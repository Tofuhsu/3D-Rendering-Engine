// Fuzz target for the OBJ/MTL parser.
//
// With ENGINE_LIBFUZZER (LLVM clang, -fsanitize=fuzzer) libFuzzer drives
// LLVMFuzzerTestOneInput. Apple clang ships no libFuzzer, so otherwise a small
// built-in driver mutates a seed corpus (byte flips, inserts, deletes, OBJ
// tokens, splices) and runs the same entry point; build with ENGINE_SANITIZE=ON
// so AddressSanitizer/UBSan catch memory errors.
//
//   fuzz_objloader [--iterations N | --seconds S] [--seed X] <corpus files or dirs>

#include "objloader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

void Check(bool ok, const char* what)
{
	if (!ok) {
		std::fprintf(stderr, "invariant violated: %s\n", what);
		std::abort();
	}
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	const std::string text(reinterpret_cast<const char*>(data), size);

	ObjMesh mesh;
	std::istringstream obj(text);
	ParseObj(obj, mesh);

	std::istringstream mtl(text);
	ParseMtl(mtl, "base/", mesh.materials);
	LinkMaterials(mesh);
	NormalizeToUnitBox(mesh);

	// Whatever the input, the output must be safe to upload and draw.
	size_t indices = 0;
	for (const ObjGroup& g : mesh.groups) {
		Check(g.indices.size() % 3 == 0, "index count is a multiple of 3");
		Check(g.materialIndex >= 0 && (size_t)g.materialIndex < mesh.materials.size(), "group has a material");
		for (uint32_t i : g.indices)
			Check(i < mesh.vertices.size(), "index in range");
		indices += g.indices.size();
	}
	Check(indices == 3 * mesh.stats.triangles, "triangle count matches indices");
	Check(mesh.vertices.size() <= mesh.stats.faceCorners, "no more vertices than face corners");
	return 0;
}
