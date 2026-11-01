#!/bin/sh
# Build, unit-test and render-smoke-test on Linux with Mesa's software rasterizer
# (llvmpipe) under Xvfb. Used by .github/workflows/ci.yml and runnable in a
# plain ubuntu:24.04 container:
#   docker run --rm -v "$PWD":/src:ro ubuntu:24.04 sh /src/ci/linux-build-and-test.sh
set -eu
SRC=${SRC:-/src}
BUILD=${BUILD:-/tmp/build}

if [ "$(id -u)" = 0 ] && ! command -v cmake >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends \
    build-essential cmake ca-certificates python3 \
    libglm-dev libglew-dev libglfw3-dev libgl1-mesa-dri xvfb xauth >/dev/null
fi

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"
(cd "$BUILD" && ctest --output-on-failure)

# Render smoke test: load a model, draw 10 frames, save the frame, and check that
# every GL object was released.
export LIBGL_ALWAYS_SOFTWARE=1
cd "$SRC"
xvfb-run -a -s "-screen 0 1024x768x24" \
  "$BUILD/engine" --model assets/TexCube/TexCube.obj --capture /tmp/texcube.ppm | tee /tmp/render.log
grep -q "Live GL objects after release: 0 buffers, 0 textures, 0 vertex arrays, 0 programs" /tmp/render.log
test "$(head -c 2 /tmp/texcube.ppm)" = "P6"
# The frame must not be blank: the cube and sky give a wide spread of colours.
python3 - /tmp/texcube.ppm <<'PY'
import sys
data = open(sys.argv[1], "rb").read().split(b"\n", 3)[3]
distinct = len(set(data[i:i + 3] for i in range(0, len(data), 3)))
mean = sum(data) / len(data)
print(f"capture: {distinct} distinct colours, mean level {mean:.1f}")
sys.exit(0 if distinct > 1000 and 20 < mean < 235 else 1)
PY
echo "render smoke test passed"
