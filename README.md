# 3D Rendering Engine (OpenGL)

A real-time 3D rendering engine written in C++ with modern OpenGL and GLSL,
built from scratch without a scene-graph or model-loading library. It
implements its own OBJ/MTL parsing, GPU buffer management, a programmable
Phong-style lighting pipeline, and skybox environment mapping — the full
path from a mesh file on disk to a shaded frame on screen.

## Features

- Custom OBJ/MTL parser (no external model-loading libraries)
- Texture mapping with material support (`map_Kd`)
- Multi-light additive shading (Directional, Point, Spot)
- Skybox / environment mapping
- MVP (Model-View-Projection) transformation pipeline
- Real-time rendering using VAO/VBO
- Programmable shader pipeline (GLSL)
- Runtime polygon-mode switching (point / wireframe / fill) for pipeline inspection
- Explicit GPU buffer and heap object management with deterministic cleanup on exit

## Rendering Pipeline

1. Load OBJ/MTL files using a custom parser
2. Normalize vertex geometry
3. Upload vertex data to GPU using VAO/VBO
4. Apply MVP matrix transformations
5. Render the skybox, then the scene, with per-fragment lighting and texture sampling in GLSL

## Controls

- **W / A / S / D** — move the spot light
- **Arrow Keys** — move the point light
- **F1 / F2 / F3** — switch polygon mode: point / wireframe / fill
- **Esc** — release all allocated resources and exit

## Tech Stack

- C++
- OpenGL
- GLSL
- GLEW
- FreeGLUT
- GLM

## Build Instructions

This project was developed using **Visual Studio 2022 (Windows)**.

Requirements:
- OpenGL
- GLEW
- FreeGLUT
- GLM

Open the project in Visual Studio and ensure all dependencies are correctly configured before building.
