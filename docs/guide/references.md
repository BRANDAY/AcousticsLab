# References

[← Guide index](README.md)

The papers whose methods the bench implements, as labelled in the specification (S1–S6), and what was taken from each.

| | Citation | Used for | Guide page |
|---|---|---|---|
| **S1** | M. Sikora, I. Mateljan, N. Bogunović, "Beam Division in Acoustic Simulation of Non-Homogenous Environments", *Automatika* 52(4), 339–352, 2011. DOI [10.1080/00051144.2011.11828433](https://doi.org/10.1080/00051144.2011.11828433). [PDF (Hrčak)](https://hrcak.srce.hr/file/116412) | triangular beams, beam division, data structures | [7](07-image-sources-and-beam-tracing.md) |
| **S2** | M. Sikora, I. Mateljan, N. Bogunović, "Beam Tracing with Refraction", *Archives of Acoustics* 37(3), 301–316, 2012. DOI [10.2478/v10168-012-0039-y](https://doi.org/10.2478/v10168-012-0039-y). [PDF](https://journals.pan.pl//Content/101603/PDF/07_paper.pdf) | the BTR algorithm (§3.2–3.3), intensity of reflected and refracted beams (§3.1.1, Eq. 1–6), refracted beam geometry (§3.1.2, Eq. 7–8), receiver lookup (§3.3.5), the acoustic-lens validation (§4.4) | [7](07-image-sources-and-beam-tracing.md), [8](08-refraction.md) |
| **S3** | M. Sikora, I. Mateljan, "A method for speeding up beam-tracing simulation using thread-level parallelization", *Engineering with Computers* 30(4), 679–688, 2014 (online 2013). DOI [10.1007/s00366-013-0316-z](https://doi.org/10.1007/s00366-013-0316-z) | independent branches of the beam computation on threads; speed-up measurement | [9](09-parallelization.md) |
| **S4** | M. Sikora, M. Russo, I. Mateljan, "Hybrid Beam/Ray Tracing Simulation Method", *Proc. Euronoise 2018*, Hersonissos, Crete, 1989–1994. [PDF](https://euronoise2018.eu/docs/papers/334_Euronoise2018.pdf), [CROSBI](https://www.bib.irb.hr/941403) | beams for the early part, rays for the rest | [10](10-hybrid.md) |
| **S5** | M. Sikora, M. Russo, J. Đerek, A. Jurčević, "Soundscape of an Archaeological Site Recreated with Audio Augmented Reality", *ACM Trans. Multimedia Comput. Commun. Appl.* 14(3), 1–22, 2018. DOI [10.1145/3230652](https://doi.org/10.1145/3230652); and M. Sikora, Grčić, M. Russo, "A tool for soundscape auralization of ancient archaeological sites", 7th Congress of the Alps Adria Acoustics Association, Ljubljana, 2016 ([CROSBI](https://www.bib.irb.hr/838151)) | auralization of a site, soundwalk | [6](06-auralization.md), [12](12-amphitheatre-and-soundwalk.md) |
| **S6** | J. Đerek, M. Sikora, M. Pauković, L. Kraljević, "Acoustic Analysis of Various Purposes Rooms Using Measurements and Simulations", *2021 International Conference on Software, Telecommunications and Computer Networks (SoftCOM)*, 1–6, 2021. DOI [10.23919/SoftCOM52868.2021.9559097](https://doi.org/10.23919/SoftCOM52868.2021.9559097) | the evaluation metrics T20, C50, C80 | [4](04-room-acoustic-metrics.md) |

Also cited:

- M. Sikora, I. Mateljan, N. Bogunović, "The effect of refraction as a non-linear transformation on beam tracing simulation of sound propagation", Proc. 1st EAA EUROREGIO Congress on Sound and Vibration, Ljubljana, 2010 (abstract in *Acta Acustica united with Acustica* 96, Suppl. 1) — cited by S2 §3.1.2 for the apex of a refracted beam; [8](08-refraction.md).
- M. R. Schroeder, "New method of measuring reverberation time", *J. Acoust. Soc. Am.* 37, 409–412, 1965 — the backward-integrated decay curve; [4](04-room-acoustic-metrics.md).
- H. Kuttruff, *Room Acoustics* — Sabine / Eyring and the free-path-variance correction used to read the shoebox table; [4](04-room-acoustic-metrics.md).

Everything in the bench was implemented from the descriptions in the papers (mainly S1–S2) and the specification; no code of the authors was used.

Notes on the sources:
- S1 and S2 describe the same beam-tracing pipeline ("BTR"); for the shared parts the guide cites S2's section and equation numbers.
- S3 is not open access; the parallelization page relies on its abstract.
