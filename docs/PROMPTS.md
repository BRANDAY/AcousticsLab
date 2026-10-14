# PROMPTS.md — промпты для Claude Code / Cursor

Как пользоваться: создайте проект в UE 5.8.3 (Games → Blank → C++, без Starter Content, имя `AcousticsLab`), закройте редактор, скопируйте в корень проекта `CLAUDE.md` и папку `docs/` (с `SPEC.md`, `PROMPTS.md`), откройте терминал в корне проекта, запустите `claude` и вставьте **стартовый промпт**. Дальше — по одной вехе за сессию: промпт вехи → проверка в редакторе → коммит.

Промпты на английском (ассистенту так проще работать с движком); перевод смысла — курсивом под каждым.

---

## P0 — стартовый промпт (один раз)

```
You are working in the root of an Unreal Engine 5.8.3 C++ project named AcousticsLab (Windows, DX12).
Read CLAUDE.md and docs/SPEC.md completely before doing anything. SPEC.md is in Russian with English identifiers; CLAUDE.md is the rulebook.

Goal of the project (SPEC §0): a demo/learning project that implements geometric-acoustics methods from the papers of Marjan Sikora (stochastic ray tracing, image sources, triangular beam tracing with beam division, beam tracing with refraction, thread-level parallelization, hybrid beam/ray tracing) in UE5 with full visualization and auralization, plus two "industry" levels using the Steam Audio and Audio Tracing plugins on the same geometry, and a Compare panel. Everything is reachable from a main menu shown at startup.

Work plan: milestones M0…M10 from SPEC §4, one milestone per session, acceptance criteria as written there.

First task now: milestone M0.
1. Inspect the current project layout (uproject, Source/, Config/). Record the engine install path you find in docs/ENV.md.
2. Create the two modules AcousticsCore (runtime, pure C++) and AcousticsLab (game) with the folder structure from SPEC §3.2; wire Build.cs and Target.cs; enable plugins GeometryScripting, ProceduralMeshComponent, EnhancedInput, AudioCapture in the .uproject (SteamAudio and AudioTracing will be added at M9 — leave TODO comments).
3. Implement Level_MainMenu with a UMG main menu (C++ UUserWidget + BP layout) that matches the tree in SPEC §0, loads placeholder levels for every item, and returns to the menu on Esc. Set it as the Game Default Map. Add Enhanced Input (WASD, mouse look, keys 1–6 reserved for A/B, Esc).
4. Add the logging category, an empty automation test that passes, and docs/milestones/M0.md.
5. Compile with Build.bat and run the automation tests from the command line (commands in CLAUDE.md). Fix until green. Do not ask me to compile.
6. Finish with: what you did, what deviates from SPEC, exact steps for me to verify in the editor, and the tail of the build log.

Rules: compute nothing in Blueprints; meters/seconds inside AcousticsCore; small compilable steps; grep the engine source when unsure about an API; commit as "M0: project skeleton" when green.
```
*Смысл: прочитать CLAUDE.md и SPEC.md, сделать каркас проекта: два модуля, плагины, главное меню с загрузкой уровней-заглушек, ввод, пустой тест; собрать и прогнать тесты самому; отчитаться и закоммитить.*

---

## P1 — M1 Geometry

```
Milestone M1 from docs/SPEC.md §3.3 and §4.
Implement in AcousticsCore: FAcousticTriangle, FAcousticMaterialProps (arrays sized NumBands), FMedium, FAcousticScene, FBVH (median split, leaves of 4–8 triangles, IntersectRay / QueryFrustum / QueryAABB, Möller–Trumbore ray–triangle).
Implement in AcousticsLab: UAcousticMaterialAsset (DataAsset) with the presets from SPEC §3.3 as assets under Content/Acoustics/Materials; UAcousticSceneExtractorComponent that collects triangles from StaticMesh/DynamicMesh components tagged "Acoustic" (RenderData LOD0, world transform, cm→m) and assigns materials by component tag "AcMat:<name>" with Concrete as default; ALevelSetupActor.
Build Level_Shoebox: a 10×7×3 m room (6 slabs, inward-facing normals), material tag Concrete, source and receiver placeholders (AAcousticSource, AAcousticReceiver) at SPEC positions.
Tests: AcousticsLab.Core.Geometry.RayTriangle, AcousticsLab.Core.Geometry.BvhMatchesBruteForce (10k random rays), AcousticsLab.Core.Geometry.ShoeboxExtraction (12 triangles, area 242 m² ± 0.1).
Compile, run tests, write docs/milestones/M1.md, commit "M1: geometry, BVH, scene extraction".
```
*Смысл: геометрическое ядро, BVH, извлечение сцены из уровня, материалы, комната для валидации, тесты.*

---

## P2 — M2 Ray tracer + metrics + validation

```
Milestone M2 (SPEC §2.2, §2.3, §4).
AcousticsCore: FEnergyHistogram (bins of 1 ms, T_max 3 s, NumBands), FRayTracer per §2.2 (Fibonacci sphere emission, specular/diffuse by scattering s, absorption, optional air absorption, receiver sphere hit with energy deposit at entry time, analytic direct sound with occlusion check, termination rules), FRoomMetrics per §2.3 (Schroeder EDC, EDT/T20/T30 by linear regression over the dB range, C50/C80/D50 relative to direct-sound arrival), Sabine/Eyring helpers.
AcousticsLab: FSolverSettings (UStruct), AAcousticSimulationManager that extracts the scene, runs FRayTracer asynchronously (UE::Tasks), reports progress, and publishes FSimulationResult; UAcousticSimulationSubsystem storing results per level/solver; USimulationHUDWidget with settings, Run/Stop, metrics for the selected receiver, timing, and the analytic Sabine/Eyring values for the shoebox.
Tests: AcousticsLab.Core.Metrics.SyntheticDecay (exponential decay with known T60 → T30 within 2 %, C50/C80 against closed form), AcousticsLab.Core.Solvers.ShoeboxSabine (α = 0.1, 0.2, 0.3; 20k rays; T30 within 20 % of Eyring), determinism test with a fixed RNG seed.
Compile, tests, docs/milestones/M2.md with the validation table, commit "M2: ray tracer, metrics, shoebox validation".
```
*Смысл: стохастический трассировщик лучей, гистограмма, метрики T30/C50/C80, асинхронный запуск из уровня, HUD, валидация по Сабину/Эйрингу.*

---

## P3 — M3 Visualization

```
Milestone M3 (SPEC §3.6).
Implement URayPathVisualizerComponent (line batch / DrawDebugLine; ≤ 2000 paths; color by reflection order; "only paths reaching the receiver" toggle), AReceiverGrid + UHeatmapComponent (instanced 1×1 m tiles over a configurable grid, per-instance custom data → viridis gradient material; metric selector: direct SPL, C80, T30, EDT), UIRPlotWidget (UMG OnPaint: energy in dB vs time, Schroeder curve, markers at −5/−35 dB and 50/80 ms, up to 3 overlaid curves).
Hook them to AAcousticSimulationManager results. Add FSimulationResult::PathSamples capped at MaxPathsForVisualization.
Verify in Level_Shoebox and write docs/milestones/M3.md with screenshots. Commit "M3: visualization".
```
*Смысл: визуализация лучей, тепловая карта по сетке приёмников, график импульсного отклика.*

---

## P4 — M4 Auralization

```
Milestone M4 (SPEC §2.9, §3.7).
AcousticsCore: FIRSynthesizer — energy histogram → 48 kHz mono impulse response (noise shaped by sqrt(E) per bin, deterministic contributions inserted as impulses, peak-normalized to −3 dBFS, truncated at −60 dB), WAV writer to Saved/IR/.
AcousticsLab: UAuralizationComponent with an anechoic clip (placeholder: any mono 48 kHz WAV in Content/Audio/Anechoic; I will replace it), submix SM_Auralization with a Convolution Reverb effect; create UAudioImpulseResponse at runtime and assign it via USubmixEffectConvolutionReverbPreset::SetImpulseResponse — grep the engine headers first; if runtime creation is not viable, implement the fallback from SPEC §6 (import WAV as asset and pick from a list). A/B keys: 1 = Dry, 2 = Ray. Receiver switching R1–R4 updates the IR.
Verify audibly in Level_Shoebox; export WAVs; docs/milestones/M4.md; commit "M4: IR synthesis and convolution auralization".
```
*Смысл: синтез импульсного отклика, экспорт WAV, свёрточная реверберация в UE, A/B.*

---

## P5 — M5 Image sources + beam tracing

```
Milestone M5 (SPEC §2.4, §2.5, §4). Read SPEC §2.5 twice; this is the core of the project.
AcousticsCore: FImageSourceSolver (order ≤ 3, validity by back-tracing with polygon containment and occlusion), FBeamTracer (icosahedron subdivision emission; frustum query against the BVH; beam division by Sutherland–Hodgman clipping of the beam cross-section against the hit triangle in the triangle's plane, fan triangulation of clipped polygons; reflection by mirroring the apex; beam tree nodes as in §2.5.5; receiver test by half-spaces; energies and delays as in §2.5.6; the tree is built once per source and queried per receiver).
AcousticsLab: UImageSourceVisualizerComponent (spheres per order + polyline to the receiver), UBeamVisualizerComponent (translucent frustum meshes per order via UDynamicMeshComponent, order slider, "only beams containing the receiver" toggle), solver selector in the HUD, A/B key 3 = Beam.
Tests: AcousticsLab.Core.Solvers.BeamVsImageSource (shoebox, orders ≤ 2: same delays ±0.1 ms, same energies ±1 %), AcousticsLab.Core.Geometry.ClipPolygon (clipping cases incl. fully inside/outside/partial), receiver-inside-beam cases.
docs/milestones/M5.md; commit "M5: image sources and beam tracing".
```
*Смысл: мнимые источники и трассировка пучков с делением — ядро метода Сикоры; визуализация пучков; тест совпадения ранних отражений.*

---

## P6 — M6 Refraction

```
Milestone M6 (SPEC §2.6). Add FMedium list to FAcousticScene; interface triangles with media on both sides; Snell refraction of the three guide rays, total internal reflection, transmission factor from impedances; new apex by least-squares intersection of the refracted guide rays (document this approximation in the HUD and README).
Build Level_Refraction with Geometry Script: (a) pool 20×10×3 m with source above water and a receiver grid below; (b) acoustic lens scene (spherical interface) as in the 2012 paper. Visualize beams before/after the interface in different colors.
Acceptance: refraction visible, TIR at grazing angles, focusing spot on the lens receiver grid. docs/milestones/M6.md; commit "M6: beam tracing with refraction".
```
*Смысл: преломление пучков на границе сред, бассейн и линза, честно подписанное приближение.*

---

## P7 — M7 Parallelization benchmark

```
Milestone M7 (SPEC §2.7). Implement FParallelBeamTracer: root beams distributed over a dedicated FQueuedThreadPool with an explicit thread count, per-thread histograms, deterministic reduction; bUseBVH toggle (brute-force triangle search when off). UBenchmarkWidget: run matrix {1,2,4,8,…,NumCores} × {BVH on/off} × 3 repetitions on the current level, table + speedup chart (OnPaint), CSV export to Saved/Benchmarks/.
Tests: AcousticsLab.Core.Solvers.ThreadCountInvariance (identical histograms for 1 vs N threads).
docs/milestones/M7.md with the benchmark table from my machine (ask me to run it once and paste the CSV if you cannot run the editor interactively). Commit "M7: thread-level parallelization benchmark".
```
*Смысл: пул потоков, бенчмарк ускорения, BVH вкл/выкл, CSV и график.*

---

## P8 — M8 Hybrid

```
Milestone M8 (SPEC §2.8). Implement FHybridTracer: beams up to order K (default 2) or t_mix (80 ms), rays for the rest with specular-only paths of order ≤ K excluded. UCompareWidget: three overlaid IR curves (ray / beam / hybrid) and a metrics table; A/B key 4 = Hybrid.
Test: AcousticsLab.Core.Solvers.HybridConsistency (early part equals pure beam; T30 within 5 % of pure ray). docs/milestones/M8.md; commit "M8: hybrid beam/ray tracer".
```
*Смысл: гибрид пучки+лучи и панель сравнения трёх кривых.*

---

## P9 — M9 Industry levels

```
Milestone M9 (SPEC §3.8). Add the Steam Audio plugin (Valve; from the official release or built from source if the release does not support UE 5.8 — record which) and the Audio Tracing plugin from Fab (already in my Fab library; I will download it into Plugins/ if needed — tell me exactly what to do). First check the Fab listing's supported engine versions: it lists UE 5.6 and 5.7.2+; if 5.8 is not supported yet, stop and tell me instead of hacking the plugin. Create Level_Amphitheatre_SteamAudio and Level_Amphitheatre_AudioTracing as copies of Level_Amphitheatre with identical source/receiver positions from DA_AmphitheatrePositions. Configure each plugin per its documentation and sample project (geometry/material components, probes/baking for Steam Audio; CVar reflection depth for Audio Tracing). A/B keys 5 = Steam Audio, 6 = Audio Tracing (these levels only).
Optional (do after the above works): UClickResponseRecorder — play a 1-sample click through the source, record the master submix output to WAV (USoundSubmix::StartRecordingOutput/StopRecordingOutput), derive an approximate IR and compute SPEC §2.3 metrics for the Compare table.
If the two plugins conflict in project settings, implement the two-config-profile fallback from SPEC §6 and document it. docs/milestones/M9.md; commit "M9: industry levels".
```
*Смысл: два уровня с готовыми плагинами, A/B, опционально запись отклика на щелчок для метрик.*

---

## P10 — M10 Amphitheatre + polish

```
Milestone M10 (SPEC §3.4, §5). Build Level_Amphitheatre with Geometry Script: elliptical arena ≈65×40 m, 20 stepped cavea rows, ≈15 m high, stone material; toggles "audience" (row absorption 0.7) and "velarium" (absorbing canopy). Receiver positions R1–R4 and the heatmap grid from DA_AmphitheatrePositions. Soundwalk mode (first-person walk with live receiver = camera, IR switched to the nearest grid cell; optional virtual ambient sources for the Audio AR item 1.5).
Run the experiments from SPEC §5 (1–5), put the tables into README.md with screenshots/GIFs, list all simplifications (SPEC §6). Full menu pass without crashes. Commit "M10: amphitheatre, experiments, README".
```
*Смысл: сцена амфитеатра, эксперименты из §5, README с результатами и честным списком упрощений.*

---

## Полезные короткие промпты

- `Grep the engine source for <ClassName> and show me the header path and the public API before using it.` — перед любым сомнительным API.
- `Compile and run the tests; if red, fix and repeat; report only the final tail of the log.` — когда ассистент начинает спрашивать вместо того, чтобы собирать.
- `Update docs/milestones/M<x>.md with what was done, deviations from SPEC, and manual verification steps.` — в конце вехи.
- `Explain in Russian, in 10 lines, how the beam division you implemented works and where it differs from the 2011 paper.` — чтобы вы сами понимали, что сделано, и могли это рассказать.
