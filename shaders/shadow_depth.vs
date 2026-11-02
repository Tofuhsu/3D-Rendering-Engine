#version 330 core

// Depth-only pass from the directional light.
layout (location = 0) in vec3 Position;

uniform mat4 lightMVP;

void main()
{
    gl_Position = lightMVP * vec4(Position, 1.0);
}
