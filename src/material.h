#ifndef MATERIAL_H
#define MATERIAL_H

#include "headers.h"
#include "shaderprog.h"
#include "imagetexture.h"

// Material Declarations.
class Material
{
public:
	// Material Public Methods.
	Material() { name = "Default"; };
	~Material() {};

	void SetName(const std::string mtlName) { name = mtlName; }
	std::string GetName() const { return name; }

protected:	
	// Material Protected Data.
	std::string name;
};

// ------------------------------------------------------------------------------------------------

// PhongMaterial Declarations.
class PhongMaterial : public Material
{
public:
	// PhongMaterial Public Methods.
	PhongMaterial() {
		Ka = glm::vec3(0.0f, 0.0f, 0.0f);
		Kd = glm::vec3(0.0f, 0.0f, 0.0f);
		Ks = glm::vec3(0.0f, 0.0f, 0.0f);
		Ns = 0.0f;
		reflectivity = 0.0f;
		metallic = 0.0f;
		roughness = 0.5f;
		mapKd = nullptr;
	};
	~PhongMaterial() {};

	void SetKa(const glm::vec3 ka) { Ka = ka; }
	void SetKd(const glm::vec3 kd) { Kd = kd; }
	void SetKs(const glm::vec3 ks) { Ks = ks; }
	void SetNs(const float n) { Ns = n; }
	void SetMapKd(ImageTexture* tex) { mapKd = tex; }
	void SetReflectivity(const float r) { reflectivity = r; }
	void SetMetallic(const float m) { metallic = m; }
	void SetRoughness(const float r) { roughness = r; }

	const glm::vec3 GetKa() const { return Ka; }
	const glm::vec3 GetKd() const { return Kd; }
	const glm::vec3 GetKs() const { return Ks; }
	const float GetNs() const { return Ns; }
	ImageTexture* GetMapKd() const { return mapKd; }
	float GetReflectivity() const { return reflectivity; }
	float GetMetallic() const { return metallic; }
	float GetRoughness() const { return roughness; }

private:
	// PhongMaterial Private Data.
	glm::vec3 Ka;
	glm::vec3 Kd;
	glm::vec3 Ks;
	float Ns;
	float reflectivity;
	float metallic;
	float roughness;
	ImageTexture* mapKd;
};

// ------------------------------------------------------------------------------------------------

// SkyboxMaterial Declarations.
class SkyboxMaterial : public Material
{
public:
	// SkyboxMaterial Public Methods.
	SkyboxMaterial() {
		mapKd = nullptr;
	};
	~SkyboxMaterial() {};
	void SetMapKd(ImageTexture* tex) { mapKd = tex; }
	ImageTexture* GetMapKd() const { return mapKd; }

private:
	// SkyboxMaterial Private Data.
	ImageTexture* mapKd;
};

#endif
