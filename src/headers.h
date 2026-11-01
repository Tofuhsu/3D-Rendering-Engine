#ifndef HEADERS_H
#define HEADERS_H

// OpenGL function loading: GLEW where GL entry points must be loaded at run
// time (Linux, Windows; optional on macOS to test that path), otherwise the
// macOS system headers (OpenGL 4.1 core at most).
#if defined(ENGINE_USE_GLEW)
#include <GL/glew.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#error "Build with ENGINE_USE_GLEW on this platform"
#endif

// Window, context and input.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GLM.
#include <glm.hpp>
#include <gtc/type_ptr.hpp>

#include <gtc/matrix_transform.hpp>

// C++ STL headers.
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <sstream>

#endif
