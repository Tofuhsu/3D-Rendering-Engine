#version 330 core

// Split-sum BRDF integration: scale (r) and bias (g) applied to F0, indexed by
// (N.V, roughness). Smith-GGX with the IBL remapping k = a^2 / 2.
in vec2 iTexCoord;

out vec2 FragColor;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 1024u;

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 ImportanceSampleGGX(vec2 xi, float a)
{
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);	// N = +z
}

float GeometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

void main()
{
    float NdotV = max(iTexCoord.x, 1e-4);
    float roughness = iTexCoord.y;
    float a = roughness * roughness;
    float k = a / 2.0;
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float scale = 0.0, bias = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = vec2(float(i) / float(SAMPLE_COUNT), RadicalInverseVdC(i));
        vec3 H = ImportanceSampleGGX(xi, a);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0) {
            float G = GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
            float Gvis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            scale += (1.0 - Fc) * Gvis;
            bias += Fc * Gvis;
        }
    }
    FragColor = vec2(scale, bias) / float(SAMPLE_COUNT);
}
