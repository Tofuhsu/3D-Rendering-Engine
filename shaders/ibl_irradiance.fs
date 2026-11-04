#version 330 core

// Diffuse irradiance: cosine-weighted integral of the environment over the
// hemisphere around each direction (uniform grid in spherical coordinates).
in vec3 iDirection;

uniform samplerCube environmentMap;

out vec4 FragColor;

void main()
{
    const float PI = 3.14159265359;
    vec3 N = normalize(iDirection);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    const float delta = 0.025;
    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta) {
            vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 dir = t.x * right + t.y * up + t.z * N;
            // Mip 4 of the environment pre-blurs it, avoiding aliasing from the coarse grid.
            irradiance += textureLod(environmentMap, dir, 4.0).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }
    FragColor = vec4(PI * irradiance / samples, 1.0);
}
