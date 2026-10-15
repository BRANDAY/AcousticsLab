# 9. Thread-level parallelization of beam tracing

[← Guide index](README.md) · Milestone M7 · Status: **in progress** (being implemented at the time of writing; this page describes the plan and will be completed with the results)

Menu: **1.3 Thread-level parallelization (benchmark)** (opens `Level_Shoebox` with `?mode=Benchmark`; until M7 lands the mode is only shown in the HUD line).

Source: S3 — M. Sikora, I. Mateljan, *A method for speeding up beam-tracing simulation using thread-level parallelization*, Engineering with Computers 30(4), 679–688, 2014 (online 2013; DOI [10.1007/s00366-013-0316-z](https://doi.org/10.1007/s00366-013-0316-z)). The full text is not open access; this page relies on the abstract and on S2's description of the simulation.

## What S3 does (from the abstract)

The beam-tracing simulation of S2 consists of three algorithms. Two are iterative and are parallelized with a known technique; the paper's main contribution is the multithreaded version of the third, the **recursive generation of the octree** (in S2 §3.3.5 the octree holds the finished beams so that a receiver only tests the beams of its leaf). On three platforms the speed-up was significant, and linear when all threads ran on the same processor.

## What the bench will demonstrate

- **Branches of the beam tree are independent.** A root beam and everything it spawns by division, reflection and refraction never touch another root's nodes, so root beams can be built on different threads without locks — the bench parallelizes the **beam-tree build**, i.e. the iterative tracing part.
- **Speed-up versus thread count**, measured on this machine with an explicit number of threads (1, 2, 4, 8, …, all logical cores).
- **The effect of a spatial structure** on the triangle search: a BVH over the scene triangles, switchable off (`bUseBVH`) for a brute-force search. This is the bench's own acceleration of the candidate search — S2 orders triangles with a BSP tree and uses the octree for the *beams*, not the triangles.
- **Determinism.** The result must not depend on the number of threads: per-thread data, merged in a fixed order.

What the bench does **not** reproduce: the bench has no octree over the finished beams (receivers are tested against every node, see [7](07-image-sources-and-beam-tracing.md)), so S3's main contribution — parallel octree generation — has no counterpart yet. A beam octree with a parallel build would be the faithful next step.

## How it is being built (plan of M7)

1. The beam-tree build is split by root beam: a shared read-only context (emission directions, triangle → surface map, escape length) and a per-root breadth-first build into its own subtree; the subtrees are merged in root order, so the tree is identical to the sequential build.
2. `FParallelBeamTracer(NumThreads)` owns a dedicated `FQueuedThreadPool` with exactly `NumThreads` threads (UE's `ParallelFor` does not take a thread count); workers pull root beams from an atomic counter (dynamic scheduling, one task = one root beam).
3. `bUseBVH` applies everywhere — frustum candidates, centre/depth rays and the direct-sound occlusion test — so a brute-force run never touches the BVH; results must be identical, only slower.
4. Benchmark matrix: thread counts × {BVH on, off} × repetitions, with a warm-up run; per run total / build / query time, node count and a fingerprint of the result compared with the first run; per cell mean / min / max, speed-up $S_n = T_1 / T_n$, efficiency $S_n / n$, BVH gain; CSV export to `Saved/Benchmarks/`.
5. A benchmark panel in the HUD (fields subdivision level, max beam order, repetitions; Run / Stop / Export CSV; table and a speed-up chart with the ideal line) and a headless runner for the full matrix.
6. Tests planned: `AcousticsLab.Core.Solvers.BeamThreadCountInvariance` (identical trees, histograms and arrival counts for the sequential tracer and 1 / 2 / 4 / all threads, with and without refraction) and `AcousticsLab.Core.Solvers.BvhToggleInvariance` (BVH on and off give identical results).

## What already exists

- The **ray tracer** has been thread-count invariant since M2: rays are processed in fixed chunks of 1024 with per-chunk histograms summed in chunk order and a random stream per ray, so 1 and N workers give bit-identical histograms (test `AcousticsLab.Core.Solvers.ThreadCountInvariance`). The panel field **Threads (0 = all)** sets the number of workers.
- The BVH and its brute-force counterparts have been tested against each other since M1 (`BvhMatchesBruteForce`: 10 000 random rays on 4 000 random triangles; `BvhQueriesMatchBruteForce`: AABB and frustum queries).

## Expected acceptance (SPEC §4, M7)

A table 1 / 2 / 4 / 8 / N threads × BVH on/off in which the speed-up grows monotonically, and results that do not depend on the number of threads. The numbers will be added here from the M7 milestone document.

## Screenshots

To be taken after M7 (see [SCREENSHOTS_TODO.md](SCREENSHOTS_TODO.md)): the benchmark panel with the table and the speed-up chart.
