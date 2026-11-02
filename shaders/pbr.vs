#version 330 core

// PBR vertex shader: world-space position, normal and tangent frame.
// OBJ meshes have no tangent attribute; location 3 then reads (0, 0, 0, 1)
// and the fragment shader does not use it (hasNormalMap is false).
layout (location = 0) in vec3 Position;
layout (location = 1) in vec3 Normal;
layout (location = 2) in vec2 TexCoord;
layout (location = 3) in vec4 Tangent;	// xyz, w = bitangent sign

uniform mat4 worldMatrix;
uniform mat4 normalMatrix;
uniform mat4 MVP;

out vec3 iPosWorld;
out vec3 iNormalWorld;
out vec4 iTangentWorld;
out vec2 iTexCoord;

void main()
{
    gl_Position = MVP * vec4(Position, 1.0);
    iPosWorld = (worldMatrix * vec4(Position, 1.0)).xyz;
    iNormalWorld = (normalMatrix * vec4(Normal, 0.0)).xyz;
    iTangentWorld = vec4((worldMatrix * vec4(Tangent.xyz, 0.0)).xyz, Tangent.w);
    iTexCoord = TexCoord;
}
