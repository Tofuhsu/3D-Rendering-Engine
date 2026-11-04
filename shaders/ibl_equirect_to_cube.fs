#version 330 core

// Resample an equirectangular HDR image into one cubemap face. Uses the same
// longitude/latitude mapping as the original sphere skybox.
in vec3 iDirection;

uniform sampler2D equirectMap;

out vec4 FragColor;

void main()
{
    const float PI = 3.14159265359;
    vec3 d = normalize(iDirection);
    float u = atan(d.z, d.x) / (2.0 * PI);
    if (u < 0.0) u += 1.0;
    float v = 0.5 + asin(clamp(d.y, -1.0, 1.0)) / PI;
    FragColor = vec4(textureLod(equirectMap, vec2(u, v), 0.0).rgb, 1.0);
}
