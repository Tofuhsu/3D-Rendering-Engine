#version 330 core

// HDR cubemap skybox: rotation-only view, depth forced to the far plane.
layout (location = 0) in vec3 Position;

uniform mat4 viewProj;	// projection * view with the translation removed

out vec3 iDirection;

void main()
{
    iDirection = Position;
    vec4 p = viewProj * vec4(Position, 1.0);
    gl_Position = p.xyww;
}
