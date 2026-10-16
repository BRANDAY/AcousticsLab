# M10 follow-up experiments (16.10.2026)

Headless runs (python commandlet, editor closed): `Tools/Experiments/run_m10b_experiments.py e5b e2 e3` and `Tools/Benchmark/run_m7_benchmark.py amphitheatre`. Discussion: `docs/milestones/M10.md`.

## E5b empty amphitheatre decay (1000000 rays)

CSV: docs/results/E5b_empty_decay.csv

Empty amphitheatre, 1000000 rays, R1..R4. Cells: C80 dB / T30 s / EDT s.

| case | R1 | R2 | R3 | R4 | trace, s |
|---|---|---|---|---|---|
| fast pass: order <= 200, T_max 3 s, s 0.2 | -0.8 / n/a / 4.33 | -1.9 / n/a / 5.29 | -1.9 / n/a / 4.16 | 3.7 / n/a / 4.28 | 2.1 |
| order <= 2000, T_max 3 s, s 0.2 | -0.8 / n/a / 4.33 | -1.9 / n/a / 5.29 | -1.9 / n/a / 4.16 | 3.7 / n/a / 4.28 | 2.1 |
| order <= 2000, T_max 8 s, s 0.2 | -0.8 / n/a / 4.36 | -1.9 / n/a / 5.29 | -1.9 / n/a / 4.16 | 3.7 / n/a / 4.33 | 2.2 |
| order <= 2000, T_max 8 s, s 0.5 | 2.9 / n/a / 2.15 | 2.1 / n/a / 2.32 | 1.2 / n/a / 1.83 | 4.1 / n/a / 2.52 | 2.0 |
| order <= 2000, T_max 8 s, s 0.8 | 5.8 / n/a / 1.31 | 4.5 / n/a / 1.59 | 1.9 / n/a / 1.78 | 5.8 / n/a / 1.41 | 2.1 |

## E2 convergence (5 seeds)

CSV: docs/results/E2_convergence.csv

Seeds per N: 5. Cells: mean +- standard deviation over the seeds; dev = mean minus the mean at the largest N.

**shoebox a0.1 s0.2** (12 triangles)

| N | trace, s | R1 T30, s (dev) | R1 C80, dB (dev) | R2 T30, s (dev) | R2 C80, dB (dev) |
|---|---|---|---|---|---|
| 1000 | 0.08 | 1.396 +- 0.018 (-0.004) | 1.05 +- 0.38 (+0.21) | 1.383 +- 0.035 (-0.016) | 0.86 +- 0.53 (-0.19) |
| 2000 | 0.08 | 1.398 +- 0.019 (-0.001) | 1.13 +- 0.12 (+0.28) | 1.398 +- 0.019 (-0.001) | 1.42 +- 0.41 (+0.37) |
| 5000 | 0.10 | 1.4 +- 0.021 (0.001) | 0.96 +- 0.18 (+0.12) | 1.4 +- 0.02 (0.002) | 1.16 +- 0.21 (+0.10) |
| 10000 | 0.14 | 1.401 +- 0.009 (0.002) | 0.94 +- 0.20 (+0.10) | 1.407 +- 0.015 (0.009) | 0.99 +- 0.12 (-0.07) |
| 20000 | 0.26 | 1.402 +- 0.003 (0.003) | 0.85 +- 0.14 (+0.00) | 1.4 +- 0.007 (0.001) | 1.06 +- 0.16 (+0.01) |
| 50000 | 0.68 | 1.396 +- 0.003 (-0.003) | 0.91 +- 0.04 (+0.07) | 1.398 +- 0.005 (-0.001) | 1.10 +- 0.06 (+0.04) |
| 100000 | 1.69 | 1.399 +- 0.003 (0) | 0.89 +- 0.06 (+0.05) | 1.401 +- 0.004 (0.002) | 1.10 +- 0.04 (+0.05) |
| 200000 | 2.88 | 1.399 +- 0.002 (0) | 0.84 +- 0.03 (+0.00) | 1.399 +- 0.001 (0) | 1.05 +- 0.03 (+0.00) |

**amphitheatre audience** (8160 triangles)

| N | trace, s | R1 T30, s (dev) | R1 C80, dB (dev) | R2 T30, s (dev) | R2 C80, dB (dev) | R3 T30, s (dev) | R3 C80, dB (dev) | R4 T30, s (dev) | R4 C80, dB (dev) |
|---|---|---|---|---|---|---|---|---|---|
| 1000 | 0.01 | n/a +- n/a (n/a) | 82.42 +- 39.32 (+72.03) | n/a +- n/a (n/a) | 88.25 +- 26.28 (+79.29) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+90.61) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+86.50) |
| 2000 | 0.01 | n/a +- n/a (n/a) | 100.00 +- 0.00 (+89.61) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+91.04) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+90.61) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+86.50) |
| 5000 | 0.02 | n/a +- n/a (n/a) | 65.28 +- 47.58 (+54.89) | n/a +- n/a (n/a) | 62.66 +- 51.16 (+53.70) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+90.61) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+86.50) |
| 10000 | 0.02 | n/a +- n/a (n/a) | 63.82 +- 49.54 (+53.43) | n/a +- n/a (n/a) | 23.85 +- 42.59 (+14.89) | n/a +- n/a (n/a) | 100.00 +- 0.00 (+90.61) | n/a +- n/a (n/a) | 45.24 +- 50.14 (+31.74) |
| 20000 | 0.04 | n/a +- n/a (n/a) | 11.36 +- 1.73 (+0.97) | n/a +- n/a (n/a) | 11.83 +- 5.38 (+2.87) | n/a +- n/a (n/a) | 63.03 +- 50.63 (+53.64) | n/a +- n/a (n/a) | 45.52 +- 49.84 (+32.02) |
| 50000 | 0.10 | 1.477 +- 0.295 (0.472) | 12.16 +- 2.03 (+1.77) | n/a +- n/a (n/a) | 8.38 +- 1.40 (-0.58) | n/a +- n/a (n/a) | 12.22 +- 4.69 (+2.83) | n/a +- n/a (n/a) | 9.75 +- 1.90 (-3.75) |
| 100000 | 0.22 | 0.989 +- 0.059 (-0.016) | 10.34 +- 0.98 (-0.05) | 0.878 +- 0 (n/a) | 10.34 +- 1.04 (+1.38) | n/a +- n/a (n/a) | 7.81 +- 2.09 (-1.58) | 0.58 +- 0 (-0.777) | 11.92 +- 3.99 (-1.58) |
| 200000 | 0.44 | 1.005 +- 0.054 (0) | 10.39 +- 0.34 (+0.00) | n/a +- n/a (n/a) | 8.96 +- 0.70 (+0.00) | n/a +- n/a (n/a) | 9.39 +- 1.08 (+0.00) | 1.357 +- 0.078 (0) | 13.50 +- 2.35 (+0.00) |

## E3 missed reflections, amphitheatre (K = 2, t_mix 200 ms)

CSV: docs/results/E3_missed_reflections.csv

Amphitheatre empty, 8160 triangles, beams of order <= 2, t_mix 200 ms after the emission, beam tree cap 2000000 nodes. Cells: early specular reflections found / missed by the pure ray run (of the beam count), early energy rays vs beams.

| rays | R1 found / missed of beams, energy | R2 found / missed of beams, energy | R3 found / missed of beams, energy | R4 found / missed of beams, energy | beams, s | rays, s |
|---|---|---|---|---|---|---|
| 100 | 0 / 13 of 13, 0 % | 0 / 1 of 1, 0 % | 0 / 1 of 1, 0 % | 0 / 3 of 3, 0 % | 6.58 | 0.03 |
| 1000 | 2 / 11 of 13, 367 % | 0 / 1 of 1, 0 % | 0 / 1 of 1, 0 % | 0 / 3 of 3, 0 % | 7.21 | 0.03 |
| 10000 | 4 / 9 of 13, 92 % | 1 / 0 of 1, 173 % | 0 / 1 of 1, 0 % | 1 / 2 of 3, 120 % | 6.81 | 0.08 |
| 100000 | 12 / 1 of 13, 70 % | 1 / 0 of 1, 139 % | 1 / 0 of 1, 126 % | 3 / 0 of 3, 60 % | 7.05 | 0.36 |

## E4 speed-up on the amphitheatre (beam tracer, M7 pool)

### amphitheatre2 (level 2, order 2, split 1, source None)

CSV: docs/results/E4_amphitheatre_L2_O2_bvh.csv

| threads | BVH | mean, s | min, s | build / queries, s | speedup T1/Tn (mean / min) | efficiency | BVH gain | identical |
|---|---|---|---|---|---|---|---|---|
| 1 | on | 38.548 | 27.802 | 38.405 / 0.144 | 1.00 / 1.00 | 100 % | — | yes |
| 2 | on | 19.243 | 16.250 | 19.162 / 0.081 | 2.00 / 1.71 | 100 % | — | yes |
| 4 | on | 13.645 | 11.958 | 13.584 / 0.060 | 2.83 / 2.33 | 71 % | — | yes |
| 6 | on | 10.297 | 10.098 | 10.264 / 0.034 | 3.74 / 2.75 | 62 % | — | yes |
| 8 | on | 9.546 | 9.246 | 9.501 / 0.046 | 4.04 / 3.01 | 50 % | — | yes |
| 12 | on | 9.758 | 8.345 | 9.702 / 0.056 | 3.95 / 3.33 | 33 % | — | yes |

### amphitheatre2_nobvh (level 2, order 2, split 1, source None)

CSV: docs/results/E4_amphitheatre_L2_O2_nobvh.csv

| threads | BVH | mean, s | min, s | build / queries, s | speedup T1/Tn (mean / min) | efficiency | BVH gain | identical |
|---|---|---|---|---|---|---|---|---|
| 1 | off | 109.122 | 109.122 | 109.009 / 0.114 | 1.00 / 1.00 | 100 % | — | yes |
| 12 | off | 25.367 | 25.367 | 25.339 / 0.028 | 4.30 / 4.30 | 36 % | — | yes |

### amphitheatre3 (level 3, order 1, split 1, source None)

CSV: docs/results/E4_amphitheatre_L3_O1_bvh.csv

| threads | BVH | mean, s | min, s | build / queries, s | speedup T1/Tn (mean / min) | efficiency | BVH gain | identical |
|---|---|---|---|---|---|---|---|---|
| 1 | on | 6.930 | 6.370 | 6.899 / 0.031 | 1.00 / 1.00 | 100 % | — | yes |
| 2 | on | 3.984 | 3.798 | 3.969 / 0.015 | 1.74 / 1.68 | 87 % | — | yes |
| 4 | on | 2.342 | 2.305 | 2.335 / 0.007 | 2.96 / 2.76 | 74 % | — | yes |
| 6 | on | 1.953 | 1.941 | 1.941 / 0.012 | 3.55 / 3.28 | 59 % | — | yes |
| 8 | on | 1.687 | 1.605 | 1.679 / 0.008 | 4.11 / 3.97 | 51 % | — | yes |
| 12 | on | 1.479 | 1.452 | 1.471 / 0.008 | 4.69 / 4.39 | 39 % | — | yes |

### amphitheatre3_nobvh (level 3, order 1, split 1, source None)

CSV: docs/results/E4_amphitheatre_L3_O1_nobvh.csv

| threads | BVH | mean, s | min, s | build / queries, s | speedup T1/Tn (mean / min) | efficiency | BVH gain | identical |
|---|---|---|---|---|---|---|---|---|
| 1 | off | 18.993 | 18.993 | 18.964 / 0.029 | 1.00 / 1.00 | 100 % | — | yes |
| 12 | off | 4.479 | 4.479 | 4.470 / 0.009 | 4.24 / 4.24 | 35 % | — | yes |

