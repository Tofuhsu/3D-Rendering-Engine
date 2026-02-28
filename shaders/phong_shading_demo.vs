#version 330 core

layout (location = 0) in vec3 Position;
layout (location = 1) in vec3 Normal;
layout (location = 2) in vec2 TexCoord;

// Transformation matrix.

// --------------------------------------------------------
// Add more uniform variables if needed.
// --------------------------------------------------------
uniform mat4 worldMatrix;
uniform mat4 normalMatrix;
uniform mat4 MVP;

// Data pass to fragment shader.
// --------------------------------------------------------
out vec3 iPosWorld;
out vec3 iNormalWorld;
out vec2 iTexCoord;


// Add your data for interpolation.
// --------------------------------------------------------

void main()
{
    // --------------------------------------------------------
    // Add your implementation.
    // --------------------------------------------------------
    
    gl_Position = MVP * vec4(Position, 1.0);
    vec4 positionTmp = worldMatrix * vec4(Position,1.0);
    iPosWorld = positionTmp.xyz / positionTmp.w;

    iNormalWorld = (normalMatrix * vec4(Normal,0.0)).xyz;

    iTexCoord = TexCoord;
}