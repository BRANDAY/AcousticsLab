# Screenshots to take

[← Guide index](README.md)

The guide currently uses the milestone screenshots in [`docs/milestones/img`](../milestones/img) and a few crops in [`img/`](img) (main menu, benchmark panel, the M5 beam view). The rest is captured in **one pass after M10**, when all panels are final. New files go to `docs/guide/img/` with the names below; then replace the links in the pages named in the last column.

## How to capture

- Start the editor with the MCP server: `UnrealEditor.exe AcousticsLab.uproject /Game/Maps/<Level> -ModelContextProtocolStartServer -ModelContextProtocolPort=8000`.
- Place the camera before PIE by moving the `PlayerStart` (`ActorTools.set_actor_transform`, location in cm, rotation in degrees); the PIE pawn spawns there. Visualizer settings for a shot are set on the editor actor before `StartPIE` (`ObjectTools.set_properties` on `<manager>.BeamVisualizer` etc.); restore and save afterwards.
- Capture with `Tools/Editor/capture_window.ps1 -TitlePattern "Unreal Editor" -Out docs/guide/img/<name>.png`. Do **not** use the pattern `AcousticsLab`: it also matches the Claude desktop window.
- Turn on Windows "Do not disturb" first — `M5_beamtracer_beams_receiver.png` caught a desktop notification (the guide uses a cropped copy, `img/M5_beams_receiver_crop.png`).
- Prefer captures that show only the PIE viewport (crop the editor's menus, Outliner and Details panels), 1600–1920 px wide. The walls of the shoebox and of the pool are single-sided, so a camera placed **outside** the room sees everything inside — use that for overview shots.
- For the panel close-ups crop to the panel only.

## Missing (capture after M10)

Done: `main_menu.png` (PIE of `Level_MainMenu`), `benchmark_panel.png` (crop of `M7_benchmark_panel.png`), `hybrid_compare.png` (crop of `M8_compare_early.png`), `imagesources_outside.png` (#5) and `heatmap_c80.png` (#13), captured 16.10.2026 after M10; `ir_plot_pinned.png` (#3), `auralization_block.png` (#11), `pool_side.png` (#8), `lens_R3.png` (#10), `compare_table.png` (#16), captured after the M10 follow-up.

Tried and not used (16.10): #4 (after the ray run the plot spans 6 s and the pinned beam arrivals sit in the first 0.1 s), #6 / #7 (from inside the camera sits in an order-1 beam and the view is yellow; from outside the order 2–3 beams to R1 fill the box), #9 (from above the order-1 beams cover the pool). **#2 waits for a fix**: the current panel shows no settings rows (see the known issue in [01](01-overview-and-controls.md#the-simulation-panel)). Not attempted: #12 (needs a click on an editor Details button), #18 (GIF); `M10_steamaudio_cavea.png` stands in for `industry_steam.png`.

| # | File | Level / mode | Camera and settings | What it must show | Used in |
|---|---|---|---|---|---|
| 2 | `panel_ray_shoebox.png` | `Level_Shoebox`, ray tracer, after Run | crop to the right-hand panel | the current panel: Solver / Source combos, all setting fields, metrics of R1, Sabine / Eyring line, Visualization and Auralization blocks (the M2 shot predates most of them) | [01](01-overview-and-controls.md), [03](03-stochastic-ray-tracing.md) |
| 3 | `ir_plot_pinned.png` | `Level_Shoebox`, ray tracer | crop to the bottom-left plot; run, select R1, **Pin**; select R3 | three curves: live R3 (cyan) and pinned R1 (orange), −5/−35 dB lines, 50/80 ms markers, legend with T30 / C80 | [05](05-visualization.md), [04](04-room-acoustic-metrics.md) |
| 4 | `ir_plot_beam_vs_ray.png` | `Level_Shoebox` | beam run → R1 → **Pin**; ray run → R1; crop to the plot | discrete beam arrivals (pinned) against the ray-traced decay | [07](07-image-sources-and-beam-tracing.md), [10](10-hybrid.md) |
| 5 | `imagesources_outside.png` | `Level_Shoebox`, Solver ImageSource, order 3 | camera outside and above the room, ≈ 25 m up, looking down at ~60°; Ray paths off, Image sources on, "Only valid for receiver" off | the lattice of image sources around the room coloured by order (yellow / orange / red) and the paths into R1 | [07](07-image-sources-and-beam-tracing.md) |
| 6 | `beams_outside_order1.png` (tried 16.10: from outside the order-0/1 frustums still merge into one yellow box — try orders 2–3 with "Only with receiver" instead) | `Level_Shoebox`, Solver BeamTracer, level 2, order 3 | camera outside the room (e.g. (−14, −10, 6) m looking at the centre); Beams on, slider 1, `bDrawEscapedBeams` off | the frustums of orders 0–1 seen from outside instead of the uniform tint of `M5_beamtracer_beams_order1.png` | [07](07-image-sources-and-beam-tracing.md) |
| 7 | `beams_receiver_clean.png` | as `M5_beamtracer_beams_receiver.png` | same view, notifications off, crop the editor chrome | retake of the published shot | [07](07-image-sources-and-beam-tracing.md), [walkthrough](walkthrough.md) §4 |
| 8 | `pool_side.png` | `Level_Refraction`, source Pool, level 4, order 3, refraction on | camera outside the pool wall at water level, e.g. (−6.7, −8, 2) m looking +Y; Beams on, "Only with receiver" on, slider 3 | the beams to R1 bending at the water surface, seen from the side | [08](08-refraction.md), [walkthrough](walkthrough.md) §6 |
| 9 | `pool_tir.png` | same | camera above the pool looking down; slider 1, `bDrawEscapedBeams` off | the narrow cone of refracted (cyan) beams below the source and the totally reflected (yellow) beams elsewhere | [08](08-refraction.md) |
| 10 | `lens_R3.png` | `Level_Refraction`, source Lens, level 6, order 3 | camera beside the sphere looking at the axial plane, e.g. (7.5, 57, 2) m looking +Y; select **R3** in the panel | focal spot on the vertical heatmap and the R3 arrivals in the metric block and IR plot (the M6 shot had the pool's R1 selected, so its panel is empty) | [08](08-refraction.md), [walkthrough](walkthrough.md) §7 |
| 11 | `auralization_block.png` | `Level_Shoebox` after a ray and a beam run | crop to the Auralization block | buttons 1 / 2 / 3 with 2 and 3 enabled, the status lines (responses, IR length, WAV path) | [06](06-auralization.md) |
| 12 | `extracted_scene.png` | `Level_Shoebox` in the editor (not PIE) | `LevelSetup` → Log Extracted Scene; perspective view from outside | triangle edges and the cyan normals pointing into the room | [02](02-geometry-and-scene.md) |
| 13 | `heatmap_c80.png` | `Level_Shoebox`, ray tracer | camera above the room looking down; Ray paths off; Metric C80 | the C80 floor map with its legend (the existing shots show T30 and direct SPL) | [05](05-visualization.md) |

## After the next milestones

| # | File | Milestone | What it must show | Used in |
|---|---|---|---|---|
| 15 | ~~`hybrid_compare.png`~~ | M8 | done (crop of `M8_compare_early.png`) | [10](10-hybrid.md) |
| 16 | `industry_steam.png` (on the M10 cavea, viewport only), `industry_audiotracing.png` (**RTX machine only**), `compare_table.png` (comparison panel with filled SteamAud / AudioTr columns after `Acoustics.Industry.MeasureClick`) | M9 | the two plugin levels and the Compare table; until then [11](11-industry-comparison.md) uses `M9_steamaudio_pie.png` / `M9_audiotracing_pie.png` | [11](11-industry-comparison.md) |
| 17 | covered by `M10_heatmap_{empty,audience,velarium}.png` and `M10_soundwalk.png` in docs/milestones/img | M10 | the amphitheatre, C80 per row (empty / audience / velarium), the soundwalk HUD | [12](12-amphitheatre-and-soundwalk.md) |
| 18 | a short GIF of the walkthrough (menu → shoebox run → beams → lens) | M10 | for the README | README |

## Notes on the existing screenshots

- All milestone shots include the editor UI in Russian (menus, Outliner); acceptable, but crops of the viewport would read better for an English reader.
- `M2_shoebox_pie.png` shows the panel of M2 (title "Simulation: ray tracer"); the current build prints the solver as `RayTracer` and has more blocks — retake #2.
- `M6_pool_receiver.png` and `M6_lens_focus.png` show the Metric combo at "C80" while the heatmap legend says "Total level": the grids' metric was set in the Details panel and the combo has no "Total level" entry.
