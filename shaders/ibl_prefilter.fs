#version 330 core

// Specular pre-filter (split-sum, Karis 2013): GGX importance sampling of the
// environment for one roughness value per mip level, assuming N = V = R.
in vec3 iDirection;

uniform samplerCube environmentMap;
uniform float roughness;
uniform float environmentResolution;	// face size of environmentMap, level 0
uniform int sampleCount;

out vec4 FragColor;

const float PI = 3.14159265359;

float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint n)
{
    return vec2(float(i) / float(n), RadicalInverseVdC(i));
}

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float a)
{
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float DistributionGGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

void main()
{
    vec3 N = normalize(iDirection);
    vec3 V = N;
    float a = roughness * roughness;

    vec3 color = vec3(0.0);
    float weight = 0.0;
    uint n = uint(sampleCount);
    for (uint i = 0u; i < n; ++i) {
        vec3 H = ImportanceSampleGGX(Hammersley(i, n), N, a);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = dot(N, L);
        if (NdotL > 0.0) {
            // Sample a blurrier mip where the GGX lobe is wide, which removes the
            // bright speckles a fixed sample count would leave.
            float NdotH = max(dot(N, H), 0.0);
            float pdf = DistributionGGX(NdotH, a) * 0.25 + 1e-4;
            float saSample = 1.0 / (float(n) * pdf + 1e-4);
            float saTexel = 4.0 * PI / (6.0 * environmentResolution * environmentResolution);
            float mip = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);
            color += textureLod(environmentMap, L, mip).rgb * NdotL;
            weight += NdotL;
        }
    }
    FragColor = vec4(color / max(weight, 1e-4), 1.0);
}
