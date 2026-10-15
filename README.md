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
M1–M5 — geometry and BVH, stochastic ray tracer with room-acoustic metrics (Sabine/Eyring shoebox validation), visualization, IR synthesis and convolution auralization, image sources and triangular beam tracing with beam division. See `docs/milestones/M1.md` … `M5.md`.
M6 — beam tracing with refraction (Sikora 2012): media with speed of sound and density, interface triangles, Snell refraction of the three guide rays, total internal reflection, energy split by impedances, per-medium delays; `Level_Refraction` with a pool (air above water) and the acoustic lens (rubber sphere in glycerol). See [docs/milestones/M6.md](docs/milestones/M6.md).

Known simplification (M6): the apex of a refracted beam is the least-squares point of the three refracted guide lines (they do not meet in one point after refraction); the reflected beam keeps the exact mirrored apex. The ray tracer and the image-source solver treat interfaces as ordinary surfaces (no media).

## Build

Unreal Engine 5.8.3, Visual Studio 2022. Generate project files and build `AcousticsLabEditor Win64 Development` with `Engine\Build\BatchFiles\Build.bat`; exact commands are in `CLAUDE.md`.
