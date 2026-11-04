# Engineering notes

How the 2026 rebuild was done, what was measured, and what did not work the first
time. Every number here comes from a run whose logs and CSVs are in
[`docs/bench/`](bench); nothing is estimated.

**Machine for all timings:** Apple M4 Pro, macOS 26.6.2, OpenGL 4.1 (Metal-90.5),
Release build, vsync off, 1000 frames after 100 warm-up frames, median of 3 runs
unless stated otherwise.

Commit subjects carry a short tag for each step:

| Tag | Step |
|---|---|
| U1 | Rendering correctness and GL resource lifetime |
| U4 | Frame profiler and `--bench` |
| U3 | OBJ/MTL loader rewrite, tests, fuzzing |
| U17 | USC campus scene from OpenStreetMap |
| G2 | Environment reflection on Phong materials |
| U2 | GLFW + stb_image, Linux build, CI |
| U9 | PBR + image-based lighting |
| U8 | glTF 2.0 loader |
| U7 | Shadow mapping |

## 1. Correctness first (U1)

The 2023 code compiled only in Visual Studio and ran in a compatibility context, which
hid several bugs. Fixed before adding anything:

- No VAO was ever created. Each sub-mesh now owns a VAO that records the VBO layout
  and its index buffer.
- Texture colour was added to the lighting instead of multiplying the diffuse term,
  and the ambient term was unused.
- The spot-light cone compared a cosine with an angle in degrees, so it never cut off.
  It now compares cosines and fades linearly between the inner and outer angle.
- Normals were moved to view space while lights stayed in world space.
- The MTL reader advanced the material index only on `map_Kd`, so materials without a
  texture were assigned to the wrong faces.
- Nothing was freed. The spot light built its marker twice and leaked one VAO and VBO.

`--capture` now counts live buffers, textures, VAOs, programs, framebuffers and
renderbuffers after shutdown. It reports 0 for every bundled model, and the Linux CI
render tests fail when it does not.

## 2. Measure before optimising (U4)

`src/profiler.*` records per frame:

- CPU wall time and CPU render time (`std::chrono::steady_clock`);
- GPU time per pass with `GL_TIME_ELAPSED` queries, kept in a ring of 4 frames and read
  back 4 frames late so reading them never stalls the pipeline;
- draw calls and triangles.

`--bench N` runs a fixed camera path. It writes a per-frame CSV and prints mean,
median, p95, min and max. `tools/bench_summary.py` turns a folder of runs into a table.
A baseline with the original parser was recorded before it was replaced.

## 3. OBJ/MTL loader (U3)

The loader was rewritten as a GL-free static library so it can be unit-tested and
fuzzed without a window.

- Lines are split into a reused token buffer and numbers are parsed with `strtof`,
  so a malformed number is counted and skipped instead of throwing from `stof`.
  A face with an out-of-range index is dropped and counted the same way.
- Supports `v`, `v/vt`, `v//vn` and `v/vt/vn` faces, negative indices, n-gons
  (fan-triangulated), comments and CRLF.
- Vertex de-duplication: a hash map on the (position, uv, normal) index triple.
  Normals are generated when the file has none.

| Rose, 166,844 triangles | Original | New |
|---|---|---|
| Vertices | 333,536 | 98,054 (−70.6 %) |
| VBO size (32 B/vertex) | 10.67 MB | 3.14 MB |
| Parse time | 108.4 ms | 56.3 ms |

With de-duplication off (`--dedup off`), the new parser reads Rose in 47.7 ms. So most
of the speed-up comes from tokenizing and number parsing, and the hash map costs
about 9 ms.

Frame time did not change measurably: CPU frame median 0.36–0.41 ms for every bundled
model. These models are too light to be vertex-bound, so smaller buffers do not show
up in frame time.

**Tests:** 18 doctest cases covering index forms, negative indices, n-gons, CRLF,
broken input, MTL keys and the bundled models.

**Fuzzing:** Apple clang ships without libFuzzer, so `tests/fuzz_driver.cpp` adds a
small mutation driver. The same targets also build as libFuzzer entry points. Under
ASan + UBSan, 710,694 inputs in 90 s produced no findings.

Two bugs found while writing the tests:
- A `#` inside a material name (`Material_#36`) was cut off as a comment. `#` now
  starts a comment only at the start of a token.
- Maya MTLs with `Kd 0` plus `map_Kd` rendered black once U1 made the texture
  multiply `Kd`. A zero `Kd` with a texture now means "texture only".

## 4. USC campus from OpenStreetMap (U17)

`tools/osm2obj.py` uses only the Python standard library. It takes OSM XML or Overpass
JSON and:

1. projects coordinates to local metres (equirectangular around the centre);
2. extrudes each building outline into walls;
3. ear-clips flat roofs, bridging inner rings into the outer ring so courtyards
   become holes;
4. writes an OBJ with five material classes and a ground plane.

Only buildings whose centroid lies inside the university's outline are kept.

Result: 157 buildings, 9,684 triangles, 14 courtyards, no triangulation fallbacks.
Building heights come from the `height` tag (110 buildings), from
`building:levels` (21), or default to 7 m (26). The script has 8 unit tests, which
CTest runs.

The Overpass servers returned errors at the time, so `tools/fetch_usc_campus.sh` uses
one OSM API `map` call instead. Data © OpenStreetMap contributors, ODbL 1.0.

## 5. Portable build (U2)

- GLUT was replaced by GLFW 3.5.1, and ImageIO/OpenCV by stb_image. GLEW loads GL on
  Linux and Windows; macOS uses the system headers.
- Third-party code is fetched by CMake at a pinned version and checked against a pinned
  SHA-256.
- Building with GCC found two portability bugs: `std::cosf`/`std::sinf` are not in
  libstdc++, and `<algorithm>` was missing.
- Linux is tested in an Ubuntu 24.04 container on Mesa llvmpipe, with the same script
  CI runs ([`ci/linux-build-and-test.sh`](../ci/linux-build-and-test.sh)).
- The Rose capture matches the GLUT build: mean difference 0.055/255, with 0.18 % of
  channel values more than 8 apart, all on anti-aliased edges.
- Frame time is unchanged: Rose CPU frame 0.412 ms with GLFW vs 0.414 ms with GLUT.
- Texture decoding is slower: Rose textures take 215.1 ms with stb_image vs 167.9 ms
  with ImageIO.

## 6. PBR and image-based lighting (U9)

The PBR path uses the metallic-roughness model:
- Cook-Torrance BRDF: GGX distribution, Smith-Schlick geometry, Schlick Fresnel;
- split-sum image-based lighting;
- ACES tone mapping and sRGB output.

OpenGL 4.1 has no compute shaders, so the environment is precomputed at start-up with
fragment-shader passes:

```
.hdr (equirect) ─► 512² RGBA16F cubemap + mips
                ├─► 32² diffuse irradiance (cosine-weighted)
                ├─► 128² pre-filtered specular, 5 mips for roughness 0..1
                │     (1024 GGX importance samples, mip chosen from the sample pdf)
                └─► 512² RG16F BRDF LUT
```

**Precompute time** for a 2K HDR: median 121.3 ms over 5 runs. Roughly: HDR load 29 ms,
irradiance 30 ms, prefilter 50 ms, BRDF LUT 5 ms. The first run after a build took
663 ms while the driver's shader cache was cold.

**Frame cost:** the 5×5 sphere grid at 800×800 is fragment-heavy, and its mesh pass
went from 0.067 ms with Phong to 0.114 ms with PBR. On vertex-bound scenes (Rose,
Sports Car) there was no measurable change. These are timer-query numbers; §9 explains
why they are only approximate.

**OBJ materials** use the MTL `Pm` and `Pr` keys when present. Otherwise roughness is
derived from the Phong exponent as `sqrt(sqrt(2 / (Ns + 2)))`.

## 7. glTF 2.0 (U8)

- Parsed with [cgltf](https://github.com/jkuhlmann/cgltf), which is pinned. The node
  tree is flattened with transforms baked into the vertices:
  - normals use the inverse-transpose;
  - tangent handedness flips under mirroring transforms.
- Missing tangents are generated per triangle and orthogonalised (Gram-Schmidt).
  This is not MikkTSpace, so baked normal maps may show small seams.
- Material maps: base colour, metallic-roughness, tangent-space normal, occlusion,
  emissive.

| Model | Triangles | Parse | Image decode | GPU upload | GPU frame (median) |
|---|---:|---:|---:|---:|---:|
| DamagedHelmet | 15,452 | 0.5 ms | 124.3 ms | 35.6 ms | 0.071 ms |
| SciFiHelmet | 23,358 | 2.0 ms | 278.1 ms | 26.3 ms | 0.108 ms |
| Lantern | 5,394 | 0.2 ms | 121.3 ms | 25.9 ms | 0.045 ms |

Image decoding dominates load time. Textures are uncompressed RGBA8 with mips, about
106 MiB for DamagedHelmet. Texture compression is the obvious next step.

**Fuzzing that reaches the code.** Byte mutation of `.glb` files almost never gets past
the header: 164 of 9.3 M inputs loaded in 60 s. The fuzz target therefore has a
structure-aware mode that builds a well-formed GLB from the fuzz bytes:
- the input chooses vertex and index counts, the index type, which attributes and
  images exist, the primitive mode and a node scale;
- the remaining bytes become the binary chunk, so index values, vertex data and the
  embedded image are arbitrary.

With it, 11.47 % of 2.4 M inputs loaded fully and 72,851 produced triangles, with no
sanitizer reports in 60 s under ASan + UBSan.

## 8. Shadow mapping (U7)

The shadow pass renders depth only from the directional light into a 24-bit depth
texture.
- **Light frustum:** orthographic, fitted to the scene's bounding sphere.
- **Filtering:** a `sampler2DShadow` with hardware compare, where each tap is 2×2
  bilinear, sampled with an N×N kernel (1, 3×3 or 5×5).
- **Shadow acne:** prevented by combining slope-scaled bias, normal-offset bias and
  polygon offset.
- **Coverage:** works for both shading paths and both loaders.

Synchronous timing (see §9), median of 3 × 1000 frames
([`SYNC_SUMMARY.txt`](bench/u7_shadows/SYNC_SUMMARY.txt)):

| Scene | Shadows | Shadow pass | Mesh pass | Whole frame |
|---|---|---:|---:|---:|
| USC campus, 9,684 tris, 1200×800 | off | — | 0.233 ms | 0.903 ms |
| | 1024², PCF 3×3 | 0.181 | 0.267 | 1.124 |
| | 2048², PCF 1 | 0.207 | 0.233 | 1.115 |
| | 2048², PCF 3×3 | 0.208 | 0.262 | 1.149 |
| | 2048², PCF 5×5 | 0.206 | 0.336 | 1.226 |
| | 4096², PCF 3×3 | 0.364 | 0.292 | 1.367 |
| Sports Car, 300,603 tris, 600×600 | off | — | 0.986 | 1.850 |
| | 1024², PCF 3×3 | 0.833 | 1.030 | 2.792 |
| | 2048², PCF 3×3 | 0.833 | 1.049 | 2.791 |
| | 4096², PCF 3×3 | 1.056 | 1.140 | 3.125 |

Reading the table:
- PCF cost in the mesh pass grows with the number of taps: +0.000, +0.029 and
  +0.103 ms for 1, 9 and 25 taps on the campus.
- The Sports Car's shadow pass costs the same at 1024² and 2048², so it is limited by
  geometry; at 4096² resolution starts to cost.

## 9. When the profiler was wrong

Per-pass GPU timer queries on Apple's GL-over-Metal produced numbers that cannot be
true:
- the campus shadow pass read 0.000 ms;
- a 4096² shadow map was cheaper than a 2048² one;
- the Sports Car's total GPU time *dropped* when shadows were turned on.

The likely cause is the tile-based GPU: fragment work runs when the render pass is
flushed, not between the query's begin and end.

So the profiler gained `--sync-timing on`, which calls `glFinish` before and after each
pass and times it on the CPU. This is slower, and each bracket includes a fixed round
trip of about 0.13–0.20 ms, so only differences between configurations are meaningful.
The shadow numbers above use this mode.

Per-pass `gpu_*` values from earlier steps (§3, §6, §7) come from timer queries and
should be read as approximate. `cpu_frame` with vsync off is the end-to-end number.

## 10. Not done yet

- Cascaded shadow maps, and shadows from the point and spot lights.
- glTF alpha blending and masking, texture samplers, `KHR_*` extensions, animation.
- Texture compression; an HDR render target with bloom; caching the IBL precompute.
- An interactive camera; the view is currently set by command-line flags.
- A Windows build (the CMake/GLEW path is written but untested).
- The CI workflow has been run locally in a container but not yet on GitHub.
