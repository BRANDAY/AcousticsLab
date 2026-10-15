# 4. Room-acoustic metrics

[← Guide index](README.md) · Milestone M2 · Status: **done**

Every solver in the bench ends in the same place: an **energy impulse response** per receiver (a histogram of arriving energy over time) and a fixed set of room-acoustic parameters computed from it. The metrics are the ones the FESB group reports for its rooms (T20, C50, C80 in S6, Đerek, Sikora et al., SoftCOM 2021) plus EDT, T30, D50 and the centre time, so a run in the bench can be read the same way as a measurement.

Code: `Source/AcousticsCore/Public/Results/EnergyHistogram.h`, `Results/RoomMetrics.h/.cpp` (`FRoomMetrics::Compute`, namespace `RoomAcoustics`).

## The energy histogram

- Bin width $\Delta t = 1$ ms, length $T_{max}$ (3 s by default, 6 s in `Level_Shoebox`, see below), one energy value per bin and per frequency band. The number of bands is a compile-time constant, `Acoustics::NumBands = 1` (broadband) in this version; all material and histogram data are arrays from the start, so the six-octave-band extension is a constant change.
- Energies are relative: a source emits a total energy of 1 (the ray tracer gives every one of its $N$ rays $E_0 = 1/N$). Absolute levels are only used for the "Direct SPL" and "Total level" heatmaps, which assume a sound-power level $L_W = 100$ dB.
- The direct sound is added analytically at $t_0 = d/c$ with energy $E_{direct} = r^2 / (4 d^2)$ when the segment source–receiver is not occluded ($r$ = receiver radius, $d$ = distance). This is the fraction of the emitted energy that a sphere of cross-section $\pi r^2$ intercepts at distance $d$, consistent with how rays deposit energy (see [stochastic ray tracing](03-stochastic-ray-tracing.md)).

## Schroeder decay curve

The backward-integrated energy (Schroeder, 1965):

$$
EDC(t) = \sum_{\tau \ge t} E(\tau), \qquad L(t) = 10 \log_{10} \frac{EDC(t)}{EDC(0)} \ \text{dB}
$$

## Decay times: EDT, T20, T30

Each decay time is the slope of a **least-squares line** fitted to $L(t)$ over a level range, extrapolated to 60 dB:

| Metric | Fit range of $L(t)$ | Extrapolation |
|---|---|---|
| EDT | 0 → −10 dB | × 6 |
| T20 | −5 → −25 dB | × 3 |
| T30 | −5 → −35 dB | × 2 |

$$
T = \frac{-60\ \text{dB}}{\text{slope of the regression line in dB/s}}
$$

Two guards make the fit honest when a histogram simply stops (a ray budget or $T_{max}$ cuts it):

- The fit is valid only if the usable Schroeder curve reaches the end of the range **plus a 10 dB margin** (`ValidityMarginDb`); T30 therefore needs a decay of at least 45 dB, otherwise the panel prints `n/a`.
- The last 50 ms before the final non-empty bin are excluded from every fit (`TruncationGuardSeconds`), because the backward integral plunges at a truncation and would make the decay look steeper.

The panel line `Energy … range N dB` reports the usable decay range of the selected receiver.

## Clarity, definition, centre time

All three are measured from the arrival of the direct sound $t_0$ (not from $t = 0$):

$$
C_{50} = 10 \log_{10} \frac{\sum_{t_0 \le t < t_0 + 50\,\text{ms}} E(t)}{\sum_{t \ge t_0 + 50\,\text{ms}} E(t)}, \qquad
C_{80}\ \text{likewise with 80 ms}
$$

$$
D_{50} = \frac{\sum_{t_0 \le t < t_0 + 50\,\text{ms}} E(t)}{\sum_{t \ge t_0} E(t)}, \qquad
T_s = \frac{\sum (t - t_0)\, E(t)}{\sum E(t)}
$$

Clarity is clamped to ±100 dB (`ClarityLimitDb`). A run that has **no late energy at all** — the image-source and beam solvers, which stop at a few reflections — therefore shows `C80 100.0 dB` and `n/a` decay times. That is expected: those solvers model the early part of the response only; the late part is the job of the ray tracer and, later, of the hybrid method ([10-hybrid.md](10-hybrid.md)).

> Note for multi-media scenes ([refraction](08-refraction.md)): $t_0$ is the geometric direct path in air. Under water the transmitted sound can arrive *before* that time (water is faster), so for receivers below the pool surface C50/C80/Ts are not meaningful (the M6 screenshot shows Ts = −1.5 ms for R1). The refraction demo is read through the arrivals, the beams and the "Total level" heatmap instead.

## Analytic references: Sabine and Eyring

For a rectangular room of volume $V$, surface $S$ and area-weighted mean absorption $\bar\alpha$:

$$
T_{60}^{Sabine} = \frac{0.161\, V}{S\, \bar\alpha}, \qquad
T_{60}^{Eyring} = \frac{0.161\, V}{-S \ln(1 - \bar\alpha)}
$$

(both with an optional air term $4 m V$ in the denominator when air absorption is on). In `Level_Shoebox` the manager reads $V$ and $S$ from the `ShoeboxRoom` actor and computes $\bar\alpha$ from the extracted materials, so the panel prints the two references and the deviation of the simulated T30 next to the result:

```
Shoebox V 210 m3, S 242 m2, mean alpha 0.030
Sabine 4.657 s  Eyring 4.587 s
T30 4.722 s = +2.9 % vs Eyring, +1.4 % vs Sabine
```

## What is validated, and how

### Synthetic decays (test `AcousticsLab.Core.Metrics.SyntheticDecay`)

Histograms built from exact bin integrals of $e^{-kt}$ for $T_{60}$ = 0.4 / 1.2 / 2.0 s. EDT, T20 and T30 reproduce $T_{60}$ to better than 0.1 % (acceptance criterion 2 %); C50 and C80 match the closed form

$$
C_{50} = 10 \log_{10} \frac{1 - e^{-0.05 k}}{e^{-0.05 k} - e^{-3 k}}
$$

to $10^{-6}$ dB; D50 and Ts match their closed forms. A decay truncated at −42 dB yields no T30 (it needs −35 dB plus the 10 dB margin) but a valid T20 within 2 %.

### Shoebox against Sabine/Eyring (test `AcousticsLab.Core.Solvers.ShoeboxSabine`)

Room 10 × 7 × 3 m ($V$ = 210 m³, $S$ = 242 m²), source (−3, −1.5, 1.5) m, receivers R1 (3, 1.5, 1.5) m and R2 (0, 2.5, 1.2) m, $r$ = 0.5 m, 20 000 rays, seed 20261015, order ≤ 200, no air absorption. The acceptance criterion of SPEC §4 (T30 within 20 % of Eyring) is asserted for **diffuse walls** ($s$ = 1), the field the two formulas assume:

| α | Sabine, s | Eyring, s | T30 R1, s | dev. R1 | T30 R2, s | dev. R2 |
|---|---|---|---|---|---|---|
| 0.1 | 1.397 | 1.326 | 1.359 | +2.5 % | 1.358 | +2.4 % |
| 0.2 | 0.699 | 0.626 | 0.658 | +5.1 % | 0.653 | +4.3 % |
| 0.3 | 0.466 | 0.392 | 0.421 | +7.5 % | 0.426 | +8.8 % |

With almost specular walls ($s$ = 0.1, the Concrete preset) the same room gives T30 13–37 % above Eyring (reported, not asserted):

| α | Eyring, s | T30 R1, s | dev. R1 | T30 R2, s | dev. R2 |
|---|---|---|---|---|---|
| 0.1 | 1.326 | 1.499 | +13.0 % | 1.507 | +13.7 % |
| 0.2 | 0.626 | 0.796 | +27.1 % | 0.776 | +23.9 % |
| 0.3 | 0.392 | 0.533 | +36.1 % | 0.539 | +37.5 % |

Reading the tables. With Lambert walls the simulated T30 sits 2–9 % above Eyring and the excess grows with α, as Kuttruff's correction for the variance of the free path predicts: the decay is a mixture of rays with different reflection counts and is slightly slower than one exponential. With specular walls a rectangular room is not diffuse — rays grazing along the 10 m axis meet walls at a third of the mean rate and dominate the late decay — the classical non-diffuse shoebox result rather than a solver error.

### The concrete room in the editor

`Level_Shoebox` is concrete (α = 0.03, s = 0.1), so its reverberation is long: $T_{60} \approx 4.6$ s. Its `SimulationManager` therefore deviates from the SPEC defaults in two values, **MaxOrder 500** and **T_max 6 s**: with order ≤ 200 every ray stops after about 2 s at −26 dB and no T30 can be fitted. A PIE run (20 000 rays, mean order 454, 9.1 M segments, ~1–3 s on 10 workers) gives at R1: EDT 4.71 s, T20 4.74 s, T30 4.72 s (+2.9 % vs Eyring 4.59 s, +1.3 % vs Sabine 4.66 s), C50 −7.9 dB, C80 −5.8 dB, D50 0.14.

## How to reproduce in the editor

1. Open `Level_Shoebox`, Play (PIE or Standalone). The simulation panel appears top-right with the cursor visible.
2. Press **Run**. The metric block fills for R1; the yellow reference block shows V, S, mean α, Sabine, Eyring and the T30 deviation.
3. `<` / `>` switch to the other point receivers; the reference line follows.
4. Type `0.1` into **Wall alpha override (0 = off)**, press Enter, **Run** again: Sabine 1.397 s, Eyring 1.326 s, T30 ≈ +13 % (specular concrete). To see the diffuse agreement, set `ShoeboxRoom → Material Name = Audience` (s = 0.6) before playing.
5. Headless: the test command in [CLAUDE.md](../../CLAUDE.md) runs `SyntheticDecay` and `ShoeboxSabine` with the rest of the suite.

## Differences from the papers / SPEC

- The decay-time validity rule (10 dB margin, 50 ms truncation guard) is this project's choice, made after a truncated histogram produced a steep, meaningless T30.
- The 20 % Eyring criterion is checked for diffuse walls; the specular case is reported for comparison only.
- One broadband band; octave bands are prepared in the data layout but not computed.
