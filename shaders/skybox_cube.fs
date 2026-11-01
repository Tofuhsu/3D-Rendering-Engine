#version 330 core

in vec3 iDirection;

uniform samplerCube environmentMap;
uniform float exposure;

out vec4 FragColor;

// ACES filmic curve (Narkowicz 2015 fit), then sRGB gamma.
vec3 ToneMap(vec3 c)
{
    c *= exposure;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, vec3(1.0 / 2.2));
}

void main()
{
    FragColor = vec4(ToneMap(textureLod(environmentMap, iDirection, 0.0).rgb), 1.0);
}
