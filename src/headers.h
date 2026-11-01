#ifndef HEADERS_H
#define HEADERS_H

// OpenGL and GLUT headers.
#ifdef __APPLE__
// macOS: system OpenGL (4.1 core max) + Apple GLUT; no GLEW needed.
#define GL_SILENCE_DEPRECATION
#define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#include <OpenGL/gl3.h>
#include <GLUT/glut.h>
#else
#include <glew.h>
#include <freeglut.h>
#endif

// GLM.
#include <glm.hpp>
#include <gtc/type_ptr.hpp>

#include <gtc/matrix_transform.hpp>

// OpenCV (image loading on non-Apple platforms; macOS uses ImageIO).
#ifndef __APPLE__
#include <opencv2/opencv.hpp>
#endif

// C++ STL headers.
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <fstream>
#include <sstream>

#endif
