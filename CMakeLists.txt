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

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/objloader.cpp)
add_executable(engine ${ENGINE_SOURCES})
target_link_libraries(engine PRIVATE objloader)

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

if(ENGINE_BUILD_TESTS)
  enable_testing()
  include(FetchContent)
  # doctest is header-only; SOURCE_SUBDIR points at a folder without a
  # CMakeLists.txt so its (pre-3.5) build scripts are not configured.
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
    URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
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

  if(ENGINE_SANITIZE)
    foreach(t objloader objloader_tests fuzz_objloader)
      target_compile_options(${t} PRIVATE ${SANITIZER_FLAGS})
      target_link_options(${t} PRIVATE ${SANITIZER_FLAGS})
    endforeach()
  endif()
endif()
