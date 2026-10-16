# 10. Hybrid beam/ray tracing

[← Guide index](README.md) · Milestone M8 · Status: **done** ([M8.md](../milestones/M8.md))

Menu: **1.4 Hybrid beam/ray tracing** (`Level_Shoebox?mode=Hybrid`) and **3. Compare → Metrics table & A/B listening** (`Level_Shoebox?mode=Compare`). Both open the shoebox with the **comparison panel** at the top right; only its title differs. In any scene level **H** toggles comparison panel ↔ simulation panel (**B** still toggles the benchmark panel).

Source: S4 — M. Sikora, M. Russo, I. Mateljan, *Hybrid Beam/Ray Tracing Simulation Method*, Proc. Euronoise 2018, Hersonissos, Crete, pp. 1989–1994 ([PDF](https://euronoise2018.eu/docs/papers/334_Euronoise2018.pdf), [CROSBI record](https://www.bib.irb.hr/941403)).

![comparison panel, menu 1.4: three curves, metrics table, exp. 3 line](img/hybrid_compare.png)

*Menu 1.4 after **Run hybrid** (K = 2, t_mix = 80 ms, 20 000 rays, beam subdivision 2), receiver R1, zoom 0–200 ms: blue = pure ray, orange = pure beam ≤ K, green = hybrid; below, the metrics table and the two summary lines (crop of [M8_compare_early.png](../milestones/img/M8_compare_early.png)).*

## Why a hybrid

The two families of methods already in the bench have complementary weaknesses:

- **Beams** ([7. image sources and beam tracing](07-image-sources-and-beam-tracing.md)) give exact early reflections without misses, but their number explodes with the order (in the concrete shoebox: 320 root beams become 22 925 nodes at order 3), and they carry no diffuse energy.
- **Stochastic rays** ([3. ray tracing](03-stochastic-ray-tracing.md)) give the late, diffuse tail cheaply, but miss or smear early specular reflections unless the ray count is very high.

S4 starts from exactly this: the beam count grows exponentially with the order (the paper estimates the growth with an empirical beam-division factor of about 1.5 and cites the ~35th order needed for a 2 s reverberation in a 20 m hall), so beams cannot carry a whole response.

## The method proposed in S4

Triangular beams with adaptive division (the method of S1/S2) are traced only up to a certain reflection order; then **each beam is converted into a single ray** that carries the beam's energy and an *equivalent radius* for a spherical detector, and the rays continue. Diffuse reflection and diffraction (by the geometrical theory of diffraction) are to be handled in the ray part. The Euronoise paper presents the method and its complexity estimates; validation is announced as future work.

## What the bench implements

Two independent solvers that already exist — `FBeamTracer` (M5–M7) and `FRayTracer` (M2) — are run on the same scene and combined in the histogram (`FHybridTracer`, `AcousticsCore/Public/Solvers/HybridTracer.h`). The split between them is a rule over **path classes**, applied identically by both parts.

**Early-specular paths.** A contribution is *early-specular* when its path

- is purely specular (no diffuse reflection so far),
- has reflection order $1 \le k \le K$ (default $K = 2$), and
- has an image-source delay to the receiver centre $t_{spec} = d'/c \le t_{mix}$ (default 80 ms, measured from the emission like every delay of the solvers).

**Who contributes what.**

- **Beams** contribute exactly the early-specular paths: every beam reflection of order $1..K$ with $t \le t_{mix}$.
- **Rays** contribute everything else. A ray deposit is moved to a separate *excluded* histogram only when the ray is still specular, its order is $1..K$ and the $t_{spec}$ of its path is $\le t_{mix}$. A specular ray of order $\le K$ arriving *after* $t_{mix}$ keeps depositing.
- **Direct sound** is analytic ($r^2/(4d^2)$, see [3](03-stochastic-ray-tracing.md)) and is taken once, from the ray part.

  $$
  E_{hybrid}(t) = E_{direct}(t) + E_{beam}^{\,early\text{-}spec}(t) + E_{ray}^{\,not\ early\text{-}spec}(t)
  $$

  $$
  E_{pure\ ray}(t) = E_{ray}^{\,not\ early\text{-}spec}(t) + E_{ray}^{\,excluded}(t)
  $$

Both ray terms come out of **one** ray run (two histograms per receiver), so the hybrid and the pure-ray curve share every random number and differ only in the early-specular block.

**The ray uses the beam's delay.** A ray reaches the receiver sphere up to $r/c \approx 1.5$ ms before its centre, so it does not classify itself by its own arrival time. In the unfolded (mirrored) space a specular ray is a straight line from the image source, and its image distance is

  $$
  d'^2 = (L + a)^2 + p^2
  $$

where $L$ is the path length before the current segment, $a$ the projection of (receiver centre − segment origin) on the ray direction and $p$ the perpendicular distance. Rays and beams therefore classify a path identically, also right at $t_{mix}$.

**Energy consistency.** The stochastic ray reflects specularly with probability $1 - s$, so its expected specular energy after $k$ reflections is $\prod (1-\alpha)(1-s)$. The beams of the hybrid carry the same factor per reflection (new beam-tracer option `bScatteringLoss`, off for the pure beam tracer of M5–M7, which stays bit-identical). Without it the early specular energy would be counted $1/(1-s)^k$ times too high (shoebox, $s$ = 0.1).

**Why per path class and not literally "order ≤ K or t ≤ t_mix, whichever comes first".** The specification (SPEC §2.8) says the beams cover orders up to $K$ or up to $t_{mix}$, whichever comes first, and the rays deposit only from order $K+1$ or after their first diffuse reflection. Taken literally, a purely specular path of order ≤ K that arrives after $t_{mix}$ would be dropped by the beams (time limit) *and* by the rays (order limit). With the per-class rule it stays with the rays; the test case K = 3, $t_{mix}$ = 30 ms shows 52 (R1) / 40 (R2) such beam reflections dropped by $t_{mix}$ and found among the late specular ray groups — none counted twice, none lost.

**Matching rays and beams.** For the statistics ("found / missed by the rays") a ray group and a beam reflection are matched by **image source** (order + position within 0.1 mm), not by the sequence of surfaces: reflections off two perpendicular walls commute (A→B and B→A share an image), and the receiver sphere is reached through both while the beam reports the one valid at its centre.

## What is drawn — the comparison panel

Built in C++ like the benchmark panel (`UCompareWidget`), top right.

| Block | Controls / content |
|---|---|
| Title + note | `1.4 Hybrid beam/ray tracing: beams early, rays late` (menu 1.4) or the Compare title (menu 3); a note stating the split rule |
| Settings | **K** (beams up to order K, default 2) · **t_mix, ms** (80) · **Rays** (20 000) · **Beam subdiv** (2 → 320 root beams) · **Run hybrid** · **Stop**; status and progress (`Done in 2.99 s`) |
| Receiver | `<`  `R1 (1 / 4)`  `>` — switches plot, table and the auralized receiver |
| Plot | three curves: **pure ray** (blue), **pure beam ≤ K** (orange), **hybrid** (green); thin = energy per 1 ms bin, thick = Schroeder curve; legend with T30 / C80; zoom **0–200 ms** / **Full decay** |
| Metrics table | EDT, T20, T30, C50, C80, D50, run time, rays, beams (root / tree nodes) for Pure ray, Beam ≤ K, Hybrid, and two industry columns **SteamAud** / **AudioTr** filled by M9 (see [11](11-industry-comparison.md)) |
| Split summary | which limit came first (order K or $t_{mix}$) and the last early beam arrival |
| Exp. 3 line | SPEC §5 experiment 3: how many of the early specular reflections the pure ray run **found / missed**, and the energy of the early block, beams vs rays |
| A/B | **1: Dry** · **2: Ray** · **3: Beam ≤ K** · **4: Hybrid**, with the auralization status (variant, receiver, IR length, WAV path) |

Table and summary are also written to the log after every run. At 0–200 ms the orange steps sit on the blue ray peaks up to 80 ms, and the green curve follows the orange one before $t_{mix}$ and the blue one after. In **Full decay** the green and blue Schroeder curves coincide.

The **simulation panel** (key H) also offers the hybrid: Solver combo **Hybrid**, fields **Hybrid: beams up to order K** and **Hybrid: t_mix, ms**, button **4: Hybrid**, and a timing line (early reflections kept / dropped by $t_{mix}$, found / missed by the rays, beam and ray times).

**Listening.** One hybrid run feeds three auralization variants: **2** = pure ray (`Saved/IR/<Level>_RayTracer_<R>.wav`), **3** = pure beam ≤ K (`<Level>_BeamTracerK2_<R>.wav`), **4** = hybrid (`<Level>_Hybrid_<R>.wav`). See [6. auralization](06-auralization.md).

## Results

Run of menu 1.4 in PIE (shoebox 10 × 7 × 3 m, concrete α = 0.03, s = 0.1; K = 2, $t_{mix}$ = 80 ms, 20 000 rays, subdivision 2), receiver R1:

| | EDT, s | T20, s | T30, s | C50, dB | C80, dB | D50 | time, s | rays | beams |
|---|---|---|---|---|---|---|---|---|---|
| Pure ray | 4.71 | 4.74 | 4.72 | −7.90 | −5.82 | 0.140 | 2.767 | 20000 | – |
| Beam ≤ 2 | n/a | n/a | n/a | 22.54 | 100.00 | 0.994 | 0.118 | – | 320 / 8428 |
| Hybrid | 4.71 | 4.74 | 4.72 | −7.86 | −5.80 | 0.141 | 2.994 | 20000 | 320 / 8428 |

The beam column has no decay tail (orders ≤ 2 only), so its T20 / T30 are n/a and C80 is the 100 dB "no late energy" cap. Exp. 3 line: 24 early specular reflections, all 24 found by 20 000 rays; early block beams 0.01115 vs rays 0.01071 (+4.1 %). T30 4.72 s against Eyring 4.59 s (+2.9 %) and Sabine 4.66 s (+1.4 %).

**Missed reflections (SPEC §5 experiment 3)**, from the test `HybridMissedReflections` (24 early specular reflections of order ≤ 2 per receiver):

| rays N | 250 | 1 000 | 4 000 | 16 000 | 64 000 | 256 000 |
|---|---|---|---|---|---|---|
| R1 missed | 20 | 14 | 3 | 0 | 0 | 0 |
| R2 missed | 17 | 14 | 2 | 0 | 0 | 0 |
| R1 + R2 hits of the early block | 11 | 21 | 107 | 444 | 1 745 | 7 018 |

The beams give all 24 at any N; here about 16 000 rays are needed before the rays find every early reflection, and the number grows with the order and the room size. In the panel, menu 3 with **Rays = 20**: 0 of 24 found, pure ray C50 −23.72 dB vs hybrid −14.19 dB (T30 5.13 s for both — 20 rays are far too few for a decay; the point is the early block).

![menu 3 with 20 rays: the rays miss all 24 early reflections](../milestones/img/M8_compare_20rays.png)

## Validation

| Test | What it checks |
|---|---|
| `AcousticsLab.Core.Solvers.HybridConsistency` | K = 2, $t_{mix}$ = 80 ms: T30 hybrid = pure ray to the millisecond (R1 1.499 / 1.499 s at s = 0.1; 1.359 / 1.359 s at s = 0.5); early specular energy beams vs rays within 1–2 %; every hybrid bin after $t_{mix}$ equals the pure-ray bin |
| `AcousticsLab.Core.Solvers.HybridNoDoubleCounting` | late + excluded histogram = an independent ray run, bin by bin; the image sets of the beam part and of the late ray groups are disjoint; the K = 3, $t_{mix}$ = 30 ms case above |
| `AcousticsLab.Core.Solvers.HybridDeterminism` | 8 000 rays: identical hybrid / pure ray / pure beam histograms for 1 thread, 4 threads, all cores and a repeated run |
| `AcousticsLab.Core.Solvers.HybridMissedReflections` | the exp. 3 table above |

(These automation tests use α = 0.1, hence the shorter T30 than in the concrete level.)

## How to run it

1. Main menu → **1.4 Hybrid beam/ray tracing** (or **3. Compare**). In PIE: open `Level_Shoebox`, Play, press **H**.
2. Keep K = 2, t_mix = 80, Rays = 20000, Beam subdiv = 2 and press **Run hybrid** (about 3 s on the development machine).
3. **0–200 ms** for the early part, **Full decay** for the Schroeder curves; read the table and the exp. 3 line.
4. Set **Rays = 20** (or 250 / 1000), run again: most of the 24 early reflections are missed by the rays, the hybrid still has all of them.
5. `<` `>` for R1–R4; keys **1–4** to listen (Dry / Ray / Beam ≤ K / Hybrid).

## Differences from S4 and from the specification

- **Two solvers combined in the histogram**, not beams turned into rays: S4 continues each beam as one ray with an equivalent detector radius; here the beam part and an independent stochastic ray run are split by path class inside one ray run. The beam receiver is a point (energy $r^2/(4d^2)$), as in M5.
- **No diffuse reflection inside the beams**: the scattered share $s$ leaves the beams (factor $1-s$) and is carried by the rays after their first diffuse reflection. No diffraction (as in S2 and the rest of the bench).
- **Split per path class** with $t_{mix}$ measured from the emission (not from the direct arrival as for C50 / C80); see above for why the literal SPEC rule would lose paths.
- **The hybrid's beams are omnidirectional and do not refract**, because the ray tracer models neither; a directional source or refraction (`Level_Refraction`) is ignored by the hybrid (the beam tracer itself keeps both).
- **Receiver sphere vs point**: rays enter a sphere of radius $r$, beams test its centre. A specular zone whose boundary crosses the sphere could leave a ray group without a beam counterpart (counted as `NumRayGroupsWithoutBeam`); in the shoebox the count is 0.
- **Validated on the shoebox only**; S4 targets curved surfaces such as ancient theatres — the amphitheatre is the subject of M10 ([12](12-amphitheatre-and-soundwalk.md)).
- Menu items 1.4 and 3 open the same level with the same panel (titles differ).
