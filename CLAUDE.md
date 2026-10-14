# CLAUDE.md — AcousticsLab (Unreal Engine 5 C++)

Project rules for the AI coding assistant. The full technical specification is in `docs/SPEC.md` (Russian text, English identifiers) — read it before any task. Milestone prompts are in `docs/PROMPTS.md`.

## What this project is
A demonstration/learning project: geometric-acoustics simulation methods from the papers of Marjan Sikora (FESB, Split) — stochastic ray tracing, image sources, triangular beam tracing with beam division, beam tracing with refraction, thread-level parallelization, hybrid beam/ray — implemented in UE5 and compared on the same geometry with Steam Audio (Valve) and Audio Tracing (Pinpoint, Fab). Main menu → sections "Methods of M. Sikora", "Industry", "Compare". See `docs/SPEC.md` §0.

## Environment
- Unreal Engine **5.8.3** (Windows, DX12, RT-capable GPU). Engine path: `C:\Program Files\Epic Games\UE_5.8` (adjust if different; store the real path in `docs/ENV.md` once known).
- C++ project, modules `AcousticsCore` (pure C++ solvers, no UObject) and `AcousticsLab` (game module). Do not put computation into Blueprints or into the game module.
- Plugins: GeometryScripting, ProceduralMeshComponent, Niagara (optional), EnhancedInput, AudioCapture, SteamAudio, AudioTracing (Fab). Enable them in `AcousticsLab.uproject`.

## Build / test commands (run them yourself; do not ask the user to compile)
```
# Generate project files (after adding modules/plugins)
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%CD%\AcousticsLab.uproject" -game -engine

# Compile editor target
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" AcousticsLabEditor Win64 Development -Project="%CD%\AcousticsLab.uproject" -WaitMutex -FromMsBuild

# Run automation tests (headless)
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\AcousticsLab.uproject" -ExecCmds="Automation RunTests AcousticsLab" -unattended -nopause -nosplash -NullRHI -testexit="Automation Test Queue Empty" -log -log=AutomationTests.log
```
A milestone is done only when the editor target compiles with zero errors and all `AcousticsLab.*` automation tests pass. Paste the tail of the build log in your report.

## Code conventions
- Follow Epic's coding standard: `F`/`U`/`A`/`S`/`I`/`E` prefixes, PascalCase, `b` prefix for bools, `TArray`/`TMap`, `check()`/`ensure()`.
- Solvers work in **meters and seconds** using `FVector3d`/`double`. Convert UE centimeters ↔ meters only at the boundary (`UAcousticSceneExtractorComponent`, visualizers). Never mix units inside `AcousticsCore`.
- `AcousticsCore` depends only on `Core` (+ `CoreUObject` if unavoidable). No `UWorld`, no actors, no Slate inside it.
- Multithreading: solvers are pure functions of (scene, settings) → result; per-thread histograms, deterministic reduction (sum in fixed order). Results must not depend on thread count.
- Every solver exposes `FSolverSettings` and returns `FSimulationResult` (histogram per band, metrics, path samples for visualization capped at `MaxPathsForVisualization`, timing).
- Materials: `NumBands` is a compile-time constant (`constexpr int32 NumBands = 1` for v1), stored as arrays from the start so the 6-band extension is a constant change.
- Logging category `LogAcoustics`. Use `SCOPE_CYCLE_COUNTER`/`TRACE_CPUPROFILER_EVENT_SCOPE` on solver entry points.
- UI: UMG widgets created in C++ (`UUserWidget` subclasses with `BindWidget`) with thin Blueprint layouts under `Content/UI/`. Keep logic in C++.
- No external third-party libraries in `AcousticsCore` (write the small math/geometry needed: ray–triangle Möller–Trumbore, Sutherland–Hodgman clipping, BVH).

## Testing
- Automation tests live in `Source/AcousticsCore/Private/Tests/`, use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` with names `AcousticsLab.Core.<Area>.<Case>`.
- Required tests (see SPEC §4): ray–triangle, BVH vs brute force, Schroeder/T30/C50/C80 on synthetic exponential decay, Sabine/Eyring shoebox validation (ray tracer, 20k rays, tolerance 20 %), image-source vs beam early reflections (±0.1 ms, ±1 % energy), thread-count invariance.

## Working style
- Work milestone by milestone (`docs/SPEC.md` §4). Before coding a milestone, write a short plan into `docs/milestones/Mx.md`; after finishing, append what was done, deviations from SPEC, and how to verify by hand in the editor.
- Prefer small, compilable steps. Compile after each class. Run tests before reporting.
- Do not invent engine APIs: when unsure about a UE 5.8 API, grep the engine source under `Engine/Source` (e.g. `USubmixEffectConvolutionReverbPreset`, `UAudioImpulseResponse`, `UDynamicMeshComponent`, `FQueuedThreadPool`, `UGeometryScriptLibrary_*`) and cite the header path in your notes.
- Known fallbacks are listed in SPEC §6; use them instead of blocking (e.g. runtime impulse-response creation → WAV import).
- Commit per milestone with message `M<x>: <summary>`. Never commit `Saved/`, `Intermediate/`, `DerivedDataCache/`, `Binaries/`.
- Report in English, short. If a choice is not covered by SPEC and is cheap to change later, decide and note it in the milestone file; if it is expensive to change (project settings, plugin conflicts), stop and ask.

## Physics reference (quick)
- c = 343 m/s (air), 1480 m/s (water). Energy reflection factor (1−α), scattering probability s.
- Receiver sphere radius 0.5 m; direct sound energy r²/(4d²); histogram bin 1 ms, T_max 3 s.
- Schroeder EDC; EDT (0→−10 dB ×6), T20 (−5→−25 ×3), T30 (−5→−35 ×2) by linear regression; C50/C80 = 10·log10(early/late); D50.
- Sabine T60 = 0.161·V/(S·ᾱ); Eyring T60 = 0.161·V/(−S·ln(1−ᾱ)). Shoebox 10×7×3 m: V=210 m³, S=242 m².
- Snell: sinθ2/sinθ1 = c2/c1; total internal reflection when sinθ2 > 1; R = ((Z2−Z1)/(Z2+Z1))², Z = ρ·c.
