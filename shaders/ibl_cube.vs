#version 330 core

// Unit cube used to render into each cubemap face (and for the HDR skybox).
layout (location = 0) in vec3 Position;

uniform mat4 viewProj;

out vec3 iDirection;

void main()
{
    iDirection = Position;
    gl_Position = viewProj * vec4(Position, 1.0);
}
