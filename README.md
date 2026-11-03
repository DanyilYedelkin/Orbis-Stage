# Orbis — planet-scale terrain renderer

A whole-planet terrain renderer in modern C++20 and OpenGL 4.5, built around the problems real
geospatial and military simulators have to solve: **real Earth elevation data on a WGS84 ellipsoid,
planetary coordinate precision, continuous level of detail from orbit to the ground, culling at
planetary scale, and streaming terrain generation on worker threads.**

![Earth from orbit](docs/earth-orbit.png)

| The Alpine arc from 70 km (real data + synthetic detail) | High Tatras from the south |
|---|---|
| ![Alps](docs/alps.png) | ![Tatras](docs/tatras.png) |

| Procedural planet (`--world procedural`) | Quadtree LOD levels (debug view, F3) |
|---|---|
| ![Procedural](docs/procedural.png) | ![LOD levels](docs/lod-levels.png) |

| Sunset: colours from physics, not from a palette | Debug panel |
|---|---|
| ![Sunset](docs/sunset.png) | ![Debug panel](docs/debug-panel.png) |

*All frames rendered headless in CI with Mesa's software OpenGL (llvmpipe).*

## Highlights

### Real Earth

**Measured elevation.** NOAA ETOPO1 (ice surface, 10 arc-minutes ≈ 18 km), converted by
`tools/prepare_etopo.py` into a 4.7 MB grid that ships with the repository. The converter verifies
the source by SHA-256 and recovers the grid layout from the point coordinates instead of assuming
it — the source turned out to start at Greenwich, not at −180°, which a check against known
places (Everest, Mariana Trench, Košice, the Netherlands' polders below sea level) caught.

**Sampling done properly.** Bicubic (Catmull-Rom) interpolation, because bilinear is only C0 and
every grid line would show as a lighting crease below the data resolution. A low-passed mip
pyramid so coarse LOD levels read pre-filtered data instead of aliasing. Rows beyond a pole
continue on the opposite meridian and longitude wraps, so the poles and the date line are not edges.

**Synthetic detail below the data resolution** — the standard simulator approach of "measured base,
plausible detail". The detail starts exactly at the data's Nyquist limit, and its strength comes
from a precomputed *relief map* (how much the data deviates from its own smoothed version): ~5 m on
plains, ~300 m in the Tatras, ~700 m in the Alps. Absolute height would be a poor guide — the
Tibetan plateau is high but flat. Where the data says land, detail may not create sea (otherwise
every lowland fills with puddles); the other way round gives natural fractal coastlines.

**One interface, two worlds.** Quadtree, mesh builder and worker threads only see
`ElevationSource`; `--world earth|procedural` switches between ETOPO and the procedural planet.

*Honest limits:* 18 km data shows real mountain ranges from altitude, but foothills rather than
peaks from the ground — that needs higher-resolution tiles (see roadmap). Land cover is a
latitude/elevation stylisation (arid subtropical belts, temperate forest, taiga, tundra, tree, rock
and snow lines fitted to real references), so e.g. monsoon Nepal comes out arid.

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

**Geomorphing: no popping.** Every vertex also stores where it would lie on its *parent's*
surface (even vertices are parent vertices; the others lie on a parent edge or on the diagonal the
parent's triangulation uses). The vertex shader blends the two by distance, in a window that ends
just before the parent's split distance and starts after the chunk's own split distance — so a new
chunk appears looking exactly like its parent and grows its detail as the camera approaches, and
its own children later start from its full-detail surface. The blend depends only on vertex
*position*, so neighbours agree on shared vertices and no crack opens mid-transition. A test
builds a real parent mesh and checks that a fully morphed child coincides with it vertex by vertex.

**Measured, not eyeballed.** `--record N,metres --record-lod-only` keeps the view fixed and moves
only the LOD camera, so every change between frames comes from LOD transitions alone. Over a
30-frame pull-back above procedural mountains, pixels that jump by more than 25 levels drop from
0.127 % to 0.021 % per frame on average (**6×**) and **19×** in the worst frame. (The first version
of the experiment moved the LOD camera *forward* — then frustum culling around the fixed view
dominated everything; moving it backwards keeps the view inside the LOD frustum.)

![Geomorphing comparison](docs/geomorph-diff.png)

*Change between two consecutive frames at the worst LOD transition: without geomorphing (left) a
whole chunk pops, with it (right) only small, gradual changes remain.*

Vertices are 40 bytes: normals are packed into `GL_INT_2_10_10_10_REV` (~0.1° precision) so the
second set of attributes costs 12 bytes instead of 24.

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
face edges bit-identical, tested). **Procedural planet**: band-limited continents, ridged-multifractal
mountain ranges and fractal detail down to ~1 m, sampled on the unit sphere (no pole stretching, no
date-line seam). Octaves finer than a chunk can represent are faded out (Nyquist), so coarse levels
do not alias, and the normalisation keeps the large-scale shape identical across levels.
**Deterministic** across compilers (own PRNG instead of `std::shuffle`). **Seamless normals**
computed from the height field around each vertex's direction, bit-identical across chunk borders.

### Debug panel

Dear ImGui (immediate mode: the panel is re-described from the live state every frame, so it can
never show stale values). Frame-time graph, camera position, jump-to presets (Europe, Alps, Tatras,
Himalaya, Andes), LOD statistics and controls (pixel error, max level, geomorphing, LOD colours,
wireframe, freeze), and the sun's local azimuth / elevation. Input over the panel is not passed to
the camera. `F4` hides it.

### Physically based atmosphere

After S. Hillaire, *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*
(EGSR 2020) — the approach used by Unreal Engine. Rayleigh scattering (air molecules), Mie
scattering (aerosols, Cornette-Shanks phase) and ozone absorption with physical coefficients. Two
lookup tables are computed once at startup by **compute shaders**: transmittance (Bruneton's
parameterisation, texels concentrated near the horizon) and Hillaire's multiple-scattering
approximation (second-order light over 64 directions, higher orders as a geometric series). Per
frame, every sky pixel and every terrain fragment integrates in-scattering along its view ray
(analytically per step, so few steps stay energy-conserving). The blue sky, the pale horizon,
the red low sun, orange light on the mountains at dusk, the planet's glowing limb and the haze
that makes distant ranges fade into exactly the sky colour behind them all *emerge* from the
same integral — none of them is painted.

- **Single source of truth.** The physical constants live in one C++ struct; the GLSL constants
  are generated from it, so CPU tests and GPU shaders cannot drift apart. Unit tests check the CPU
  model against the closed-form optical depth (to 10⁻⁴) and that the horizon sun is red.
- **GPU validated against CPU at startup.** The transmittance table is read back and compared
  with the C++ reference: the largest difference is 5·10⁻⁴, i.e. half-float precision.
- **WGS84 in a spherical model.** Atmosphere models assume a sphere; scaling z by a/b maps the
  ellipsoid onto one (the same trick as the horizon culling), so altitudes stay correct to 0.3 %.
- Sun light reaching each surface point is dimmed and reddened by the air above it and softly
  cut by the planet's shadow (mountain tops keep the light after the valleys lose it). Skylight
  is still a simple twilight-scaled ambient term — an irradiance table is a possible next step.

ACES filmic tone mapping turns the HDR result into display values.

### Engineering

- `orbis_core` has no OpenGL or windowing dependency: math, geodesy, terrain, LOD and the thread
  pool are all unit-tested — **74 Catch2 tests**, ~460 000 assertions, including checks against
  known geography, parent/child geomorph consistency and the analytic atmosphere.
- CI: Windows (MSVC) + Linux (GCC), Debug + Release, warnings as errors
  (`/W4`, `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion ...`),
  **AddressSanitizer + UBSan**, **ThreadSanitizer**, and a headless render job that uploads frames.
- Dependencies pinned by version **and** SHA-256; shaders embedded at build time, shared GLSL with
  `#line` so compiler errors point at the right line.
- Modern OpenGL: Direct State Access, immutable storage, compute shaders, RAII ownership of every
  GL object, `KHR_debug` output enabled right after context creation in debug builds (so GPU work
  done while other objects are still being constructed is checked too); member order guarantees GL objects die before the context and
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
orbis [--world earth|procedural] [--elevation file.oeg] [--size WxH]
      [--view lat,lon,altKm,heading,pitch] [--sun azimuth,elevation]
      [--lod-colors] [--no-geomorph]
      [--screenshot file.ppm [--show-ui] [--record frames,metres [--record-lod-only]]]
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
| F4 | Show / hide the debug panel |
| Esc | Quit |

The window title shows FPS, altitude, position, chunks and triangles drawn, deepest level and the
build queue. Places to start:

```
orbis                                                   # Europe from orbit, above Kosice
orbis --view 44.3,8.6,70,5,-22 --sun 235,25             # the Alps from 70 km
orbis --view 48.95,20.10,2.6,8,-3 --sun 250,16          # the High Tatras
orbis --world procedural --view 0.30,-140.75,6.5,45,-10 # procedural mountains
```

The Earth data is looked up next to the executable (`data/`, copied there by the build) and then
in the source tree; `--elevation` points to another `.oeg` file.

## Project layout

```
src/core/      engine core, no GPU dependency
  math/        Vec3, Mat4 (reversed-Z projection), Frustum
  geo/         WGS84 ellipsoid, geodetic <-> ECEF, ENU frames, horizon culling
  terrain/     cube-sphere, noise, elevation sources (ETOPO grid, procedural), chunk meshes +
               skirts, quadtree LOD, thread pool
  scene/       ENU flight camera
  atmosphere/  physical atmosphere model (CPU reference) and GLSL constant generation
src/app/       window, terrain streaming + GPU cache, sky, debug panel, OpenGL wrappers, app loop
shaders/       GLSL (incl. atmosphere compute shaders), embedded into the executable at build time
data/          earth_etopo10.oeg - ETOPO1 elevation grid (generated by tools/prepare_etopo.py)
tools/         data preparation scripts
tests/         Catch2 unit tests
```

## Roadmap

- [x] **Foundation:** WGS84, cube-sphere, camera-relative rendering, reversed-Z, tests, CI.
- [x] **LOD and streaming:** quadtree with screen-space error, frustum + ellipsoid horizon culling,
      skirts, worker-thread generation with a re-prioritised queue, GPU cache with eviction.
- [x] **Real Earth:** ETOPO1 elevation, bicubic + mip-filtered sampling, relief-driven synthetic
      detail, `ElevationSource` interface.
- [ ] **High-resolution tiles:** stream regional DEM tiles (ETOPO 2022 1′, SRTM) on demand.
- [ ] **Land cover data** instead of the latitude stylisation.
- [x] **Smooth transitions and tooling:** geomorphing (measured 6× less popping), ImGui debug
      panel, LOD recording for objective comparisons.
- [x] **Physically based atmosphere:** Hillaire 2020 LUTs in compute shaders, sky and aerial
      perspective, GPU/CPU validation.
- [ ] **Sky irradiance table** for physically based ambient light; clouds.

## References

- NOAA NCEI — *ETOPO1 1 Arc-Minute Global Relief Model* (doi:10.7289/V5C8276M), public domain;
  10′ resampling from the PyVista example-data repository.
- NIMA TR8350.2 — *Department of Defense World Geodetic System 1984*.
- P. Cozzi, K. Ring — *3D Engine Design for Virtual Globes* (2011).
- T. Ulrich — *Rendering Massive Terrains using Chunked Level of Detail Control* (SIGGRAPH 2002),
  including geomorphing.
- O. Cornut — *Dear ImGui*.
- K. Perlin — *Improving Noise* (2002); F. K. Musgrave — *Texturing and Modeling: A Procedural
  Approach*, ch. on ridged multifractals.
- N. Reed — *Depth Precision Visualized* (reversed-Z).
- S. Hillaire — *A Scalable and Production Ready Sky and Atmosphere Rendering Technique* (2020).
- E. Bruneton, F. Neyret — *Precomputed Atmospheric Scattering* (2008); E. Bruneton's 2017
  reference implementation (transmittance parameterisation).
