# 6. Auralization: impulse-response synthesis, WAV export, convolution, A/B

[← Guide index](README.md) · Milestones M4 (beam variant added in M5, hybrid in M8, plugin switch in M9) · Status: **done** for Dry / Ray / Beam / Hybrid and Steam Audio; Audio Tracing prepared (needs hardware ray tracing)

The bench lets you **hear** each solver: the energy response of the selected receiver is turned into a 48 kHz pressure impulse response, loaded into the engine's convolution reverb, and an anechoic clip is played through it. This is the listening side of the S4–S5 line of work (auralization of rooms and of archaeological sites); here it serves as an A/B check of the solvers.

Code: `Source/AcousticsCore/Public/Results/IRSynthesizer.h`, `Results/WavFile.h`; `Source/AcousticsLab/Public/Audio/AuralizationComponent.h`.

## From an energy histogram to a pressure response

The solvers produce energy per 1 ms bin, $E_k$, without phase (energy model, SPEC §2.1). `FIRSynthesizer` (SPEC §2.9) builds a signal $h[n]$ at $f_s$ = 48 kHz:

1. **Noise per bin.** For every bin $k$ a white-noise burst $w[n]$ (PCG32 stream seeded by `(Seed, k)`, uniform in [−1, 1)) is scaled so that the sum of squares over the bin equals the bin energy **exactly**, not only in expectation:

$$
h[n] = \sqrt{\frac{E_k}{\sum_{m \in k} w[m]^2}}\; w[n], \qquad n \in \text{bin } k
$$

2. **Deterministic arrivals as impulses.** The direct sound and every early reflection that a deterministic solver found (image sources, beams) are inserted as single samples of amplitude $\sqrt{E}$ at $n = \mathrm{round}(t f_s)$; their energy is removed from the noise of the bin they fall into, because the histogram already contains it.
3. **Length.** The response is cut at the first bin where the Schroeder curve falls below −60 dB (never beyond $T_{max}$).
4. **Normalization.** Peak normalized to −3 dBFS.

The test `AcousticsLab.Core.Audio.IRSynthesis` checks all of this on a synthetic exponential decay ($T_{60}$ = 1.2 s) plus a direct impulse: the response ends at the first bin where the Schroeder curve is below −60 dB (1.2 s ± 0.05 s), the peak is −3 dBFS ± 1e-6, the per-bin energy of the IR divided by the normalization gain² reproduces the histogram to 1e-6 (relative), a `MaxSeconds` cap of 0.5 s gives exactly 24 000 samples, the direct impulse sits at `round(t·fs)` with amplitude $\sqrt{E_{direct}}$·gain, the same seed gives bit-identical samples and another seed gives different samples with identical per-bin energies.

## WAV export

Every successful run writes one file per point receiver: `Saved/IR/<Level>_<Solver>_<ReceiverId>.wav` — mono, 48 kHz, 32-bit float (format tag 3), peak −3 dBFS. Open it in Audacity (File → Import → Audio): a direct impulse followed by a noise tail decaying by 60 dB. `AcousticsLab.Core.Audio.WavRoundTrip` writes and re-reads 16-bit and float files and compares header fields and samples.

Exported responses of the concrete shoebox (ray tracer, 20 000 rays):

| File | Length | Peak at | Sum of squares |
|---|---|---|---|
| `Level_Shoebox_RayTracer_R1.wav` | 4.521 s | 19.56 ms (6.7 m) | 147.8 |
| `Level_Shoebox_RayTracer_R2.wav` | 4.519 s | 14.60 ms | 85.1 |
| `Level_Shoebox_RayTracer_R3.wav` | 4.475 s | 2.92 ms (1 m) | 4.0 |
| `Level_Shoebox_RayTracer_R4.wav` | 4.527 s | 24.75 ms (8.5 m) | 179.6 |

The peak is the direct sound (distance / 343 m/s); the sum of squares is the tail energy relative to the peak — 37 times larger at R1 than at R3, which is what you hear.

## Convolution in the engine

- `UAudioImpulseResponse` is created at runtime with `NewObject`, filled with the samples (mono, 48 kHz) and assigned with `USubmixEffectConvolutionReverbPreset::SetImpulseResponse` (engine `Plugins/Runtime/Synthesis`). The SPEC §6 risk ("runtime creation may need private methods") did not materialize, so the WAV-import fallback is not needed.
- The preset sits on the submix `SM_Auralization` (wet 0 dB, dry −96 dB). The anechoic clip has `SM_Auralization` as its base submix and plays **2D** (not spatialized): the response already contains the direct sound and the 1/d² attenuation of the selected receiver, so the listener is the selected receiver, not the camera.
- Loudness is normalized per response, `NormalizationVolumeDb = −10 log10(Σ h²)`, so the convolved clip has about the loudness of the dry clip whatever the room. Listen for clarity and distance, not for level.
- **A/B is a mix switch**: Dry sets the submix dry level 1 / wet 0, the solver variants set 0 / 1 (`USoundSubmix::SetSubmixDryLevel / SetSubmixWetLevel`). The convolution keeps running in both states, so switching is instantaneous and click-free.
- **Receiver switching does not rerun the solver.** All point receivers' responses are synthesized after a run and cached; `<` / `>` swaps the cached response within the same frame. This is the "moving receiver" property of beam tracing (S2: the beam tree is built once per source and only queried per receiver) applied to all solvers.

## Variants and keys

| Key | Variant | Signal path | Status |
|---|---|---|---|
| 1 | Dry | the clip as recorded | done |
| 2 | Ray | response from the last ray-tracer run | done |
| 3 | Beam | response from the last beam-tracer run (early reflections as exact impulses) | done |
| 4 | Hybrid | response of the last hybrid run (beams early, rays late, [10](10-hybrid.md)); the same run also refreshes 2 = pure ray and 3 = pure beam ≤ K | done (M8) |
| 5 | Steam Audio | the clip rendered by the plugin in the industry levels (binaural, listener at the selected receiver) | done (M9) |
| 6 | Audio Tracing | the same with Audio Tracing | prepared; needs the plugin and a GPU with hardware ray tracing ([11](11-industry-comparison.md)) |

Keys 5 and 6 stop the bench's convolution clip and play the plugin source; 1–4 return to the convolution. Outside the industry levels 5 / 6 only log "only in the industry levels". Key 2, 3 or 4 before the corresponding solver has run logs "has no result yet" and keeps the current routing. The component keeps one synthesized set per solver, so after one ray run and one beam run, keys 2 and 3 switch between the two rooms instantly. Image-source results are not auralized (the panel says so); their early reflections reach the ear through the beam variant, which finds the same paths.

## Panel: the "Auralization (convolution reverb)" block

| Control | What it does |
|---|---|
| `1: Dry`, `2: Ray`, `3: Beam`, `4: Hybrid` | same as keys 1–4; the active variant is highlighted, variants without a response are greyed |
| `Play`, `Stop` | start / stop the clip (it re-triggers itself with a 0.5 s gap, `LoopGapSeconds`) |
| status lines | variant, receiver, clip state; which responses are cached; IR length, cut reason (−60 dB or T_max), peak, normalization; WAV path |

The receiver selector `<` / `>` at the top of the panel selects the receiver whose response is heard.

Component properties (Details panel of `SimulationManager → Auralization`): anechoic clip, submix and preset references, sample rate, seed, peak dBFS, −60 dB truncation level, extra gain, WAV export on/off and format, auto-play on result, loop gap, clip volume.

Console commands (PIE, `~` console): `Acoustics.Run`, `Acoustics.Auralization.Variant 1|2|3`, `Acoustics.Auralization.Receiver R3|<index>`, `Acoustics.Auralization.Play`, `Acoustics.Auralization.Stop`.

## How to listen (step by step)

Headphones recommended; the clip is mono.

1. Open `Level_Shoebox`, Play, press **Run**. The clip starts in the **Dry** variant.
2. Press **2** (Ray): the same signal through the ray-traced concrete room at **R1** (6.7 m from the source, T30 ≈ 4.7 s) — every burst is followed by a long dense tail.
3. Press `>` twice to reach **R3** (1 m in front of the source): the direct sound is about 45 times stronger relative to the tail (direct energy $1/(4d^2)$: 0.0625 vs 0.0014), so onsets are sharper and the sound is closer and clearer; C50 is several dB higher. **R4** (8.5 m, far corner) is the most distant and diffuse.
4. Type `0.3` into **Wall alpha override**, Enter, **Run**: the tail collapses to about 0.5 s and the R1/R3 difference becomes one of room size rather than distance. `0` restores the concrete room.
5. Switch the **Solver** combo to `BeamTracer`, **Run**, press **3**: only the direct sound and the early reflections up to the beam order — a "dry room" with discrete echoes, which shows what the late part (rays) contributes.

![PIE, Ray variant, receiver R3](../milestones/img/M4_pie_auralization.png)

*PIE in `Level_Shoebox`: variant Ray, receiver R3 (1 m from the source); the Auralization block shows the cached response (4.475 s, cut at −60 dB, normalization −6.0 dB) and its WAV path; the IR plot shows the R3 decay.*

## Differences from the SPEC

- The anechoic clip is a synthetic placeholder (`Tools/Audio/make_anechoic_clip.py`: tone bursts at 300 / 1000 / 3000 Hz, a click train, a noise burst and a short sweep, separated by silence so the tail is audible). A real anechoic speech recording replaces it later.
- Noise is scaled per bin to the exact bin energy (stricter than "noise × √E" of SPEC §2.9).
- Per-response loudness normalization instead of the engine default of −24 dB.
- Broadband only: the six-band version will need band-filtered noise.
- The listener is the selected receiver; the first-person "soundwalk" with the response following the camera is planned for M10 ([12-amphitheatre-and-soundwalk.md](12-amphitheatre-and-soundwalk.md)).
