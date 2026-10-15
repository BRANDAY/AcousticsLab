# AcousticsLab — reviewer's guide

This guide explains, mode by mode, how the methods of prof. Marjan Sikora's papers were implemented and visualized in the AcousticsLab test bench (Unreal Engine 5.8, C++): what each mode demonstrates and which paper it comes from, the algorithm and its key formulas, what is drawn on screen, every setting of the panel, what is validated and how, how to run it, and where the implementation differs from the paper.

**Start here:** [the 10-minute walkthrough](walkthrough.md).

## Pages

| # | Page | Paper | Milestone | Status |
|---|---|---|---|---|
| 1 | [Overview, menu and controls](01-overview-and-controls.md) | — | M0, M2 | done |
| 2 | [Geometry, scene extraction and the BVH](02-geometry-and-scene.md) | (infrastructure for S1–S3) | M1, M6 | done |
| 3 | [Stochastic ray tracing](03-stochastic-ray-tracing.md) | reference method of S2 / late part of S4 | M2, M3 | done |
| 4 | [Room-acoustic metrics](04-room-acoustic-metrics.md) | metrics as in S6; Sabine / Eyring validation | M2 | done |
| 5 | [Visualization: paths, heatmaps, IR plot](05-visualization.md) | — | M3, M5, M6 | done |
| 6 | [Auralization: IR synthesis, convolution, A/B](06-auralization.md) | listening side of S4–S5 | M4, M5 | done (Dry / Ray / Beam) |
| 7 | [Image sources and beam tracing with beam division](07-image-sources-and-beam-tracing.md) | **S1**, S2 §3.2–3.3 | M5 | done |
| 8 | [Beam tracing with refraction: pool and lens](08-refraction.md) | **S2** | M6 | done |
| 9 | [Thread-level parallelization](09-parallelization.md) | **S3** | M7 | done |
| 10 | [Hybrid beam/ray tracing](10-hybrid.md) | **S4** | M8 | planned |
| 11 | [Industry comparison: Steam Audio, Audio Tracing](11-industry-comparison.md) | — | M9 | planned |
| 12 | [Amphitheatre and soundwalk](12-amphitheatre-and-soundwalk.md) | S4, **S5** | M10 | planned |
| — | [References](references.md) | S1–S6 with full citations | | |
| — | [Screenshots still to take](SCREENSHOTS_TODO.md) | | | |

The milestone documents in [`docs/milestones/`](../milestones/) hold the development record (plans, build logs, test logs, deviations) that this guide summarises; the specification is [`docs/SPEC.md`](../SPEC.md) (in Russian).

## Validation at a glance

30 automation tests, all green on the M7 build (`AcousticsLab.*`, headless command in [CLAUDE.md](../../CLAUDE.md)).

| Test | What it checks | Tolerance / criterion | Page |
|---|---|---|---|
| `Core.Smoke.Constants` | constants (bands, c, histogram size) | exact | — |
| `Core.Geometry.RayTriangle` | Möller–Trumbore hits, misses, edges, back faces | 1e-12 … 1e-9 | [2](02-geometry-and-scene.md) |
| `Core.Geometry.BvhMatchesBruteForce` | 10 000 random rays on 4 000 random triangles + shoebox | 0 mismatches | [2](02-geometry-and-scene.md) |
| `Core.Geometry.BvhQueriesMatchBruteForce` | 500 AABB + 500 frustum queries | identical sets | [2](02-geometry-and-scene.md) |
| `Core.Geometry.ShoeboxScene` | 12 triangles, 242 m², inward normals | 1e-9 | [2](02-geometry-and-scene.md) |
| `Core.Geometry.ShoeboxExtraction` | extraction from a real UE actor in a transient world | 12 triangles, 242 ± 0.1 m² | [2](02-geometry-and-scene.md) |
| `Core.Geometry.ClipPolygon` | Sutherland–Hodgman, inside/outside split on 2000 random pairs | areas 1e-9, all pieces convex | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Metrics.SyntheticDecay` | EDT / T20 / T30 / C50 / C80 / D50 / Ts on exact exponential decays; Sabine / Eyring | 2 % (achieved < 0.1 %); clarity 1e-6 dB | [4](04-room-acoustic-metrics.md) |
| `Core.Metrics.LevelHelpers` | level and direct-SPL helpers | 1e-9 | [5](05-visualization.md) |
| `Core.Solvers.RayTracerPrimitives` | Fibonacci sphere, reflection, Lambert mean cosine, segment–sphere, direct energy | 1e-12; Lambert 0.01 | [3](03-stochastic-ray-tracing.md) |
| `Core.Solvers.ShoeboxSabine` | ray-traced T30 vs Eyring, α = 0.1 / 0.2 / 0.3, diffuse walls | **≤ 20 %** (measured +2.4 … +8.8 %) | [4](04-room-acoustic-metrics.md) |
| `Core.Solvers.Deterministic` | same seed → bit-identical; other seed → different | exact | [3](03-stochastic-ray-tracing.md) |
| `Core.Solvers.ThreadCountInvariance` | 1 vs 4 vs all workers; BVH on vs off | bit-identical | [3](03-stochastic-ray-tracing.md) |
| `Core.Solvers.PathSampling` | stride sampling of drawn paths, invariants | exact | [5](05-visualization.md) |
| `Core.Visualization.Helpers` | viridis, order colours, opacity, heatmap samples | per value | [5](05-visualization.md) |
| `Core.Audio.IRSynthesis` | IR length, peak, per-bin energy, impulse placement, seeds | length ± 0.05 s; −3 dBFS ± 1e-6; energy 1e-6 | [6](06-auralization.md) |
| `Core.Audio.WavRoundTrip` | 16-bit and float WAV write/read | 1.5 codes / exact | [6](06-auralization.md) |
| `Core.Solvers.BeamEmission` | 20·4^L beams, solid angles sum to 4π | 1e-9 | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Solvers.ReceiverInsideBeam` | containment: side planes, reflection plane, hit plane; duplicates | exact | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Solvers.ImageSourceShoebox` | 162 images; first-order delays and energies; occlusion | 1e-12 | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Solvers.BeamVsImageSource` | beams vs image sources, orders ≤ 2 and ≤ 3, four receivers | **delays ± 0.1 ms, energies ± 1 %** | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Solvers.BeamTreeDeterminism` | two builds, BVH vs brute force, one receiver vs all | bit-identical | [7](07-image-sources-and-beam-tracing.md) |
| `Core.Geometry.Snell` | refraction angles, critical angle air → water 13.4006° | 1e-9 °; 1e-3 ° | [8](08-refraction.md) |
| `Core.Geometry.LeastSquaresApex` | refracted-beam apex fit | 1e-9 | [8](08-refraction.md) |
| `Core.Solvers.RefractionEnergy` | R + T = 1; child factors; transmitted delay under water | 1e-15; **± 0.1 ms**; energy within 25 % of paraxial estimate | [8](08-refraction.md) |
| `Core.Solvers.RefractedBeamTreeDeterminism` | pool with refraction: determinism, delays between all-air and all-water | bit-identical | [8](08-refraction.md) |
| `Core.Solvers.BeamThreadCountInvariance` | beam tree and results for the sequential tracer vs 1 / 2 / 4 / 12 pool threads, with refraction; truncated trees | bit-identical | [9](09-parallelization.md) |
| `Core.Solvers.BvhToggleInvariance` | BVH on vs brute force for the beam tracer (shoebox, split shoebox, pool) | bit-identical | [9](09-parallelization.md) |
| `Core.Solvers.BenchmarkMatrix` | benchmark matrix, summary, CSV, cancel | exact | [9](09-parallelization.md) |
| `Core.Solvers.LensFocus` | S2 lens scaled × 10: focus energy vs off-axis and in front | **focus > 2 × off-axis**, > front | [8](08-refraction.md) |

(All names start with `AcousticsLab.`; `ShoeboxExtraction` and `Visualization.Helpers` live in the game module because they need engine types.)

## Known simplifications (summary)

- **Energy model**: no phase, no interference; broadband (one band; six octave bands prepared in the data layout).
- **No diffraction, no scattering in the beam tracer** (as in S2); the ray tracer has Lambert scattering.
- **Refracted beam apex**: least-squares point of the three refracted guide rays (S2 uses the mean of the pairwise intersections); R / T at normal incidence for every angle.
- **Only the beam tracer knows media**; the ray tracer and image sources treat interfaces as walls.
- **No beam octree** for receiver lookup (linear scan of the tree); the parallel speed-up is limited by the largest root subtree.
- **Geometry from render data**: works in the editor, PIE and standalone-from-editor; a packaged build would need meshes with CPU access.
- **Placeholder anechoic clip** (synthetic test signal) until a real anechoic recording is added.
- Details and the reasons are on each page under "Differences".
