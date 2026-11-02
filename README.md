# Orbis — planet-scale terrain renderer

A whole-planet terrain renderer in modern C++20 and OpenGL 4.5, built around the problems real
geospatial and military simulators have to solve: **planetary coordinate precision, WGS84 geodesy,
continuous level of detail from orbit to the ground, culling at planetary scale, and streaming
terrain generation on worker threads.**

![Mountains from 6.5 km](docs/mountains.png)

| Quadtree LOD levels (debug view, F3) | Orbit |
|---|---|
| ![LOD levels](docs/lod-levels.png) | ![Orbit](docs/orbit.png) |

*All frames rendered headless in CI with Mesa's software OpenGL (llvmpipe).*

## Highlights

### Level of detail

**Chunked quadtree LOD with screen-space error.** Each of the 6 cube faces is a quadtree, down to
level 18 (~1.2 m between vertices). A node is refined when its vertex spacing, projected onto the
screen at the node's nearest point, exceeds a pixel budget (default 4 px, `[` / `]` to change). The
result: ~200 k triangles for the whole planet from orbit, ~2 M triangles at ground level with
detail from the foreground to the horizon.

**Selection is a pure function** of the view and of which chunks are resident — no hidden state,
fully unit-tested. If the children of a node are not ready yet, the coarser node keeps being drawn
and the children are requested: the image never has holes, it sharpens as data arrives.

**Tight bounds.** Culling needs bounding volumes *before* a mesh exists. Bounds start conservative
(0 – 11 km of terrain) and tighten to the real height range of each mesh once it is built; a
not-yet-built child inherits its parent's *subtree* range. Tests check these bounds against real
meshes. (Loose bounds were caught by a test: they inflated a ground-level view from ~1 000 to
~24 000 chunks.)

**Frustum culling + exact ellipsoid horizon culling.** The horizon test runs in ellipsoid-scaled
space, where WGS84 becomes the unit sphere and lines of sight stay straight, so the horizon is the
real one — an inscribed sphere would be 21 km too small at the equator and keep hundreds of
invisible chunks alive.

**Skirts against LOD cracks.** Neighbouring chunks at different levels do not share vertices; each
chunk has a skirt hanging below its border, wound to face outwards, so a crack shows terrain-coloured
skirt instead of sky.

### Streaming

**Background mesh generation.** A thread pool builds chunk meshes; the main thread only uploads
finished ones (bounded per frame, no hitches). The request queue is *replaced* every frame rather
than appended to: requests the moving camera no longer needs are dropped before anyone wastes time
on them, the rest is re-prioritised by screen-space error. Shutdown is immediate even with
thousands of queued requests (a test caught it draining the whole queue first).

**GPU chunk cache** with time-based and LRU eviction (bounded memory; chunks drawn this frame are
never evicted).

### Precision and geodesy

**Camera-relative rendering.** A 32-bit float has ~7 significant digits: at Earth-radius distances
its resolution is ~0.5 m, which shows up as shaking vertices. World positions stay in `double`
(ECEF); each chunk stores `float` vertices relative to its own origin; per frame the CPU computes
`chunkOrigin − cameraPosition` in double. The GPU only ever sees small numbers.

**Reversed-Z with an infinite far plane.** One depth buffer from 0.5 m to infinity:
`glClipControl(GL_ZERO_TO_ONE)` + 32-bit float depth + a projection mapping near → 1, ∞ → 0.

**WGS84.** Exact geodetic → ECEF, an iterative inverse stable at the poles (< 1 mm round-trip error
from −11 km to 10 000 km altitude), ENU frames, terrain displaced along the geodetic vertical.

### Terrain

**Spherified cube** (cell areas within 4/3 of each other instead of 5.2× for naive normalisation;
face edges bit-identical, tested). **Procedural, band-limited relief**: continents, ridged-multifractal
mountain ranges and fractal detail down to ~1 m, sampled on the unit sphere (no pole stretching, no
date-line seam). Octaves finer than a chunk can represent are faded out (Nyquist), so coarse levels
do not alias, and the normalisation keeps the large-scale shape identical across levels.
**Deterministic** across compilers (own PRNG instead of `std::shuffle`). **Seamless normals**
computed from the height field around each vertex's direction, bit-identical across chunk borders.

### Rendering

Sky pass sharing its atmosphere functions with the terrain haze (distant mountains fade into the
sky behind them); haze based on the *air path length*, so orbit views get a light tint and
ground-level views get real aerial perspective; ACES filmic tone mapping; sun disk and dusk colours.

### Engineering

- `orbis_core` has no OpenGL or windowing dependency: math, geodesy, terrain, LOD and the thread
  pool are all unit-tested — **54 Catch2 tests**, 365 000 assertions.
- CI: Windows (MSVC) + Linux (GCC), Debug + Release, warnings as errors
  (`/W4`, `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion ...`),
  **AddressSanitizer + UBSan**, **ThreadSanitizer**, and a headless render job that uploads frames.
- Dependencies pinned by version **and** SHA-256; shaders embedded at build time, shared GLSL with
  `#line` so compiler errors point at the right line.
- Modern OpenGL: Direct State Access, immutable buffer storage, RAII ownership of every GL object,
  `KHR_debug` output in debug builds; member order guarantees GL objects die before the context and
  worker threads join before the data they read.

## Building

Requirements: CMake ≥ 3.25, a C++20 compiler (MSVC 2022, GCC 12+, Clang 16+), a GPU with
OpenGL 4.5. macOS is not supported (Apple stops at OpenGL 4.1).

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

These commands **build and test**; they do not start the program. Run it from:

| Generator | Executable |
|---|---|
| Visual Studio (Windows default) | `build\release\src\app\Release\orbis.exe` |
| Ninja / Makefiles | `build/release/src/app/orbis` |

In Visual Studio you can instead *Open Folder*, pick the `release` preset and `orbis.exe` as the
startup item, and press F5. On Linux install the X11 headers first:
`sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`.

## Running

```
orbis [--size WxH] [--view lat,lon,altKm,heading,pitch] [--sun azimuth,elevation]
      [--lod-colors] [--screenshot file.ppm]
```

| Input | Action |
|---|---|
| Right mouse + move | Look around |
| W A S D | Fly |
| Space / E, Ctrl / Q | Up / down along the local vertical |
| Shift | 4× speed |
| Mouse wheel | Speed multiplier (speed also scales with altitude) |
| `[` / `]` | More / less detail (pixel error) |
| F1 | Wireframe |
| F2 | Freeze LOD — keep the selection of this moment and fly around it to watch culling |
| F3 | Colour chunks by quadtree level |
| Esc | Quit |

The window title shows FPS, altitude, position, chunks and triangles drawn, deepest level and the
build queue. A nice place to start: `orbis --view 0.30,-140.75,6.5,45,-10`.

## Project layout

```
src/core/      engine core, no GPU dependency
  math/        Vec3, Mat4 (reversed-Z projection), Frustum
  geo/         WGS84 ellipsoid, geodetic <-> ECEF, ENU frames, horizon culling
  terrain/     cube-sphere, noise, height field, chunk meshes + skirts, quadtree LOD, thread pool
  scene/       ENU flight camera
src/app/       window, terrain streaming + GPU cache, sky, OpenGL wrappers, application loop
shaders/       GLSL, embedded into the executable at build time
tests/         Catch2 unit tests
```

## Roadmap

- [x] **Foundation:** WGS84, cube-sphere, camera-relative rendering, reversed-Z, tests, CI.
- [x] **LOD and streaming:** quadtree with screen-space error, frustum + ellipsoid horizon culling,
      skirts, worker-thread generation with a re-prioritised queue, GPU cache with eviction.
- [ ] **Real Earth:** global elevation data (ETOPO / GEBCO) with procedural detail on top.
- [ ] **Smooth transitions:** geomorphing between LOD levels; ImGui debug panel; physically based
      atmospheric scattering.

## References

- NIMA TR8350.2 — *Department of Defense World Geodetic System 1984*.
- P. Cozzi, K. Ring — *3D Engine Design for Virtual Globes* (2011).
- T. Ulrich — *Rendering Massive Terrains using Chunked Level of Detail Control* (SIGGRAPH 2002).
- K. Perlin — *Improving Noise* (2002); F. K. Musgrave — *Texturing and Modeling: A Procedural
  Approach*, ch. on ridged multifractals.
- N. Reed — *Depth Precision Visualized* (reversed-Z).
