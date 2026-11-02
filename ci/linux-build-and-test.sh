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
grep -q "Live GL objects after release: 0 buffers, 0 textures, 0 vertex arrays, 0 programs, 0 framebuffers/renderbuffers" /tmp/render.log
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

# PBR + IBL smoke test. The Poly Haven HDRs are not in git, so write a small
# synthetic Radiance HDR sky (flat RGBE scanlines) and light the sphere grid with it.
python3 - /tmp/sky.hdr <<'PY'
import math, sys
w, h = 64, 32
rows = []
for y in range(h):
    elevation = 0.5 - (y + 0.5) / h            # +0.5 at the top row
    for x in range(w):
        sun = math.exp(-((x - 16) ** 2 + (y - 8) ** 2) / 8.0) * 40.0
        sky = max(elevation, 0.0) * 2.0 + 0.2
        rgb = (sky * 0.6 + sun, sky * 0.8 + sun, sky * 1.2 + sun * 0.9)
        m = max(rgb)
        e = math.frexp(m)[1] if m > 1e-32 else -128
        scale = 256.0 / (2.0 ** e) if m > 1e-32 else 0.0
        rows.append(bytes([min(255, int(c * scale)) for c in rgb] + [e + 128]))
with open(sys.argv[1], "wb") as f:
    f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (h, w))
    f.write(b"".join(rows))
PY
xvfb-run -a -s "-screen 0 1024x768x24" \
  "$BUILD/engine" --model assets/generated/sphere_grid.obj --shading pbr --env /tmp/sky.hdr \
  --rotate 0 --camera 0,0,3.3 --target 0,0,0 --capture /tmp/pbr.ppm | tee /tmp/pbr.log
grep -q "IBL precompute" /tmp/pbr.log
grep -q "Live GL objects after release: 0 buffers, 0 textures, 0 vertex arrays, 0 programs, 0 framebuffers/renderbuffers" /tmp/pbr.log
python3 - /tmp/pbr.ppm <<'PY'
import sys
data = open(sys.argv[1], "rb").read().split(b"\n", 3)[3]
distinct = len(set(data[i:i + 3] for i in range(0, len(data), 3)))
print(f"pbr capture: {distinct} distinct colours")
sys.exit(0 if distinct > 1000 else 1)
PY
echo "PBR smoke test passed"

# glTF smoke test with the committed seed (embedded PNG texture, node hierarchy).
xvfb-run -a -s "-screen 0 1024x768x24" \
  "$BUILD/engine" --model tests/corpus_gltf/triangle.glb --env /tmp/sky.hdr --capture /tmp/gltf.ppm | tee /tmp/gltf.log
grep -q "glTF: 1 mesh nodes, 1 primitives (0 skipped), 1 materials, 1 images (0 failed)" /tmp/gltf.log
grep -q "Live GL objects after release: 0 buffers, 0 textures, 0 vertex arrays, 0 programs, 0 framebuffers/renderbuffers" /tmp/gltf.log
echo "glTF smoke test passed"
