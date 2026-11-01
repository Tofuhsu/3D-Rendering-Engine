cmake_minimum_required(VERSION 3.20)
project(RenderingEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENGINE_BUILD_TESTS "Build unit tests and the fuzz driver" ON)
option(ENGINE_SANITIZE "Build tests and fuzz driver with AddressSanitizer + UBSan" OFF)
option(ENGINE_LIBFUZZER "Link the fuzz target against libFuzzer (LLVM clang, not Apple clang)" OFF)

# GLM is header-only; the sources include <glm.hpp>, so add the glm/ dir itself.
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)

# OBJ/MTL parsing has no OpenGL dependency so tests and fuzzing can link it alone.
add_library(objloader STATIC src/objloader.cpp)
target_include_directories(objloader PUBLIC src ${GLM_INCLUDE_DIR} ${GLM_INCLUDE_DIR}/glm)

include(FetchContent)

# GLFW: use an installed copy (e.g. apt libglfw3-dev) or build the pinned release.
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
    URL_HASH SHA256=ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(glfw)
endif()

# stb_image (public domain / MIT), single header pinned to a commit.
FetchContent_Declare(stb_image
  URL https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h
  URL_HASH SHA256=594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3
  DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(stb_image)

# GL entry points: macOS links them directly; elsewhere GLEW loads them at run time.
if(APPLE)
  set(ENGINE_USE_GLEW_DEFAULT OFF)
else()
  set(ENGINE_USE_GLEW_DEFAULT ON)
endif()
option(ENGINE_USE_GLEW "Load OpenGL functions with GLEW" ${ENGINE_USE_GLEW_DEFAULT})

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/objloader.cpp)
add_executable(engine ${ENGINE_SOURCES})
target_include_directories(engine PRIVATE ${stb_image_SOURCE_DIR})
target_link_libraries(engine PRIVATE objloader glfw)
find_package(OpenGL REQUIRED)
target_link_libraries(engine PRIVATE OpenGL::GL)
if(ENGINE_USE_GLEW)
  find_package(GLEW REQUIRED)
  target_link_libraries(engine PRIVATE GLEW::GLEW)
  target_compile_definitions(engine PRIVATE ENGINE_USE_GLEW)
endif()

if(ENGINE_BUILD_TESTS)
  enable_testing()
  # doctest is header-only; SOURCE_SUBDIR points at a folder without a
  # CMakeLists.txt so its (pre-3.5) build scripts are not configured.
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR header-only)
  FetchContent_MakeAvailable(doctest)

  set(SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)

  add_executable(objloader_tests tests/test_objloader.cpp)
  target_link_libraries(objloader_tests PRIVATE objloader)
  target_include_directories(objloader_tests PRIVATE ${doctest_SOURCE_DIR})
  target_compile_definitions(objloader_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME objloader_tests COMMAND objloader_tests)

  add_executable(fuzz_objloader tests/fuzz_objloader.cpp)
  target_link_libraries(fuzz_objloader PRIVATE objloader)
  if(ENGINE_LIBFUZZER)
    target_compile_definitions(fuzz_objloader PRIVATE ENGINE_LIBFUZZER)
    target_compile_options(fuzz_objloader PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_objloader PRIVATE -fsanitize=fuzzer)
  endif()
  # A short fuzz run is part of the test suite; long runs are started by hand.
  add_test(NAME fuzz_objloader_smoke
           COMMAND fuzz_objloader --iterations 20000 ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME osm2obj_tests COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_osm2obj.py)
  endif()

  if(ENGINE_SANITIZE)
    foreach(t objloader objloader_tests fuzz_objloader)
      target_compile_options(${t} PRIVATE ${SANITIZER_FLAGS})
      target_link_options(${t} PRIVATE ${SANITIZER_FLAGS})
    endforeach()
  endif()
endif()
