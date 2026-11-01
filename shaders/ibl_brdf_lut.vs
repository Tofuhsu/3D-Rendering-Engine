#version 330 core

// Full-screen triangle from gl_VertexID; no vertex buffer needed.
out vec2 iTexCoord;

void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    iTexCoord = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
