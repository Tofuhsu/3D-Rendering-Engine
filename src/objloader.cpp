#include "objloader.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace {

// Split a line into whitespace-separated tokens, dropping a trailing comment.
// A comment starts only at the beginning of a token, so names such as
// "Material_#35" (3ds Max exports) survive. Handles tabs, repeated spaces and CRLF.
void Tokenize(const std::string& line, std::vector<std::string>& tokens)
{
	tokens.clear();
	size_t i = 0;
	const size_t n = line.size();
	while (i < n) {
		while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\f' || line[i] == '\v'))
			++i;
		if (i >= n || line[i] == '#')
			break;
		size_t start = i;
		while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\f' && line[i] != '\v')
			++i;
		tokens.emplace_back(line, start, i - start);
	}
}

// Strict float parse: the whole token must be a finite number.
bool ParseFloat(const std::string& token, float& value)
{
	if (token.empty())
		return false;
	char* end = nullptr;
	errno = 0;
	float v = std::strtof(token.c_str(), &end);
	if (end != token.c_str() + token.size() || errno == ERANGE || !std::isfinite(v))
		return false;
	value = v;
	return true;
}

bool ParseVec(const std::vector<std::string>& tokens, size_t count, float* out)
{
	if (tokens.size() < count + 1)
		return false;
	for (size_t i = 0; i < count; ++i)
		if (!ParseFloat(tokens[i + 1], out[i]))
			return false;
	return true;
}

// Resolve one OBJ index (1-based, or negative = relative to the end) to 0-based.
// An empty field means "not given" (-1). Returns false if it is invalid.
bool ResolveIndex(const char* begin, const char* end, size_t count, long long& index)
{
	if (begin == end) {
		index = -1;
		return true;
	}
	char* stop = nullptr;
	errno = 0;
	long long v = std::strtoll(begin, &stop, 10);
	if (stop != end || errno == ERANGE || v == 0)
		return false;
	long long resolved = v > 0 ? v - 1 : (long long)count + v;
	if (resolved < 0 || resolved >= (long long)count)
		return false;
	index = resolved;
	return true;
}

struct CornerKey
{
	long long p, t, n;
	bool operator==(const CornerKey& o) const { return p == o.p && t == o.t && n == o.n; }
};

struct CornerKeyHash
{
	size_t operator()(const CornerKey& k) const
	{
		uint64_t h = (uint64_t)k.p * 0x9E3779B97F4A7C15ull;
		h ^= (uint64_t)(k.t + 1) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
		h ^= (uint64_t)(k.n + 1) * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
		return (size_t)h;
	}
};

std::string DirectoryOf(const std::string& path)
{
	size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string JoinPath(const std::string& dir, std::string name)
{
	for (char& c : name)
		if (c == '\\')
			c = '/';
	if (!name.empty() && name[0] == '/')
		return name;
	return dir + name;
}

}

bool ParseObj(std::istream& in, ObjMesh& mesh, const ObjLoadOptions& options)
{
	if (!in)
		return false;

	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texcoords;
	std::vector<glm::vec3> normals;
	std::unordered_map<CornerKey, uint32_t, CornerKeyHash> cornerToVertex;
	// Position index of every output vertex that still needs a normal.
	std::vector<std::pair<uint32_t, long long>> needsNormal;
	std::vector<glm::vec3> accumulatedNormals;

	// Groups are keyed by material name; repeated `usemtl X` appends to X's group.
	std::unordered_map<std::string, size_t> groupByName;
	size_t currentGroup = (size_t)-1;
	auto selectGroup = [&](const std::string& name) {
		auto it = groupByName.find(name);
		if (it == groupByName.end()) {
			ObjGroup g;
			g.materialName = name;
			mesh.groups.push_back(g);
			it = groupByName.emplace(name, mesh.groups.size() - 1).first;
		}
		currentGroup = it->second;
	};

	std::string line;
	std::vector<std::string> tokens;
	std::vector<CornerKey> face;
	while (std::getline(in, line)) {
		Tokenize(line, tokens);
		if (tokens.empty())
			continue;
		const std::string& kw = tokens[0];

		if (kw == "v") {
			float p[3];
			if (ParseVec(tokens, 3, p)) positions.emplace_back(p[0], p[1], p[2]);
			else { positions.emplace_back(0.0f); ++mesh.stats.ignoredLines; }	// keep numbering intact
		}
		else if (kw == "vt") {
			float t[2] = { 0.0f, 0.0f };
			if (tokens.size() >= 2 && ParseFloat(tokens[1], t[0]) && (tokens.size() < 3 || ParseFloat(tokens[2], t[1])))
				texcoords.emplace_back(t[0], t[1]);
			else { texcoords.emplace_back(0.0f); ++mesh.stats.ignoredLines; }
		}
		else if (kw == "vn") {
			float n[3];
			if (ParseVec(tokens, 3, n)) normals.emplace_back(n[0], n[1], n[2]);
			else { normals.emplace_back(0.0f, 1.0f, 0.0f); ++mesh.stats.ignoredLines; }
		}
		else if (kw == "usemtl") {
			selectGroup(tokens.size() >= 2 ? tokens[1] : std::string());
		}
		else if (kw == "mtllib") {
			for (size_t i = 1; i < tokens.size(); ++i)
				mesh.materialLibraries.push_back(tokens[i]);
		}
		else if (kw == "f") {
			// Resolve every corner first; a single bad index drops the whole face.
			face.clear();
			bool ok = tokens.size() >= 4;
			for (size_t i = 1; ok && i < tokens.size(); ++i) {
				const std::string& c = tokens[i];
				const char* s = c.c_str();
				const char* e = s + c.size();
				const char* slash1 = std::find(s, e, '/');
				const char* slash2 = slash1 == e ? e : std::find(slash1 + 1, e, '/');
				CornerKey k;
				ok = ResolveIndex(s, slash1, positions.size(), k.p) && k.p >= 0
					&& (slash1 == e ? (k.t = -1, true) : ResolveIndex(slash1 + 1, slash2, texcoords.size(), k.t))
					&& (slash2 == e ? (k.n = -1, true) : ResolveIndex(slash2 + 1, e, normals.size(), k.n));
				if (ok && slash2 != e && std::find(slash2 + 1, e, '/') != e)
					ok = false;	// more than two slashes
				face.push_back(k);
			}
			if (!ok) {
				++mesh.stats.skippedFaces;
				continue;
			}
			if (currentGroup == (size_t)-1)
				selectGroup(std::string());

			// Map corners to output vertices.
			uint32_t faceVerts[3] = { 0, 0, 0 };
			std::vector<uint32_t> cornerVerts;
			cornerVerts.reserve(face.size());
			for (const CornerKey& k : face) {
				uint32_t vi;
				auto it = options.deduplicate ? cornerToVertex.find(k) : cornerToVertex.end();
				if (it != cornerToVertex.end()) {
					vi = it->second;
				} else {
					VertexPTN v;
					v.position = positions[(size_t)k.p];
					v.texcoord = k.t >= 0 ? texcoords[(size_t)k.t] : glm::vec2(0.0f);
					v.normal = k.n >= 0 ? normals[(size_t)k.n] : glm::vec3(0.0f);
					vi = (uint32_t)mesh.vertices.size();
					mesh.vertices.push_back(v);
					if (options.deduplicate)
						cornerToVertex.emplace(k, vi);
					if (k.n < 0 && options.generateNormals)
						needsNormal.emplace_back(vi, k.p);
				}
				cornerVerts.push_back(vi);
			}

			// Fan triangulation; also accumulate area-weighted face normals.
			std::vector<uint32_t>& indices = mesh.groups[currentGroup].indices;
			for (size_t i = 2; i < face.size(); ++i) {
				faceVerts[0] = cornerVerts[0];
				faceVerts[1] = cornerVerts[i - 1];
				faceVerts[2] = cornerVerts[i];
				indices.insert(indices.end(), faceVerts, faceVerts + 3);
				if (options.generateNormals) {
					if (accumulatedNormals.size() < positions.size())
						accumulatedNormals.resize(positions.size(), glm::vec3(0.0f));
					const glm::vec3& a = positions[(size_t)face[0].p];
					const glm::vec3& b = positions[(size_t)face[i - 1].p];
					const glm::vec3& c = positions[(size_t)face[i].p];
					glm::vec3 n = glm::cross(b - a, c - a);	// length = 2 * area
					accumulatedNormals[(size_t)face[0].p] += n;
					accumulatedNormals[(size_t)face[i - 1].p] += n;
					accumulatedNormals[(size_t)face[i].p] += n;
				}
			}
			++mesh.stats.faces;
			mesh.stats.faceCorners += face.size();
			mesh.stats.triangles += face.size() - 2;
		}
		else if (kw == "o" || kw == "g" || kw == "s" || kw == "l" || kw == "p") {
			// Object/group names, smoothing groups, lines and points are not used.
		}
		else {
			++mesh.stats.ignoredLines;
		}
	}

	for (const auto& entry : needsNormal) {
		glm::vec3 n = (size_t)entry.second < accumulatedNormals.size() ? accumulatedNormals[(size_t)entry.second] : glm::vec3(0.0f);
		float len = glm::length(n);
		mesh.vertices[entry.first].normal = len > 0.0f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
	}
	mesh.stats.generatedNormals = needsNormal.size();
	mesh.stats.positions = positions.size();
	mesh.stats.texcoords = texcoords.size();
	mesh.stats.normals = normals.size();
	return true;
}

bool ParseMtl(std::istream& in, const std::string& baseDir, std::vector<ObjMaterial>& materials)
{
	if (!in)
		return false;
	std::string line;
	std::vector<std::string> tokens;
	ObjMaterial* current = nullptr;
	while (std::getline(in, line)) {
		Tokenize(line, tokens);
		if (tokens.empty())
			continue;
		const std::string& kw = tokens[0];
		if (kw == "newmtl") {
			ObjMaterial m;
			m.name = tokens.size() >= 2 ? tokens[1] : std::string();
			materials.push_back(m);
			current = &materials.back();
			continue;
		}
		if (current == nullptr)
			continue;	// keys before the first newmtl have nothing to apply to
		float v[3];
		if (kw == "Ka" && ParseVec(tokens, 3, v)) current->Ka = glm::vec3(v[0], v[1], v[2]);
		else if (kw == "Kd" && ParseVec(tokens, 3, v)) current->Kd = glm::vec3(v[0], v[1], v[2]);
		else if (kw == "Ks" && ParseVec(tokens, 3, v)) current->Ks = glm::vec3(v[0], v[1], v[2]);
		else if (kw == "Ns" && ParseVec(tokens, 1, v)) current->Ns = v[0];
		else if (kw == "illum" && ParseVec(tokens, 1, v)) current->illum = (int)v[0];
		else if (kw == "map_Kd" && tokens.size() >= 2)
			current->mapKd = JoinPath(baseDir, tokens.back());	// options such as -s u v come first
	}
	// Maya writes Kd/Ka = 0 when a texture drives the colour. Taken literally
	// (map_Kd x Kd) the surface would be black, so a zero colour with a texture
	// means "texture only".
	for (ObjMaterial& m : materials) {
		if (m.illum >= 3 && m.illum <= 7)
			m.reflectivity = std::min(1.0f, std::max(m.Ks.x, std::max(m.Ks.y, m.Ks.z)));
		if (m.mapKd.empty())
			continue;
		if (m.Kd == glm::vec3(0.0f)) m.Kd = glm::vec3(1.0f);
		if (m.Ka == glm::vec3(0.0f)) m.Ka = glm::vec3(1.0f);
	}
	return true;
}

void LinkMaterials(ObjMesh& mesh)
{
	for (ObjGroup& g : mesh.groups) {
		g.materialIndex = -1;
		for (size_t i = 0; i < mesh.materials.size(); ++i) {
			if (mesh.materials[i].name == g.materialName) {
				g.materialIndex = (int)i;
				break;
			}
		}
		if (g.materialIndex < 0) {
			ObjMaterial fallback;
			fallback.name = g.materialName;
			mesh.materials.push_back(fallback);
			g.materialIndex = (int)mesh.materials.size() - 1;
		}
	}
}

bool LoadObjFile(const std::string& objPath, ObjMesh& mesh, const ObjLoadOptions& options)
{
	std::ifstream ifs(objPath, std::ios::binary);
	if (!ifs)
		return false;
	return ParseObj(ifs, mesh, options);
}

void LoadObjMaterials(const std::string& objPath, ObjMesh& mesh)
{
	const std::string dir = DirectoryOf(objPath);
	std::vector<std::string> libraries = mesh.materialLibraries;
	if (libraries.empty()) {
		// Exporters sometimes omit mtllib; fall back to <name>.mtl next to the OBJ.
		std::string base = objPath.substr(dir.size());
		libraries.push_back(base.substr(0, base.find_last_of('.')) + ".mtl");
	}
	for (const std::string& lib : libraries) {
		const std::string path = JoinPath(dir, lib);
		std::ifstream ifs(path, std::ios::binary);
		if (ifs)
			ParseMtl(ifs, DirectoryOf(path), mesh.materials);
	}
	LinkMaterials(mesh);
}

void ComputeBounds(const ObjMesh& mesh, glm::vec3& minCorner, glm::vec3& maxCorner)
{
	if (mesh.vertices.empty()) {
		minCorner = maxCorner = glm::vec3(0.0f);
		return;
	}
	minCorner = maxCorner = mesh.vertices[0].position;
	for (const VertexPTN& v : mesh.vertices) {
		minCorner = glm::min(minCorner, v.position);
		maxCorner = glm::max(maxCorner, v.position);
	}
}

void NormalizeToUnitBox(ObjMesh& mesh)
{
	glm::vec3 lo, hi;
	ComputeBounds(mesh, lo, hi);
	const glm::vec3 center = 0.5f * (lo + hi);
	const glm::vec3 extent = hi - lo;
	const float longest = std::max(extent.x, std::max(extent.y, extent.z));
	const float scale = longest > 0.0f ? 1.0f / longest : 1.0f;
	for (VertexPTN& v : mesh.vertices)
		v.position = (v.position - center) * scale;
}
