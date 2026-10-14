# AcousticsLab

Geometric-acoustics simulation in Unreal Engine 5.8: the methods from the papers of Marjan Sikora (FESB, Split) — stochastic ray tracing, image sources, triangular beam tracing with beam division, beam tracing with refraction, thread-level parallelization, hybrid beam/ray tracing — implemented in C++ with visualization and auralization, and compared on the same geometry with Steam Audio (Valve) and Audio Tracing (Pinpoint, Fab).

A learning/demonstration project, not a scientific result. The full specification is in [docs/SPEC.md](docs/SPEC.md) (Russian), the milestone prompts in [docs/PROMPTS.md](docs/PROMPTS.md), the assistant rules in [CLAUDE.md](CLAUDE.md).

## Layout

- `Source/AcousticsCore` — pure C++ solvers (meters, seconds, double), no engine types, with automation tests.
- `Source/AcousticsLab` — game module: scene extraction, async simulation runs, visualization, audio, UMG UI.
- `Content/Maps` — `Level_MainMenu`, `Level_Shoebox`, `Level_Amphitheatre`, `Level_Refraction`, `Level_Amphitheatre_SteamAudio`, `Level_Amphitheatre_AudioTracing`.
- `docs/milestones` — what each milestone did and how to verify it in the editor.

## Status

M0 — project skeleton (main menu, levels, input, test pipeline). See [docs/milestones/M0.md](docs/milestones/M0.md).

## Build

Unreal Engine 5.8.3, Visual Studio 2022. Generate project files and build `AcousticsLabEditor Win64 Development` with `Engine\Build\BatchFiles\Build.bat`; exact commands are in `CLAUDE.md`.
