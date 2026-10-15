# 1. Overview, menu and controls

[← Guide index](README.md) · Milestones M0 (menu, input), M2 (panel) · Status: **done**

## How the bench is organised

```
Level (Unreal)                               AcousticsCore (pure C++, meters / seconds)
┌─────────────────────────────┐              ┌──────────────────────────────────────────┐
│ LevelSetup                  │  extract     │ FAcousticScene: triangles, materials,     │
│  └ SceneExtractor ──────────┼────────────► │ media, BVH                                │
│ Sources, Receivers, Grids   │              │                                           │
│ SimulationManager           │  Run (task)  │ FRayTracer / FImageSourceSolver /         │
│  ├ Settings, Solver ────────┼────────────► │ FBeamTracer (+ refraction)                │
│  ├ PathVisualizer           │              │        │                                  │
│  ├ ImageSourceVisualizer    │  result      │        ▼                                  │
│  ├ BeamVisualizer      ◄────┼───────────── │ FSimulationResult: histograms, metrics,   │
│  └ Auralization             │              │ early reflections, paths, beam tree       │
│ Simulation panel (HUD)      │              │ FIRSynthesizer → 48 kHz IR → WAV          │
└─────────────────────────────┘              └──────────────────────────────────────────┘
```

- Every scene level has a `LevelSetup` actor (with the scene extractor), acoustic sources (`AAcousticSource`), point receivers (`AAcousticReceiver`, a marker with a 0.5 m wireframe sphere), optional receiver grids (`AReceiverGrid`) and one `SimulationManager`.
- **Run** extracts the scene (cm → m), launches the selected solver in a background task, and on completion hands the result to the visualizers, the panel and the auralization component on the game thread.
- Solvers are pure functions of (scene, source, receivers, settings); results do not depend on the number of threads.

## Main menu

The project starts in `Level_MainMenu` ("ACOUSTICS LAB — Geometric acoustics after M. Sikora in Unreal Engine 5"). Each entry loads a level with a `?mode=` option:

![main menu](img/main_menu.png)

*The status line under the menu still carries the M0 text ("every item opens a placeholder level"); it is outdated.*


| Entry | Level | Mode | What is there today | Guide page |
|---|---|---|---|---|
| **1. Methods of M. Sikora** | | | | |
| 1.1 Image sources & beam tracing - Shoebox | `Level_Shoebox` | Beam | the full bench: ray tracer, image sources, beam tracer, metrics, heatmap, auralization | [3](03-stochastic-ray-tracing.md)–[7](07-image-sources-and-beam-tracing.md) |
| 1.1 Image sources & beam tracing - Amphitheatre | `Level_Amphitheatre` | Beam | placeholder (M10) | [12](12-amphitheatre-and-soundwalk.md) |
| 1.2 Beam tracing with refraction | `Level_Refraction` | Refraction | pool and acoustic lens | [8](08-refraction.md) |
| 1.3 Thread-level parallelization (benchmark) | `Level_Shoebox` | Benchmark | the benchmark panel (thread pool, BVH on/off, speed-up chart) | [9](09-parallelization.md) |
| 1.4 Hybrid beam/ray tracing | `Level_Shoebox` | Hybrid | regular panel; hybrid in M8 | [10](10-hybrid.md) |
| 1.5 Audio AR soundwalk (optional) | `Level_Amphitheatre` | Soundwalk | placeholder (M10) | [12](12-amphitheatre-and-soundwalk.md) |
| **2. Industry** | | | | |
| 2.1 Steam Audio (Valve) | `Level_Amphitheatre_SteamAudio` | SteamAudio | placeholder (M9) | [11](11-industry-comparison.md) |
| 2.2 Audio Tracing (Pinpoint, Fab) | `Level_Amphitheatre_AudioTracing` | AudioTracing | placeholder (M9) | [11](11-industry-comparison.md) |
| **3. Compare** | | | | |
| Metrics table & A/B listening | `Level_Shoebox` | Compare | regular panel; compare panel in M8–M9 | [11](11-industry-comparison.md) |
| Quit | | | closes the game | |

Apart from `Benchmark`, which opens the benchmark panel, the mode is currently **informational only**: it is shown in the top-left HUD line and logged, but no panel or solver reads it yet — the level opens with the solver saved in its `SimulationManager` (ray tracer in `Level_Shoebox`, beam tracer with refraction in `Level_Refraction`). Per-mode panels arrive with M8–M10.

Run it as **Standalone Game** (Play dropdown → Standalone Game) to use Esc → menu: in PIE the editor itself takes Esc to stop play. To work on one scene in PIE, open the level directly (`Content/Maps/Level_Shoebox`, `Level_Refraction`) and press Play.

## Controls

| Key | Action |
|---|---|
| **Tab** | toggle **cursor mode** (mouse drives the panel; the levels start in it) and **fly mode** (mouse look) |
| W / A / S / D, arrow keys | fly forward / left / back / right (forward follows the view, including pitch); works in both modes |
| E or Space / Q or C | up / down along the world Z axis |
| Mouse | look around (fly mode only) |
| **1** | A/B: Dry (the anechoic clip as recorded) |
| **2** | A/B: Ray — the clip convolved with the ray-tracer response of the selected receiver |
| **3** | A/B: Beam — the same with the beam-tracer response |
| 4 / 5 / 6 | Hybrid / Steam Audio / Audio Tracing — bound, no signal path yet (M8 / M9) |
| **B** | switch between the simulation panel and the benchmark panel (scene levels) |
| **Esc** | back to the main menu (Standalone) |

The camera is a flying first-person pawn (6 m/s, collides with the walls). The top-left HUD line shows `<level> - <description> | mode: <mode> | A/B variant: <n (name)>` and the key hints of the current mode.

## The simulation panel

Top right: settings, run control and results; bottom left: the impulse-response plot. Everything is built in C++ (UMG).

| Block | Controls | Details |
|---|---|---|
| Header | `Simulation: <solver>`, status (`Idle`, `Extracting scene...`, `Running N % (t s)`, `Finished: Done in t s`, `Cancelled`, `Failed`), progress bar | |
| Solver / source | **Solver** combo (`RayTracer`, `ImageSource`, `BeamTracer`), **Source** combo (enabled when the level has more than one source) | [3](03-stochastic-ray-tracing.md), [7](07-image-sources-and-beam-tracing.md), [8](08-refraction.md) |
| Settings | Rays · Max order · Seed · Image sources: order (<= 3) · Beams: subdivision (0=20, 2=320, 6=81920) · Beams: max order · Receiver r, m (0 = own) · Bin, ms · T max, s · Threads (0 = all) · Wall alpha override (0 = off) · Air m, 1/m + checkbox · Beams: refraction at interfaces | each field applies on Enter (or focus loss) and is clamped to its range |
| Run control | **Run**, **Stop** | Stop cancels a running solver |
| Receiver | `<`  `R1 (1 / 4)`  `>` | cycles the point receivers; drives the metrics, the "only …" filters, the IR plot and the auralization at once |
| Results | metrics of the selected receiver; solver statistics and timing; Sabine / Eyring reference (shoebox) | [4](04-room-acoustic-metrics.md) |
| Visualization | Ray paths · Only to receiver · Order <= · Image sources · Only valid for receiver · Beams · Only with receiver · Order <= slider · Heatmap · Metric · legend · counters | [5](05-visualization.md) |
| Auralization | `1: Dry` · `2: Ray` · `3: Beam` · `Play` · `Stop` · status | [6](06-auralization.md) |
| IR plot | energy (thin) and Schroeder decay (thick), markers, **Pin**, **Clear** | [5](05-visualization.md) |

The settings of the last run are remembered for the session (a game-instance subsystem) and applied to the manager of **every** level opened afterwards — one shared set, not one per level. Re-entering the same level restores your values; moving from `Level_Shoebox` to `Level_Refraction` (or back) carries the other level's values over, so check the fields (refraction checkbox, subdivision, T max, receiver radius) after switching levels. A fresh start of the game uses each level's saved values.

## Editor-side settings

Select `SimulationManager` in a level → Details:

| Property | Meaning |
|---|---|
| `Solver` | RayTracer, ImageSource, BeamTracer (Hybrid falls back to the ray tracer until M8) |
| `Settings` | the full `FSolverSettings` (all panel fields plus `MaxBeamNodes`, `MaxImageSources`, `MaxPathsForVisualization`, `bUseBVH`, `HybridK`) |
| `Wall Absorption Override` | 0 = use the materials; > 0 replaces α of every material (0 … 0.99) |
| `Source Id` | which `AAcousticSource` emits (None = the first) |
| `Run On Begin Play` | start a run automatically in PIE (used by the verification scripts) |
| `Include Receiver Grids` | grids contribute receivers to the run |
| components | `PathVisualizer`, `ImageSourceVisualizer`, `BeamVisualizer`, `Auralization` with their own display properties |

## Console commands (PIE)

`Acoustics.Run` · `Acoustics.Auralization.Variant 1|2|3` · `Acoustics.Auralization.Receiver R3|<index>` · `Acoustics.Auralization.Play` · `Acoustics.Auralization.Stop`. The profiler stat group is `STATGROUP_Acoustics`; log category `LogAcoustics`.

## Automated tests

30 automation tests, all named `AcousticsLab.Core.<Area>.<Case>`; they run headless or from Tools → Session Frontend → Automation (filter `AcousticsLab`). The full list with tolerances is in the [guide index](README.md#validation-at-a-glance).
