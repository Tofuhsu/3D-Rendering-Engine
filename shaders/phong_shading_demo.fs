#version 330 core

// Data from vertex shader (world space).
in vec3 iPosWorld;
in vec3 iNormalWorld;
in vec2 iTexCoord;

// Material.
uniform sampler2D mapKd;
uniform bool hasMapKd;
// Environment reflection: the skybox's equirectangular panorama.
uniform sampler2D envMap;
uniform float reflectivity;	// 0 = none, 1 = mirror (before Fresnel)
uniform vec3 Ka;
uniform vec3 Kd;
uniform vec3 Ks;
uniform float Ns;

// Camera and lights (world space).
uniform vec3 cameraPos;
uniform vec3 ambientLight;
uniform vec3 dirLightDir;
uniform vec3 dirLightRadiance;
uniform vec3 pointLightPos;
uniform vec3 pointLightIntensity;
uniform vec3 spotLightPosition;
uniform vec3 spotLightDirection;
uniform vec3 spotLightIntensity;
// Distance attenuation: 1 / (constant + linear * d + quadratic * d^2).
uniform float constant;
uniform float linear;
uniform float quadratic;
// Half-angles in degrees: full intensity inside the cutoff, zero beyond the total width.
uniform float spotLightCutoffStartInDegree;
uniform float spotLightTotalWidthInDegree;

out vec4 FragColor;

// Diffuse + specular (Phong) for one light arriving from direction L with radiance.
vec3 Phong(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 diffuseColor)
{
    float NdotL = max(dot(N, L), 0.0);
    vec3 R = reflect(-L, N);
    float spec = (NdotL > 0.0) ? pow(max(dot(V, R), 0.0), max(Ns, 1.0)) : 0.0;
    return (diffuseColor * NdotL + Ks * spec) * radiance;
}

// Look up the panorama in direction d, using the same mapping as the skybox sphere
// (u = longitude from +x towards +z, v = latitude, +y up).
vec3 SampleEnvironment(vec3 d)
{
    const float PI = 3.14159265359;
    d = normalize(d);
    float u = atan(d.z, d.x) / (2.0 * PI);
    if (u < 0.0) u += 1.0;
    float v = 0.5 + asin(clamp(d.y, -1.0, 1.0)) / PI;
    // Level 0: the longitude wrap at u = 0/1 breaks derivative-based mip selection.
    return textureLod(envMap, vec2(u, v), 0.0).rgb;
}

float Attenuation(float d)
{
    return 1.0 / (constant + linear * d + quadratic * d * d);
}

void main()
{
    vec3 N = normalize(iNormalWorld);
    vec3 V = normalize(cameraPos - iPosWorld);

    // map_Kd modulates Kd (MTL convention); without a texture Kd is used alone.
    vec4 texel = hasMapKd ? texture(mapKd, iTexCoord) : vec4(1.0);
    vec3 diffuseColor = Kd * texel.rgb;

    vec3 color = ambientLight * Ka * texel.rgb;

    // Directional light.
    color += Phong(N, V, normalize(-dirLightDir), dirLightRadiance, diffuseColor);

    // Point light.
    vec3 toPoint = pointLightPos - iPosWorld;
    float dPoint = length(toPoint);
    color += Phong(N, V, toPoint / dPoint, pointLightIntensity * Attenuation(dPoint), diffuseColor);

    // Spot light: compare cosines, not degrees.
    vec3 toSpot = spotLightPosition - iPosWorld;
    float dSpot = length(toSpot);
    vec3 Ls = toSpot / dSpot;
    float cosTheta = dot(-Ls, normalize(spotLightDirection));
    float cosInner = cos(radians(spotLightCutoffStartInDegree));
    float cosOuter = cos(radians(spotLightTotalWidthInDegree));
    float cone = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
    color += Phong(N, V, Ls, spotLightIntensity * Attenuation(dSpot) * cone, diffuseColor);

    // Mirror reflection, stronger at grazing angles (Schlick's Fresnel approximation).
    if (reflectivity > 0.0) {
        float cosV = max(dot(N, V), 0.0);
        float F = reflectivity + (1.0 - reflectivity) * pow(1.0 - cosV, 5.0);
        color = mix(color, SampleEnvironment(reflect(-V, N)), F);
    }

    FragColor = vec4(color, texel.a);
}
