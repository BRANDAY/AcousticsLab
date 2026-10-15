# 10. Hybrid beam/ray tracing

[← Guide index](README.md) · Milestone M8 · Status: **planned**

Menu: **1.4 Hybrid beam/ray tracing** (opens `Level_Shoebox` with `?mode=Hybrid`; until M8 the mode is only shown in the HUD line and the level opens with the regular simulation panel).

Source: S4 — M. Sikora, M. Russo, I. Mateljan, *Hybrid Beam/Ray Tracing Simulation Method*, Proc. Euronoise 2018, Hersonissos, Crete, pp. 1989–1994 ([PDF](https://euronoise2018.eu/docs/papers/334_Euronoise2018.pdf), [CROSBI record](https://www.bib.irb.hr/941403)).

## Why a hybrid

The two families of methods already in the bench have complementary weaknesses:

- **Beams** ([7. image sources and beam tracing](07-image-sources-and-beam-tracing.md)) give exact early reflections without misses, but their number explodes with the order (in the concrete shoebox: 320 root beams become 22 925 nodes at order 3), and they carry no diffuse energy.
- **Stochastic rays** ([3. ray tracing](03-stochastic-ray-tracing.md)) give the late, diffuse tail cheaply, but miss or smear early specular reflections unless the ray count is very high.

S4 starts from exactly this: the beam count grows exponentially with the order (the paper estimates the growth with an empirical beam-division factor of about 1.5 and cites the ~35th order needed for a 2 s reverberation in a 20 m hall), so beams cannot carry a whole response.

## The method proposed in S4

Triangular beams with adaptive division (the method of S1/S2) are traced only up to a certain reflection order; then **each beam is converted into a single ray** that carries the beam's energy and an *equivalent radius* for a spherical detector, and the rays continue. Diffuse reflection and diffraction (by the geometrical theory of diffraction) are to be handled in the ray part. The Euronoise paper presents the method and its complexity estimates; validation is announced as future work.

## What the bench plans (SPEC §2.8) — and how it differs

The SPEC plans a simpler combination of the two solvers that already exist:

1. **Early part — beams.** `FBeamTracer` up to reflection order $K$ (default 2) or the mixing time $t_{mix}$ (80 ms), whichever comes first.
2. **Late part — rays from the source.** `FRayTracer` as usual, but contributions already counted by the beams are excluded: a ray deposits energy only from order $K+1$, or after its first diffuse reflection.
3. **Sum.** Early (beams) + late (rays); the direct sound is counted once.

  $$
  E_{hybrid}(t) = E_{beam}^{(\le K)}(t) + E_{ray}^{(> K\ \text{or diffuse})}(t)
  $$

The difference from S4: the SPEC version launches independent stochastic rays from the source and removes the double-counted specular paths, while S4 **continues each beam as one ray** (its energy and an equivalent detector radius). S4's conversion keeps the beam's energy bookkeeping and needs no exclusion rule; the SPEC version is easier to build from the two existing solvers. Which of the two M8 implements — or both, for comparison — is a decision for M8 and will be recorded here.

## What it will show on screen

A comparison panel with three overlaid impulse-response curves — pure ray, pure beam (up to $K$), hybrid — in the IR plot (which already supports three curve slots, see [5. visualization](05-visualization.md)), and the metrics of each. A/B key **4** will play the hybrid response.

## Planned validation (SPEC §4, M8)

Test `AcousticsLab.Core.Solvers.HybridConsistency`: the early part of the hybrid equals the pure beam result, and its T30 is within 5 % of the pure ray result. SPEC §5 experiment 3 ("missed reflections": pure rays at small $N$ versus beams) will quantify the argument of S2/S4 in numbers.

## What exists already

`FSolverSettings::HybridK` (the order $K$, default 2) and the solver value `Hybrid` exist in the settings structure since M2, but nothing uses them yet: the panel's Solver combo lists only `RayTracer`, `ImageSource` and `BeamTracer`, and a manager set to `Hybrid` in the Details panel runs the ray tracer with the status note "Hybrid not available yet, using the ray tracer".
