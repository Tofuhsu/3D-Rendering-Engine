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

# cgltf (MIT), single header pinned to the v1.15 release commit.
FetchContent_Declare(cgltf
  URL https://raw.githubusercontent.com/jkuhlmann/cgltf/360db1a95480fe102ae9c69b27c5d101167ff5ba/cgltf.h
  URL_HASH SHA256=e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec
  DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(cgltf)

# glTF loading, also GL-free; brings its own copies of the cgltf and stb_image
# implementations so tests and fuzzing can link it alone.
add_library(gltfloader STATIC src/gltfloader.cpp src/cgltf_impl.cpp src/stb_image_impl.cpp)
target_include_directories(gltfloader PUBLIC src ${GLM_INCLUDE_DIR} ${GLM_INCLUDE_DIR}/glm
                           ${cgltf_SOURCE_DIR} ${stb_image_SOURCE_DIR})

# GL entry points: macOS links them directly; elsewhere GLEW loads them at run time.
if(APPLE)
  set(ENGINE_USE_GLEW_DEFAULT OFF)
else()
  set(ENGINE_USE_GLEW_DEFAULT ON)
endif()
option(ENGINE_USE_GLEW "Load OpenGL functions with GLEW" ${ENGINE_USE_GLEW_DEFAULT})

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/objloader.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/gltfloader.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/cgltf_impl.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/stb_image_impl.cpp)
add_executable(engine ${ENGINE_SOURCES})
target_include_directories(engine PRIVATE ${stb_image_SOURCE_DIR})
target_link_libraries(engine PRIVATE objloader gltfloader glfw)
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

  add_executable(gltfloader_tests tests/test_gltfloader.cpp)
  target_link_libraries(gltfloader_tests PRIVATE gltfloader)
  target_include_directories(gltfloader_tests PRIVATE ${doctest_SOURCE_DIR})
  target_compile_definitions(gltfloader_tests PRIVATE ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME gltfloader_tests COMMAND gltfloader_tests)

  # Fuzz targets: libFuzzer when available, else the built-in mutation driver.
  foreach(target objloader gltfloader)
    add_executable(fuzz_${target} tests/fuzz_${target}.cpp)
    target_link_libraries(fuzz_${target} PRIVATE ${target})
    if(ENGINE_LIBFUZZER)
      target_compile_definitions(fuzz_${target} PRIVATE ENGINE_LIBFUZZER)
      target_compile_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
      target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
    else()
      target_sources(fuzz_${target} PRIVATE tests/fuzz_driver.cpp)
    endif()
  endforeach()
  # Short fuzz runs are part of the test suite; long runs are started by hand.
  add_test(NAME fuzz_objloader_smoke
           COMMAND fuzz_objloader --iterations 20000 ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus)
  add_test(NAME fuzz_gltfloader_smoke
           COMMAND fuzz_gltfloader --iterations 5000 ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus_gltf)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME osm2obj_tests COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_osm2obj.py)
  endif()

  if(ENGINE_SANITIZE)
    foreach(t objloader objloader_tests fuzz_objloader gltfloader gltfloader_tests fuzz_gltfloader)
      target_compile_options(${t} PRIVATE ${SANITIZER_FLAGS})
      target_link_options(${t} PRIVATE ${SANITIZER_FLAGS})
    endforeach()
  endif()
endif()
