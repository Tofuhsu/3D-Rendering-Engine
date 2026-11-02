#version 330 core

// Metallic-roughness PBR: Cook-Torrance specular (GGX distribution, Smith
// geometry, Schlick Fresnel) + Lambert diffuse for the analytic lights, and
// split-sum image-based lighting (irradiance map, pre-filtered specular mips,
// BRDF LUT) for the environment. Output is tone mapped and gamma corrected.

in vec3 iPosWorld;
in vec3 iNormalWorld;
in vec4 iTangentWorld;
in vec2 iTexCoord;

// Material.
uniform vec3 baseColor;		// linear; multiplied by mapKd when present
uniform sampler2D mapKd;	// sRGB-encoded 8-bit texture
uniform bool hasMapKd;
uniform float metallic;		// multiplied by metallicRoughnessMap.b when present
uniform float roughness;	// multiplied by metallicRoughnessMap.g when present
// glTF maps (texture units 4-7). OBJ materials leave them off.
uniform sampler2D metallicRoughnessMap;
uniform bool hasMetallicRoughnessMap;
uniform sampler2D normalMap;		// tangent space, +Y up
uniform bool hasNormalMap;
uniform float normalScale;
uniform sampler2D occlusionMap;		// R channel, applied to image-based light only
uniform bool hasOcclusionMap;
uniform float occlusionStrength;
uniform sampler2D emissiveMap;		// sRGB
uniform bool hasEmissiveMap;
uniform vec3 emissiveFactor;

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

// Directional-light shadow map (texture unit 8).
uniform sampler2DShadow shadowMap;
uniform mat4 lightViewProj;
uniform bool shadowsEnabled;
uniform int pcfRadius;			// 0: 1 tap, 1: 3x3, 2: 5x5 (each tap is a 2x2 bilinear compare)
uniform float shadowTexelSize;	// 1 / map size
uniform float shadowNormalOffset;	// world units, ~1.5 shadow texels

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
vec3 CookTorrance(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float r, float metalness)
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
    vec3 kD = (vec3(1.0) - F) * (1.0 - metalness);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// Fraction of the directional light that reaches this point (1 = fully lit).
float DirectionalShadow(vec3 N, vec3 L)
{
    if (!shadowsEnabled)
        return 1.0;
    // Normal-offset and slope-scaled bias against shadow acne.
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec4 p = lightViewProj * vec4(iPosWorld + N * shadowNormalOffset * (1.0 - NdotL), 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (c.z > 1.0)
        return 1.0;
    float bias = mix(0.0015, 0.0003, NdotL);
    float lit = 0.0;
    for (int y = -pcfRadius; y <= pcfRadius; ++y)
        for (int x = -pcfRadius; x <= pcfRadius; ++x)
            lit += texture(shadowMap, vec3(c.xy + vec2(x, y) * shadowTexelSize, c.z - bias));
    float taps = float((2 * pcfRadius + 1) * (2 * pcfRadius + 1));
    return lit / taps;
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
    if (hasNormalMap) {
        // Tangent frame (re-orthogonalized after interpolation).
        vec3 T = normalize(iTangentWorld.xyz - N * dot(N, iTangentWorld.xyz));
        vec3 B = cross(N, T) * iTangentWorld.w;
        vec3 n = texture(normalMap, iTexCoord).xyz * 2.0 - 1.0;
        n.xy *= normalScale;
        N = normalize(mat3(T, B, N) * n);
    }
    vec3 V = normalize(cameraPos - iPosWorld);
    vec4 texel = hasMapKd ? texture(mapKd, iTexCoord) : vec4(1.0);
    vec3 albedo = baseColor * pow(texel.rgb, vec3(2.2));	// sRGB -> linear
    float metal = metallic;
    float r = roughness;
    if (hasMetallicRoughnessMap) {
        vec4 mr = texture(metallicRoughnessMap, iTexCoord);
        r *= mr.g;
        metal *= mr.b;
    }
    r = clamp(r, 0.04, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);

    // Analytic lights.
    vec3 Ld = normalize(-dirLightDir);
    vec3 Lo = CookTorrance(N, V, Ld, dirLightRadiance * DirectionalShadow(normalize(iNormalWorld), Ld), albedo, F0, r, metal);
    vec3 toPoint = pointLightPos - iPosWorld;
    float dPoint = length(toPoint);
    Lo += CookTorrance(N, V, toPoint / dPoint, pointLightIntensity * Attenuation(dPoint), albedo, F0, r, metal);
    vec3 toSpot = spotLightPosition - iPosWorld;
    float dSpot = length(toSpot);
    vec3 Ls = toSpot / dSpot;
    float cosTheta = dot(-Ls, normalize(spotLightDirection));
    float cosInner = cos(radians(spotLightCutoffStartInDegree));
    float cosOuter = cos(radians(spotLightTotalWidthInDegree));
    float cone = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
    Lo += CookTorrance(N, V, Ls, spotLightIntensity * Attenuation(dSpot) * cone, albedo, F0, r, metal);

    // Environment (split-sum IBL).
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, r);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metal);
    vec3 diffuse = texture(irradianceMap, N).rgb * albedo;
    vec3 prefiltered = textureLod(prefilterMap, reflect(-V, N), r * prefilterMaxLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, r)).rg;
    vec3 specular = prefiltered * (F * brdf.x + brdf.y);
    vec3 ambient = (kD * diffuse + specular) * iblIntensity;
    if (hasOcclusionMap)
        ambient *= mix(1.0, texture(occlusionMap, iTexCoord).r, occlusionStrength);

    vec3 emissive = emissiveFactor;
    if (hasEmissiveMap)
        emissive *= pow(texture(emissiveMap, iTexCoord).rgb, vec3(2.2));

    FragColor = vec4(ToneMap(ambient + Lo + emissive), texel.a);
}
