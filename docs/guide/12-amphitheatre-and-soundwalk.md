# 12. Amphitheatre and soundwalk

[← Guide index](README.md) · Milestone M10 · Status: **partly done** — amphitheatre, audience / velarium, cavea heatmap and experiments E1 / E5 are built (a "fast pass", [M10.md](../milestones/M10.md)); the soundwalk, the industry levels on the new geometry and the remaining experiments are still planned.

Menu: **1.1 Image sources & beam tracing - Amphitheatre** (`Level_Amphitheatre`) and **1.5 Audio AR soundwalk (optional)** (`Level_Amphitheatre?mode=Soundwalk`; for now it opens the same level without a soundwalk mode).

Sources: S4 (the hybrid method, motivated by long reverberation in large spaces) and S5 — M. Sikora, M. Russo, J. Đerek, A. Jurčević, *Soundscape of an Archaeological Site Recreated with Audio Augmented Reality*, ACM TOMM 14(3), 2018 (DOI [10.1145/3230652](https://doi.org/10.1145/3230652)): a smartphone audio-AR system that uses location and orientation to play the synthesized soundscape of a medieval Croatian site through headphones, evaluated by an in-situ soundwalk; and the earlier Sikora, Grčić, Russo, *A tool for soundscape auralization of ancient archaeological sites* (7th Congress of the Alps Adria Acoustics Association, Ljubljana, 2016, [record](https://www.bib.irb.hr/838151)): a game-engine tool that places virtual sound sources in a site and auralizes the result on location.

| empty | audience | velarium |
|---|---|---|
| ![empty](../milestones/img/M10_heatmap_empty.png) | ![audience](../milestones/img/M10_heatmap_audience.png) | ![velarium](../milestones/img/M10_heatmap_velarium.png) |

*`Level_Amphitheatre` in PIE after a ray-tracer run (40 000 rays), C80 heatmap over the cavea grid, panel with the metrics of R1. With the audience the rows turn from ≈ 0 dB to ≈ 10 dB.*

## The scene

An elliptical amphitheatre in the spirit of the one in Salona, built procedurally by `AAmphitheatreActor` (`Source/AcousticsLab/Public/Levels/AmphitheatreActor.h`) from three dynamic meshes (`UDynamicMeshComponent`, GeometryCore's `FDynamicMesh3`, the mesh type under Geometry Script):

| Part | Geometry | Material |
|---|---|---|
| **ArenaFloor** | the arena ellipse, semi-axes 32.5 × 20 m (65 × 40 m), facing up | `Stone` (α 0.02, s 0.20; new preset, limestone) |
| **Cavea** | one closed shell swept around the arena, 96 segments: 20 rows, row $k$ has a riser at offset $(k-1)\cdot 0.8$ m and a tread at height $k \cdot 0.75$ m; back wall at 16 m offset (top row at 15 m); "offset $o$" = the ellipse with semi-axes $(32.5 + o,\ 20 + o)$ | `Stone`, or `Audience` (α 0.7) with **Audience** on |
| **Velarium** | a closed 0.1 m ring over the cavea at 18 m, open over the arena | `Absorber` (α 0.9); empty mesh when **Velarium** is off |

Normals are wound per face (treads up, risers toward the arena, back wall outward); the meshes are tagged `Acoustic` and enter the solvers through the usual scene extraction ([2](02-geometry-and-scene.md)): 8 160 triangles, 8 928 with the velarium. The curved rows are plain triangle meshes, which the beam and ray tracers handle directly.

**Positions** come from `DA_AmphitheatrePositions` with the same offset rule, so the receivers sit 1.1 m above their treads: S1 at the arena centre 1.6 m high, R1 row 1 (20.4 m), R2 row 10, R3 row 20, R4 row 10 on the major axis (table in [11](11-industry-comparison.md#the-scene-and-the-positions)).

**Cavea grid**: 856 receivers on a 2 m plan grid clipped to the cavea, each on its row's tread (row found by bisection on the offset ellipses), 1.1 m above it (`AReceiverGrid` with explicit tile centres).

**Toggles**: select the `Amphitheatre` actor and tick **Audience** / **Velarium**, or in PIE `Acoustics.Amphitheatre.Audience 1|0`, `Acoustics.Amphitheatre.Velarium 1|0`, then **Run** again.

## How to run it

1. Main menu → **1.1 Image sources & beam tracing - Amphitheatre** (or open `Level_Amphitheatre`, Play). The ray tracer runs on BeginPlay (20 000 rays, R1–R4 + the cavea grid); the camera starts above the cavea looking at the arena.
2. The heatmap colours the rows (C80 by default; **Metric** in the panel); `<` / `>` switch R1–R4 for the metric block, the IR plot and listening (keys 1–2).
3. Toggle audience / velarium (above), **Run**, compare.
4. Headless experiments (editor closed): `UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<project>/Tools/Experiments/run_m10_experiments.py" -unattended -nopause -nosplash -NullRHI` → CSVs and `m10_tables.md`.

## Experiment E5 — empty / audience / velarium (SPEC §5.5)

Ray tracer, 200 000 rays, source S1. R1–R4 are point receivers; "row" values are means over the grid points of the row. T30 "n/a" = the Schroeder curve does not reach −35 dB within 3 s or the fit gives more than 10 s. CSV: [`docs/results/E5_amphitheatre_rows.csv`](../results/E5_amphitheatre_rows.csv).

| condition | R1 C80 / T30 | R2 C80 / T30 | R3 C80 / T30 | R4 C80 / T30 | triangles | trace, s |
|---|---|---|---|---|---|---|
| empty | −1.8 dB / n/a | −2.0 dB / n/a | −2.3 dB / n/a | 6.6 dB / n/a | 8160 | 3.3 |
| audience | 10.8 dB / 1.06 s | 9.3 dB / n/a | 10.7 dB / n/a | 10.5 dB / n/a | 8160 | 2.9 |
| velarium | −1.9 dB / n/a | −2.0 dB / n/a | −2.1 dB / n/a | 5.8 dB / n/a | 8928 | 5.5 |
| audience + velarium | 10.4 dB / 1.05 s | 9.1 dB / 1.1 s | 10.5 dB / 1.1 s | 10.0 dB / 1.11 s | 8928 | 5.2 |

Per row (C80 dB / T30 s; selected rows, all 20 in [M10.md](../milestones/M10.md#e5--amphitheatre-empty--audience--velarium-spec-55-200-000-rays-source-s1-at-the-arena-centre-16-m)):

| row | empty | audience | velarium | audience + velarium |
|---|---|---|---|---|
| 1 | 1.1 / n/a | 10.4 / 1.01 | 0.9 / 4.12 | 10.0 / 1.00 |
| 5 | −0.1 / n/a | 10.0 / 1.17 | −0.2 / 3.84 | 9.7 / 1.06 |
| 10 | 0.2 / n/a | 10.2 / 1.05 | 0.1 / 4.43 | 10.0 / 1.04 |
| 15 | −0.4 / n/a | 10.5 / 1.20 | −0.0 / 3.99 | 10.5 / 1.07 |
| 20 | 0.6 / n/a | 10.6 / 0.95 | 0.6 / 5.90 | 10.4 / 1.25 |

Reading:

- **The audience is the dominant change**: C80 rises from about 0 dB to about 10.5 dB on every row, T30 becomes about 1.0–1.2 s.
- **Empty stone** (α 0.02): C80 about 0 dB and **no measurable T30 within 3 s**. Energy keeps circulating between the bare vertical risers of the closed ellipse (20 concentric walls acting like a whispering gallery), while the treads send energy up and out. This is most likely a property of the idealised geometry (continuous risers, no seats, no openings, s = 0.2) — see Known issues.
- **The velarium alone changes little**: the trapped energy travels below the 15 m top, and the flat ring at 18 m only catches rays that leave the cavea upwards. Together with the audience it changes C80 by 0.0–0.5 dB.
- R4 (side sector, major axis) has a higher C80 than R1–R3 in the empty case (6.6 dB vs about −2 dB).

## Experiment E1 — shoebox T30 against Sabine / Eyring over α (SPEC §5.1)

Shoebox 10 × 7 × 3 m, 20 000 rays, mean of R1 / R2 compared with the formulas ([4](04-room-acoustic-metrics.md)). CSV: [`docs/results/E1_shoebox_alpha.csv`](../results/E1_shoebox_alpha.csv).

| α | Sabine, s | Eyring, s | s = 1 (Lambert): T30 vs Eyring | s = 0.1 (nearly specular): T30 vs Eyring |
|---|---|---|---|---|
| 0.05 | 2.794 | 2.724 | +0.3 % | +4.1 % |
| 0.10 | 1.397 | 1.326 | +2.7 % | +12.5 % |
| 0.20 | 0.699 | 0.626 | +5.0 % | +29.2 % |
| 0.30 | 0.466 | 0.392 | +8.0 % | +36.3 % |
| 0.50 | 0.279 | 0.202 | +12.8 % | +57.7 % |

With Lambert walls — the diffuse field that Sabine and Eyring assume — the ray tracer stays within +0.3 … +12.8 % of Eyring, and between Sabine and Eyring for α ≤ 0.3. With nearly specular walls the decay is slower than both formulas and the gap grows with α: grazing rays along the long walls meet the absorbing surfaces less often than the diffuse-field mean free path $4V/S$ assumes. This is the known limit of the formulas, not a solver error (the same observation as in M2).

## Not done yet (planned)

- **Soundwalk mode** (menu 1.5): a first-person walk where the live receiver is the camera and the response switches to the nearest grid cell without re-tracing — a desktop analogue of the S5 soundwalk; optional ambient sources as in the 2016 tool. Today 1.5 opens the level like 1.1.
- **Industry levels on the amphitheatre**: they still carry the M9 arena-floor stand-in; the content script has to be rerun with the `Stone` material added (and Steam Audio's export of dynamic meshes checked). Until then the Compare table's plugin columns are not comparable with the bench ([11](11-industry-comparison.md)).
- Experiments §5.2 (convergence over N), §5.3 on the amphitheatre (the shoebox version is in [10](10-hybrid.md#results)), §5.4 (parallel speed-up on the amphitheatre), §5.6 (industry comparison on the new geometry; Audio Tracing needs an RTX GPU).
- Automation tests for the amphitheatre builder (row count, heights, closed shell, area, materials); a full menu pass.

## Known issues

- **Empty amphitheatre has no measurable T30** (above); to be checked with a longer histogram (T max) and a higher scattering of the stone.
- **Per-tile metrics are noisy** at the PIE ray count (20 000 rays saved in the level, 40 000 for the screenshots); E5 uses 200 000 rays in receiver batches. A PIE run with 856 grid receivers is kept at ≤ 40 000 rays because the ray tracer keeps one histogram per receiver and chunk (≈ 2.6 GB at 128 chunks).
- The HUD level description still says "Geometry Script build comes in M10".
- From the start camera the velarium ring hides part of the heatmap.

## Differences from the papers and simplifications

- **Idealised cavea**: rows are offset ellipses with equal growth of both semi-axes (not true parallel curves), bare steps — no seats, vomitoria, podium wall or outer façade.
- **Audience** = a material swap of the whole cavea shell (α 0.7), no extra volume of people; **velarium** = a flat absorbing ring at 18 m, not a sloped tensile roof.
- **Open air**: rays leaving over the top are lost (no sky or ground beyond the cavea).
- Energy model, one band, no diffraction (as everywhere in the bench). The fast pass ran only the ray tracer on the amphitheatre; image sources, beams and the hybrid were validated on the shoebox.
- S5's soundwalk runs on a phone on site with location and orientation; the planned bench version is a desktop walk through the model.
