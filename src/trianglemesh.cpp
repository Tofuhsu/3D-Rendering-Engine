#include "trianglemesh.h"
#include "material.h"
#include "light.h"
#include "shaderprog.h"
#include <fstream>

#include <iostream>
#include <string>
#include <map>
#include <stdlib.h>
#include <string.h>
#include <cstring>
#include <math.h>
#include <windows.h>


using std::vector;
using namespace std;
map<string, int>mtlmap;

int buffer = 0;

// Constructor of a triangle mesh.
TriangleMesh::TriangleMesh()
{
	// -------------------------------------------------------
	// Add your initialization code here.
	// -------------------------------------------------------
	numVertices = 0;
	numTriangles = 0;
	objCenter = glm::vec3(0.0f, 0.0f, 0.0f);
	objExtent = glm::vec3(0.0f, 0.0f, 0.0f);
}

// Destructor of a triangle mesh.
TriangleMesh::~TriangleMesh()
{
	// -------------------------------------------------------
	// Add your release code here.
	// -------------------------------------------------------
	vertices.clear();
	subMeshes[buffer].vertexIndices.clear();
}

const std::vector<std::string> split(const std::string& str, const std::string& pattern) {
	std::vector<std::string> result;
	std::string::size_type begin, end;

	end = str.find(pattern);
	begin = 0;

	while (end != std::string::npos)
	{
		if (end - begin != 0)
		{
			result.push_back(str.substr(begin, end - begin));
		}
		begin = end + pattern.size();
		end = str.find(pattern, begin);
	}

	if (begin != str.length())
	{
		result.push_back(str.substr(begin));
	}
	return result;
}

// Load the geometry and material data from an OBJ file.
bool TriangleMesh::LoadFromFile(const std::string& filePath, const bool normalized)
{	
	// Parse the OBJ file.
	// ---------------------------------------------------------------------------
    // Add your implementation here (HW1 + read *.MTL).
    // ---------------------------------------------------------------------------
	std::vector< glm::vec3 > v;
	std::vector< glm::vec2 > vt;
	std::vector< glm::vec3 > vn;
	std::ifstream ifs;
	//cout << subMeshes.size() << endl;


	ifs.open(filePath, std::ifstream::in);
	// Normalize the geometry data.
	std::ifstream fin(filePath);
	int index = 0, SubMeshID = 0;
	std::string buffer;
	while (!ifs.eof()) {
		getline(ifs, buffer);
		if (buffer.size() == 0) continue;
		vector<string> tmpsplit = split(buffer, " ");
		if (tmpsplit[0] == "v") {
			glm::vec3 vertex_v;
			vertex_v.x = stof(tmpsplit[1]);
			vertex_v.y = stof(tmpsplit[2]);
			vertex_v.z = stof(tmpsplit[3]);
			v.push_back(vertex_v);
		}
		if (tmpsplit[0] == "vt") {
			glm::vec2 vertex_vt;
			vertex_vt.x = stof(tmpsplit[1]);
			vertex_vt.y = stof(tmpsplit[2]);
			vt.push_back(vertex_vt);
		}
		if (tmpsplit[0] == "vn") {
			glm::vec3 vertex_vn;
			vertex_vn.x = stof(tmpsplit[1]);
			vertex_vn.y = stof(tmpsplit[2]);
			vertex_vn.z = stof(tmpsplit[3]);
			vn.push_back(vertex_vn);
		}
		if (tmpsplit[0] == "usemtl") {
			SubMeshID = mtlmap[tmpsplit[1]];
			buffer = SubMeshID;
			//cout << SubMeshID << endl;
		}

		if (tmpsplit[0] == "f") {
			VertexPTN recent;
			//cout << SubMeshID << endl;
			for (int i = 1; i < tmpsplit.size(); i++) {
				vector<string> mtmpsplit = split(tmpsplit[i], "/");
				recent.position = v[abs(stoi(mtmpsplit[0])) - 1];
				recent.texcoord = vt[abs(stoi(mtmpsplit[1])) - 1];
				//cout << vt[abs(stoi(mtmpsplit[1])) - 1].x <<" " << vt[abs(stoi(mtmpsplit[1])) - 1].y << endl;
				recent.normal = vn[abs(stoi(mtmpsplit[2])) - 1];
				vertices.push_back(recent);
			}
			
			for (int i = index; i < index + tmpsplit.size() - 1; i++) {
				if (i - index > 1) {
					//cout << index << " " << i << subMeshes.size() << endl;
					subMeshes[SubMeshID].vertexIndices.push_back(index);
					subMeshes[SubMeshID].vertexIndices.push_back(i - 1);
					subMeshes[SubMeshID].vertexIndices.push_back(i);
				}
			}
			index += tmpsplit.size() - 1;
		}
	}
	
	ifs.close();
	numVertices = vertices.size();
	numTriangles = subMeshes[SubMeshID].vertexIndices.size() / 3;



	// 
	// Normalize the geometry data.
	if (normalized) {
		// -----------------------------------------------------------------------
		// Add your normalization code here (HW1).
		// -----------------------------------------------------------------------
		glm::vec3 minbuffer, maxbuffer;
		minbuffer = vertices[0].position;
		maxbuffer = vertices[0].position;
		for (int i = 0; i < numVertices; i++) {
			if (minbuffer.x > vertices[i].position.x) minbuffer.x = vertices[i].position.x;
			if (minbuffer.y > vertices[i].position.y) minbuffer.y = vertices[i].position.y;
			if (minbuffer.z > vertices[i].position.z) minbuffer.z = vertices[i].position.z;
			if (maxbuffer.x < vertices[i].position.x) maxbuffer.x = vertices[i].position.x;
			if (maxbuffer.y < vertices[i].position.y) maxbuffer.y = vertices[i].position.y;
			if (maxbuffer.z < vertices[i].position.z) maxbuffer.z = vertices[i].position.z;
		}
		float box_length;
		box_length = maxbuffer.x - minbuffer.x;
		if ((maxbuffer.y - minbuffer.y) > box_length)box_length = maxbuffer.y - minbuffer.y;
		if ((maxbuffer.z - minbuffer.z) > box_length)box_length = maxbuffer.z - minbuffer.z;

		std::vector<glm::vec3>new_vector;
		glm::vec3 tmp{};
		for (int i = 0; i < numVertices; i++) {
			tmp.x = (vertices[i].position.x - minbuffer.x) * 2 / box_length - 1;
			tmp.y = (vertices[i].position.y - minbuffer.y) * 2 / box_length - 1;
			tmp.z = (vertices[i].position.z - minbuffer.z) * 2 / box_length - 1;
			vertices[i].position.x = tmp.x / 2;
			vertices[i].position.y = tmp.y / 2;
			vertices[i].position.z = tmp.z / 2;
		}
	}
	

	return true;
}

bool TriangleMesh::LoadFromFilemtl(const std::string& filePath, const bool normalized)
{
	if (normalized) {
		// -----------------------------------------------------------------------
		// Add your normalization code here (HW1).
		// -----------------------------------------------------------------------

		std::ifstream ifs;
		glm::vec3 tmp;
		ifs.open(filePath, std::ifstream::in);
		
		// Normalize the geometry data.
		std::ifstream fin(filePath);
		std::string buffer;
		int index = 0, mtlcount = 0, i = 0, count = 0;

		while (!ifs.eof()) {

			getline(ifs, buffer);
			if (buffer.size() == 0) continue;
			vector<string> tmpsplit = split(buffer, " ");

			/*if (tmpsplit[0] == "#" && tmpsplit[1] == "Material" && tmpsplit[2] == "Count:") {
				mtlcount = stoi(tmpsplit[3]);

				for (int i = 0; i < mtlcount; i++) {
					SubMesh recent;
					PhongMaterial* phong = new PhongMaterial;
					recent.material = phong;
					subMeshes.push_back(recent);
				}
			}*/

			if (tmpsplit[0] == "newmtl") {
				//cout << subMeshes.size() << endl;
				SubMesh recent;
				PhongMaterial* phong = new PhongMaterial;
				recent.material = phong;
				subMeshes.push_back(recent);
				subMeshes[count].material->SetName(tmpsplit[1]);
				mtlmap[tmpsplit[1]] = count;
			}

			if (tmpsplit[0] == "Ns") {
				subMeshes[count].material->SetNs(stof(tmpsplit[1]));
			}

			if (tmpsplit[0] == "Ka") {
				glm::vec3 vertex_ka;
				vertex_ka.x = stof(tmpsplit[1]);
				vertex_ka.y = stof(tmpsplit[2]);
				vertex_ka.z = stof(tmpsplit[3]);
				subMeshes[count].material->SetKa(vertex_ka);
				tmp = subMeshes[count].material->GetKa();
			}

			if (tmpsplit[0] == "Kd") {
				glm::vec3 vertex_kd;
				vertex_kd.x = stof(tmpsplit[1]);
				vertex_kd.y = stof(tmpsplit[2]);
				vertex_kd.z = stof(tmpsplit[3]);
				subMeshes[count].material->SetKd(vertex_kd);
				tmp = subMeshes[count].material->GetKd();
			}

			if (tmpsplit[0] == "Ks") {
				glm::vec3 vertex_ks;
				vertex_ks.x = stof(tmpsplit[1]);
				vertex_ks.y = stof(tmpsplit[2]);
				vertex_ks.z = stof(tmpsplit[3]);
				subMeshes[count].material->SetKs(vertex_ks);
				tmp = subMeshes[count].material->GetKs();
			}
			if (tmpsplit[0] == "map_Kd") {
				string x= filePath;
				while (x.back() != '/')x.pop_back();
				x += tmpsplit[1];
				PhongMaterial recent;
				ImageTexture* texture = new ImageTexture(x);
				recent.SetMapKd(texture);
				subMeshes[count].material->SetMapKd(texture);
				count++;
			}
		}
		ifs.close();
	}
	return true;
}


void TriangleMesh::CreateVertexBuffer() {

	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPTN) * GetNumVertices(), &(vertices[0]), GL_STATIC_DRAW);

}
void TriangleMesh::CreateSubmeshBuffer() {

	for (int i = 0; i < subMeshes.size(); i++) {
		glGenBuffers(1, &subMeshes[i].iboId);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, subMeshes[i].iboId);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * subMeshes[i].vertexIndices.size(), &(subMeshes[i].vertexIndices[0]), GL_STATIC_DRAW);
	}

}


// Show model information.
void TriangleMesh::ShowInfo()
{
	std::cout << "# Vertices: " << numVertices << std::endl;
	std::cout << "# Triangles: " << numTriangles << std::endl;
	std::cout << "Total " << subMeshes.size() << " subMeshes loaded" << std::endl;
	for (unsigned int i = 0; i < subMeshes.size(); ++i) {
		const SubMesh& g = subMeshes[i];
		std::cout << "SubMesh " << i << " with material: " << g.material->GetName() << std::endl;
		std::cout << "Num. triangles in the subMesh: " << g.vertexIndices.size() / 3 << std::endl;
	}
	std::cout << "Model Center: " << objCenter.x << ", " << objCenter.y << ", " << objCenter.z << std::endl;
	std::cout << "Model Extent: " << objExtent.x << " x " << objExtent.y << " x " << objExtent.z << std::endl;
}

