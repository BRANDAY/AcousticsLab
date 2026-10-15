# A 10-minute walkthrough

[← Guide index](README.md)

A guided tour of what the bench shows today (milestones M0–M7), in the order a reviewer would want to see it. Each step names what to press, what to look at, and where the details are. Times are on the development machine (10 worker threads).

**Before you start:** open `AcousticsLab.uproject` in Unreal Engine 5.8.3 (build instructions in the [README](../../README.md#quick-start)). Headphones help in step 5. Keys: **Tab** switches between the panel (cursor) and flying (WASD + mouse, E/Q up/down); **Esc** returns to the menu in Standalone mode.

---

## 1. The menu (30 s)

Play as **Standalone Game**. The menu mirrors the structure of the work: **1. Methods of M. Sikora** (one entry per paper), **2. Industry** (Steam Audio, Audio Tracing — planned), **3. Compare** (planned). Items that are not built yet open a placeholder level; Esc comes back.

![main menu](img/main_menu.png)

→ Choose **1.1 Image sources & beam tracing - Shoebox**.

## 2. The validation room and the ray tracer (2 min)

`Level_Shoebox` is a 10 × 7 × 3 m concrete room (α = 0.03, s = 0.1) with a source S1, four receivers R1–R4 (spheres of 0.5 m) and a 10 × 7 grid of receivers on the floor.

1. In the panel (top right) press **Run**. In about 1–3 s: `20000 rays, 12 triangles, mean order 454.0, 9079967 segments`.
2. Read the metric block for R1: EDT ≈ 4.71 s, T30 ≈ 4.72 s, C80 ≈ −5.8 dB; and the yellow reference: Sabine 4.66 s, Eyring 4.59 s, **T30 +2.9 % vs Eyring**. The pipeline (geometry → rays → histogram → Schroeder curve → regression) agrees with the analytic reverberation time.
3. The room fills with ray paths: white = direct, yellow = 1st, orange = 2nd, red = 3rd reflection; fainter = less energy. Tick **Only to receiver**: only the paths that enter R1 within three reflections remain.
4. Bottom left, the impulse response of R1: the energy per millisecond (thin) and the Schroeder decay (thick), with the −5/−35 dB fit range and the 50/80 ms marks.

![ray paths to R1](../milestones/img/M3_paths_to_receiver.png)

Details: [3. ray tracing](03-stochastic-ray-tracing.md), [4. metrics](04-room-acoustic-metrics.md) (with the Sabine/Eyring table for α = 0.1–0.3: deviations +2.4 … +8.8 % for diffuse walls).

## 3. Heatmaps over a receiver grid (1 min)

The floor tiles are 70 receivers. Change **Metric**: **C80** (−5.9 … −2.0 dB, clearer near the source), **T30** (4.70 … 4.75 s — nearly uniform, as a shoebox should be), **Direct SPL** (falls by 6 dB per doubling of distance). Untick **Ray paths** to see the tiles clearly.

![direct SPL heatmap](../milestones/img/M3_heatmap_direct_spl.png)

Details: [5. visualization](05-visualization.md).

## 4. Image sources and beams — the early reflections (2 min)

1. **Solver** = `ImageSource`, **Run** (≈ 0.02 s): `162 image sources` (orders 1–3), spheres coloured by order, polylines source → walls → R1 for every valid path. Press `>`: the paths switch to R2 instantly.
2. **Solver** = `BeamTracer`, **Run** (≈ 0.2 s): `320 root beams, 22925 beam nodes`. Tick **Beams** and **Only with receiver**, slider **Order <=** to 3: the translucent beams that contain R1, and their parent beams back to the source. The receiver test is a point-in-beam query — no back-tracing — so all 74 receivers are answered from one beam tree.
3. The two solvers find the same reflections: the test `BeamVsImageSource` requires equal sets of paths with **delays within 0.1 ms and energies within 1 %**.
4. The IR plot shows discrete early arrivals; T30 is `n/a` and C80 = 100 dB because these solvers model only the early part — the motivation for the hybrid method (S4, planned).

![beams containing R1](img/M5_beams_receiver_crop.png)

Details: [7. image sources and beam tracing](07-image-sources-and-beam-tracing.md) — including how the beam division of S1/S2 (projection onto a plane across the beam, clipping, child beams with mirrored apexes) is done here.

## 5. Listening (1 min)

1. After the beam run the anechoic clip plays (variant **Dry**). Press **3**: the clip through the beam response of R1 — direct sound plus discrete early echoes.
2. **Solver** = `RayTracer`, **Run**, press **2**: the full concrete room, a 4.7 s tail. Press **1** to compare with the dry clip — the switch is instantaneous.
3. Press `>` to **R3** (1 m from the source): onsets become sharp and close; `>` again to **R4** (8.5 m, far corner): distant and diffuse. No solver rerun is needed — every receiver's response was synthesized after the run.

Responses are also written to `Saved/IR/Level_Shoebox_<Solver>_<Receiver>.wav` (48 kHz float).

Details: [6. auralization](06-auralization.md).

## 6. Refraction: the pool (2 min)

Esc → **1.2 Beam tracing with refraction**. The level's manager uses the beam tracer; source `Pool` is 1.5 m above a water surface in a 20 × 10 × 3 m pool.

> The panel carries the settings of the last run over to the next level of the same session (one shared set). Coming from the shoebox, set them for this level before running: tick **Beams: refraction at interfaces**, **Beams: subdivision** = 4, **Beams: max order** = 3, **T max, s** = 1, **Receiver r, m (0 = own)** = 0. (Opening `Level_Refraction` first, or directly in the editor, uses the level's saved values.)

1. **Run** (≈ 30 s). Result line: `Refraction: 1616 refracted beams, 22192 total internal reflections …`.
2. **Beams** on, slider to 1: above the water the reflected beams (yellow); under the water the refracted beams (cyan) only in a narrow cone below the source — beyond 13.4° incidence (the critical angle air → water) sound is totally reflected. The underwater heatmap is brightest below the source.
3. **Only with receiver**, slider to 3: the beam chains that reach R1 under the water bend at the surface.
4. The panel note states the one approximation of this mode: the apex of a refracted beam is the least-squares point of its three refracted guide rays (a refracted beam has no exact apex, S2 §3.1.2).

![pool, beams to R1](../milestones/img/M6_pool_receiver.png)

## 7. Refraction: the acoustic lens of S2 (1.5 min)

1. **Source** = `Lens`, **Beams: subdivision** = 6, **Run** (≈ 17 s). The S2 lens experiment scaled × 10: a rubber sphere (900 m/s) in glycerol (1920 m/s), a 4.5° directional source 7.5 m away.
2. Fly 60 m along +Y (Tab, then WASD) to the lens: the beams enter the sphere and converge towards its back; the vertical heatmap through the axis shows the focal spot inside the back half of the sphere, where the ball-lens formula $nR/(2(n-1)) \approx 0.47$ m puts it.
3. The test `LensFocus` requires the focus receiver to get more than twice the energy of receivers 0.3 m off the axis.

![lens focus](../milestones/img/M6_lens_focus.png)

Details: [8. refraction](08-refraction.md).

## 8. What is next

- **1.3 Thread-level parallelization (S3)** — done: the beam-tree build on a thread pool, a benchmark panel with a speed-up chart (about 4× on 6 cores, bit-identical results); 1 minute with the default settings ([9](09-parallelization.md)).
- **1.4 Hybrid beam/ray tracing (S4)** — planned ([10](10-hybrid.md)).
- **2. Industry** and **3. Compare** — the same amphitheatre in Steam Audio and Audio Tracing: planned ([11](11-industry-comparison.md)).
- **Amphitheatre, audience/velarium, soundwalk (S5)** — planned ([12](12-amphitheatre-and-soundwalk.md)).
