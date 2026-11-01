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

#ifndef ENGINE_LIBFUZZER

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <vector>

namespace {

const char* kTokens[] = {
	"v ", "vt ", "vn ", "f ", "usemtl ", "mtllib ", "newmtl ", "map_Kd ", "Kd ", "Ns ", "o ", "g ", "s ",
	"/", "//", " ", "\t", "\n", "\r\n", "#", "-", "0", "1", "-1", "2", "3", "-3",
	"2147483647", "2147483648", "-9223372036854775808", "99999999999999999999",
	"1e38", "1e39", "-1e-45", "nan", "inf", "0x1p3", "1.5", ".", "\\", "\0",
};

std::vector<uint8_t> Mutate(const std::vector<uint8_t>& in, const std::vector<std::vector<uint8_t>>& corpus,
							std::mt19937_64& rng)
{
	std::vector<uint8_t> out = in;
	const size_t maxSize = 64 * 1024;
	std::uniform_int_distribution<int> numOps(1, 8);
	for (int op = numOps(rng); op > 0; --op) {
		auto pos = [&](size_t extra) { return std::uniform_int_distribution<size_t>(0, out.size() + extra - 1)(rng); };
		switch (rng() % 6) {
		case 0:	// flip bits
			if (!out.empty()) out[pos(0)] ^= (uint8_t)(1u << (rng() % 8));
			break;
		case 1:	// random byte
			if (out.size() < maxSize) out.insert(out.begin() + pos(1), (uint8_t)rng());
			break;
		case 2:	// delete a range
			if (!out.empty()) {
				size_t a = pos(0);
				size_t len = std::min<size_t>(out.size() - a, 1 + rng() % 16);
				out.erase(out.begin() + a, out.begin() + a + len);
			}
			break;
		case 3: {	// insert an OBJ/MTL token
			const char* t = kTokens[rng() % (sizeof(kTokens) / sizeof(kTokens[0]))];
			std::string s = (*t == '\0') ? std::string(1, '\0') : std::string(t);
			if (out.size() + s.size() < maxSize) out.insert(out.begin() + pos(1), s.begin(), s.end());
			break;
		}
		case 4:	// duplicate a range (grows faces / vertex lists)
			if (!out.empty() && out.size() < maxSize / 2) {
				size_t a = pos(0);
				size_t len = std::min<size_t>(out.size() - a, 1 + rng() % 64);
				std::vector<uint8_t> chunk(out.begin() + a, out.begin() + a + len);
				out.insert(out.begin() + pos(1), chunk.begin(), chunk.end());
			}
			break;
		default: {	// splice with another corpus entry
			const std::vector<uint8_t>& other = corpus[rng() % corpus.size()];
			if (!other.empty() && out.size() + other.size() < maxSize) {
				size_t a = std::uniform_int_distribution<size_t>(0, other.size() - 1)(rng);
				out.insert(out.begin() + pos(1), other.begin() + a, other.end());
			}
			break;
		}
		}
	}
	return out;
}

}

int main(int argc, char** argv)
{
	long long iterations = 100000;
	double seconds = 0.0;
	uint64_t seed = 1;
	std::vector<std::vector<uint8_t>> corpus;

	auto addFile = [&](const std::filesystem::path& p) {
		std::ifstream ifs(p, std::ios::binary);
		corpus.emplace_back(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	};
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--iterations" && i + 1 < argc) iterations = std::atoll(argv[++i]);
		else if (arg == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
		else if (arg == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
		else if (std::filesystem::is_directory(arg)) {
			for (const auto& entry : std::filesystem::directory_iterator(arg))
				if (entry.is_regular_file()) addFile(entry.path());
		}
		else addFile(arg);
	}
	if (corpus.empty())
		corpus.push_back({});

	// Seeds first, unmodified.
	for (const auto& input : corpus)
		LLVMFuzzerTestOneInput(input.data(), input.size());

	std::mt19937_64 rng(seed);
	const auto start = std::chrono::steady_clock::now();
	long long done = 0;
	for (;; ++done) {
		const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (seconds > 0.0 ? elapsed >= seconds : done >= iterations)
			break;
		std::vector<uint8_t> input = Mutate(corpus[rng() % corpus.size()], corpus, rng);
		LLVMFuzzerTestOneInput(input.data(), input.size());
		// Keep some mutants so later rounds build on earlier ones.
		if (rng() % 64 == 0 && corpus.size() < 4096)
			corpus.push_back(std::move(input));
	}
	const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::cout << "fuzz_objloader: " << done << " inputs in " << elapsed << " s ("
			  << (long long)(done / std::max(elapsed, 1e-9)) << "/s), seed " << seed
			  << ", no invariant violations or sanitizer reports" << std::endl;
	return 0;
}

#endif
