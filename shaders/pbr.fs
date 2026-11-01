#version 330 core

// Metallic-roughness PBR: Cook-Torrance specular (GGX distribution, Smith
// geometry, Schlick Fresnel) + Lambert diffuse for the analytic lights, and
// split-sum image-based lighting (irradiance map, pre-filtered specular mips,
// BRDF LUT) for the environment. Output is tone mapped and gamma corrected.

in vec3 iPosWorld;
in vec3 iNormalWorld;
in vec2 iTexCoord;

// Material.
uniform vec3 baseColor;		// linear; multiplied by mapKd when present
uniform sampler2D mapKd;	// sRGB-encoded 8-bit texture
uniform bool hasMapKd;
uniform float metallic;
uniform float roughness;

// Camera and analytic lights (world space), same parameters as the Phong path.
uniform vec3 cameraPos;
uniform vec3 dirLightDir;
uniform vec3 dirLightRadiance;
uniform vec3 pointLightPos;
uniform vec3 pointLightIntensity;
uniform vec3 spotLightPosition;
uniform vec3 spotLightDirection;
uniform vec3 spotLightIntensity;
uniform float constant;
uniform float linear;
uniform float quadratic;
uniform float spotLightCutoffStartInDegree;
uniform float spotLightTotalWidthInDegree;

// Image-based lighting.
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLUT;
uniform float prefilterMaxLod;
uniform float iblIntensity;
uniform float exposure;

out vec4 FragColor;

const float PI = 3.14159265359;

float DistributionGGX(float NdotH, float a)
{
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float GeometrySmith(float NdotV, float NdotL, float roughnessValue)
{
    // Direct-light remapping k = (r + 1)^2 / 8.
    float k = (roughnessValue + 1.0) * (roughnessValue + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float r)
{
    return F0 + (max(vec3(1.0 - r), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Outgoing radiance from one light arriving along L with the given radiance.
vec3 CookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float r)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    if (NdotL <= 0.0)
        return vec3(0.0);
    float a = max(r * r, 1e-3);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = DistributionGGX(max(dot(N, H), 0.0), a) * GeometrySmith(NdotV, NdotL, r) * F
                  / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

float Attenuation(float d)
{
    return 1.0 / (constant + linear * d + quadratic * d * d);
}

vec3 ToneMap(vec3 c)
{
    c *= exposure;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}

void main()
{
    vec3 N = normalize(iNormalWorld);
    vec3 V = normalize(cameraPos - iPosWorld);
    vec4 texel = hasMapKd ? texture(mapKd, iTexCoord) : vec4(1.0);
    vec3 albedo = baseColor * pow(texel.rgb, vec3(2.2));	// sRGB -> linear
    float r = clamp(roughness, 0.04, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Analytic lights.
    vec3 Lo = CookTorrance(N, V, normalize(-dirLightDir), dirLightRadiance, albedo, F0, r);
    vec3 toPoint = pointLightPos - iPosWorld;
    float dPoint = length(toPoint);
    Lo += CookTorrance(N, V, toPoint / dPoint, pointLightIntensity * Attenuation(dPoint), albedo, F0, r);
    vec3 toSpot = spotLightPosition - iPosWorld;
    float dSpot = length(toSpot);
    vec3 Ls = toSpot / dSpot;
    float cosTheta = dot(-Ls, normalize(spotLightDirection));
    float cosInner = cos(radians(spotLightCutoffStartInDegree));
    float cosOuter = cos(radians(spotLightTotalWidthInDegree));
    float cone = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
    Lo += CookTorrance(N, V, Ls, spotLightIntensity * Attenuation(dSpot) * cone, albedo, F0, r);

    // Environment (split-sum IBL).
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, r);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = texture(irradianceMap, N).rgb * albedo;
    vec3 prefiltered = textureLod(prefilterMap, reflect(-V, N), r * prefilterMaxLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, r)).rg;
    vec3 specular = prefiltered * (F * brdf.x + brdf.y);
    vec3 ambient = (kD * diffuse + specular) * iblIntensity;

    FragColor = vec4(ToneMap(ambient + Lo), texel.a);
}
