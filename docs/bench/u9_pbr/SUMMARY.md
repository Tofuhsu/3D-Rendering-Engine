Machine: Apple M4 Pro | 4.1 Metal - 90.5; viewport 600x600, vsync off; 1000 frames after 100 warm-up; median of 3 runs.

| Model | Vertices | Triangles | OBJ parse ms | MTL+textures ms | Draws | CPU frame median ms | CPU frame p95 ms | GPU total median ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Rose-pbr | 98,054 | 166,844 | 69.7 | 230.5 | 6 | 0.428 | 4.106 | 0.191 |
| Rose-phong | 98,054 | 166,844 | 73.0 | 230.6 | 6 | 0.406 | 4.023 | 0.224 |
| SphereGrid800-pbr | 13,975 | 24,000 | 9.5 | 0.1 | 28 | 0.476 | 4.181 | 0.183 |
| SphereGrid800-phong | 13,975 | 24,000 | 9.3 | 0.1 | 28 | 0.434 | 3.978 | 0.156 |
| SportsCar-pbr | 163,634 | 300,603 | 109.7 | 0.1 | 41 | 0.486 | 4.205 | 0.286 |
| SportsCar-phong | 163,634 | 300,603 | 110.2 | 0.1 | 41 | 0.481 | 4.075 | 0.298 |
