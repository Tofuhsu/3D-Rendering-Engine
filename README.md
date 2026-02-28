# 3D Rendering Engine (OpenGL)

A 3D rendering engine implemented in C++ using modern OpenGL and GLSL.  
This project demonstrates a complete programmable rendering pipeline, including custom model loading, texture mapping, and multi-light shading.

---

## Features

- Custom OBJ/MTL parser (no external model-loading libraries)
- Texture mapping with material support (map_Kd)
- Multi-light additive shading (Directional, Point, Spot)
- MVP (Model-View-Projection) transformation pipeline
- Real-time rendering using VAO/VBO
- Programmable shader pipeline (GLSL)

---

## Rendering Pipeline

1. Load OBJ/MTL files using a custom parser  
2. Normalize vertex geometry  
3. Upload vertex data to GPU using VAO/VBO  
4. Apply MVP matrix transformations  
5. Perform per-fragment lighting and texture sampling in GLSL  

---

## Technical Highlights

- Implemented custom OBJ/MTL loader without third-party libraries
- Designed full OpenGL rendering pipeline
- Implemented multi-light Phong-style shading model in GLSL
- Managed GPU buffers efficiently for real-time rendering
- Applied MVP matrix transformations for 3D scene control

---

## Controls

- **W / A / S / D** – Control primary light position
- **Arrow Keys** – Control secondary light



## Tech Stack

- C++
- OpenGL
- GLSL
- GLEW
- FreeGLUT
- GLM

---

## Build Instructions

This project was developed using **Visual Studio 2022 (Windows)**.

Requirements:
- OpenGL
- GLEW
- FreeGLUT
- GLM

Open the project in Visual Studio and ensure all dependencies are correctly configured before building.