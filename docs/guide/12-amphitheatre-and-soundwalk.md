# 12. Amphitheatre and soundwalk

[← Guide index](README.md) · Milestone M10 · Status: **in progress**

Menu: **1.1 Image sources & beam tracing - Amphitheatre** (`Level_Amphitheatre`) and **1.5 Audio AR soundwalk (optional)** (`Level_Amphitheatre`, `?mode=Soundwalk`). As of M9, `Level_Amphitheatre` holds a stand-in: a 100 × 80 m concrete arena floor with the source S1 and receivers R1–R4 at their final positions; M10 builds the cavea.

Sources: S4 (the hybrid method, motivated by long reverberation in large spaces) and S5 — M. Sikora, M. Russo, J. Đerek, A. Jurčević, *Soundscape of an Archaeological Site Recreated with Audio Augmented Reality*, ACM TOMM 14(3), 2018 (DOI [10.1145/3230652](https://doi.org/10.1145/3230652)): a smartphone audio-AR system that uses location and orientation to play the synthesized soundscape of a medieval Croatian site through headphones, evaluated by an in-situ soundwalk; and the earlier Sikora, Grčić, Russo, *A tool for soundscape auralization of ancient archaeological sites* (7th Congress of the Alps Adria Acoustics Association, Ljubljana, 2016, [record](https://www.bib.irb.hr/838151)): a game-engine tool that places virtual sound sources in a site and auralizes the result on location.

## What it will show

- **The "hero" scene.** An elliptical amphitheatre in the spirit of the one in Salona: arena about 65 × 40 m, a cavea of 20 stepped rows, about 15 m high, stone material, built procedurally with Geometry Script so the curved rows are triangle meshes, which beam tracing handles directly.
- **Empty / audience / velarium.** Toggles for seated audience (row absorption α = 0.7) and a velarium (absorbing canopy), with C80 and T30 per row (SPEC §5 experiment 5).
- **Fixed positions.** Source in the centre of the arena at 1.6 m; receivers R1 (1st row), R2 (10th row), R3 (20th row), R4 (side sector); a heatmap grid over all rows with a 2 m step. The same positions are used in the industry levels.
- **Soundwalk.** A first-person walk where the live receiver is the camera and the convolution response switches to the nearest grid cell as you move — a desktop analogue of the soundwalk evaluation of S5 (which ran on a phone, on site). Optional virtual ambient sources for menu item 1.5, as in the 2016 tool.
- **Experiments of SPEC §5** (convergence of ray tracing in N, missed reflections, speed-up on the amphitheatre at subdivision levels 2–3, empty / audience / velarium) with tables in the README.

## What already exists

- **Positions** (M9): the data asset `DA_AmphitheatrePositions` computes the source and R1–R4 from the arena and row parameters (65 × 40 m arena, 20 rows of 0.8 m depth and 0.75 m rise); the table is in [11](11-industry-comparison.md#the-scene-and-the-positions). The industry levels are generated from `Level_Amphitheatre` by `Tools/Editor/create_m9_content.py`, so they follow the cavea once it is built.
- Geometry extraction from tagged static and dynamic meshes ([2. geometry](02-geometry-and-scene.md)), so a Geometry Script mesh tagged `Acoustic` enters the solvers without new code.
- Receiver grids and heatmaps (C80, T30, EDT, direct SPL, total level), per-receiver responses and instant receiver switching for listening.
- The `Audience` material preset (α 0.70, s 0.60).
