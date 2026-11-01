cmake_minimum_required(VERSION 3.20)
project(RenderingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(engine ${ENGINE_SOURCES})

# GLM is header-only; the sources include <glm.hpp>, so add the glm/ dir itself.
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
target_include_directories(engine PRIVATE ${GLM_INCLUDE_DIR} ${GLM_INCLUDE_DIR}/glm)

if(APPLE)
  target_link_libraries(engine PRIVATE
    "-framework OpenGL" "-framework GLUT"
    "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
else()
  find_package(OpenGL REQUIRED)
  find_package(GLEW REQUIRED)
  find_package(GLUT REQUIRED)
  find_package(OpenCV REQUIRED core imgcodecs imgproc highgui)
  target_link_libraries(engine PRIVATE OpenGL::GL GLEW::GLEW GLUT::GLUT ${OpenCV_LIBS})
  target_include_directories(engine PRIVATE ${GLEW_INCLUDE_DIRS}/GL ${GLUT_INCLUDE_DIR}/GL)
endif()
