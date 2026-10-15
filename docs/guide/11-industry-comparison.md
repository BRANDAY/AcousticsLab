# 11. Industry comparison: Steam Audio and Audio Tracing

[← Guide index](README.md) · Milestone M9 · Status: **planned**

Menu: **2. Industry → 2.1 Steam Audio (Valve)** (`Level_Amphitheatre_SteamAudio`) and **2.2 Audio Tracing (Pinpoint, Fab)** (`Level_Amphitheatre_AudioTracing`); **3. Compare → Metrics table & A/B listening** (opens `Level_Shoebox` with `?mode=Compare` until the compare panel exists). Today the two industry items open placeholder levels (a floor and a sign "Placeholder - Esc returns to the main menu").

## What it will demonstrate

The same amphitheatre geometry, the same source and receiver positions (a shared data asset, `DA_AmphitheatrePositions`), rendered by two production tools:

- **Steam Audio** (Valve, open source, Apache 2.0): geometry and material components on the meshes, a probe volume with baked or real-time reflections, Steam Audio source and listener. If its release does not support UE 5.8 it is built from source.
- **Audio Tracing** (Pinpoint, free on Fab): hardware ray-traced reflections from the world geometry, reflection depth through its console variable. The listing states UE 5.6 and 5.7.2+; support for 5.8.3 must be confirmed before installation.

A/B keys **5** and **6** will switch to the plugin renderings in their levels.

## Planned comparison

Neither plugin exposes its impulse response through a public API, so the comparison is indirect (SPEC §3.8): play a short click (one sample at 48 kHz, or a sine sweep), record the master submix (`USoundSubmix::StartRecordingOutput / StopRecordingOutput`), derive an approximate response and compute the same metrics as for the bench's own solvers ([4. metrics](04-room-acoustic-metrics.md)). The **Compare** panel will hold a table: own ray / beam / hybrid versus Steam Audio versus Audio Tracing at the same positions, plus subjective A/B (SPEC §5 experiment 6).

## Why it is in the bench

To show where the production tools are strong (real-time, hardware ray tracing, ease of setup) and where the methods of the papers add something: curved geometry with exact early reflections (beams), refraction between media, deterministic early response without misses.

## Known risks

- Plugin compatibility with UE 5.8.3 (see above). If the two plugins conflict in project settings (spatialization / reverb / occlusion plugins are project settings), two configuration profiles with a restart are the fallback (SPEC §6).
- Responses from the plugins are only indirect (recorded click), so the comparison is approximate.
