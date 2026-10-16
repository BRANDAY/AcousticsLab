"""M10 follow-up experiments (SPEC §5.2, §5.3 on the amphitheatre, the empty-decay check) through the python commandlet,
editor closed:
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Experiments/run_m10b_experiments.py e2 e3 e5b" -unattended -nopause -nosplash -NullRHI
Each part appends its Markdown table to Saved/Experiments/m10b_tables.md and writes its CSV to Saved/Experiments/; look for
"M10b experiments done" in the log (the commandlet exits with 1 because of the known LogGameFeatures error, docs/ENV.md).
SPEC §5.4 (speed-up on the amphitheatre) is the "amphitheatre" plan of Tools/Benchmark/run_m7_benchmark.py.
"""
import os
import sys
import time

import unreal

RAY_COUNTS_E2 = [1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000]
SEEDS_E2 = 5
RAY_COUNTS_E3 = [100, 1000, 10000, 100000]
E3_K = 2
E3_SUBDIVISION = -1  # the manager setting
E3_T_MIX_MS = 200.0  # R2..R4 are 28-40 m away: with 80 ms after the emission nothing of theirs is early
E3_MAX_NODES = 2000000
RAYS_E5B = 1000000


def world():
    if not unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Level_Amphitheatre"):
        raise RuntimeError("could not load Level_Amphitheatre")
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def append(title, csv_path, table):
    path = os.path.join(unreal.Paths.project_saved_dir(), "Experiments", "m10b_tables.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(f"\n## {title}\n\nCSV: {csv_path}\n\n{table}\n")
    unreal.log(table)


def main(parts):
    w = world()
    for part in parts:
        start = time.time()
        if part == "e2":
            table, csv_path = unreal.AcousticsExperimentLibrary.run_ray_convergence(w, RAY_COUNTS_E2, SEEDS_E2)
            append(f"E2 convergence ({SEEDS_E2} seeds)", csv_path, table)
        elif part == "e3":
            table, csv_path = unreal.AcousticsExperimentLibrary.run_missed_reflections(w, RAY_COUNTS_E3, E3_K, E3_SUBDIVISION, E3_T_MIX_MS, E3_MAX_NODES)
            append(f"E3 missed reflections, amphitheatre (K = {E3_K}, t_mix {E3_T_MIX_MS:.0f} ms)", csv_path, table)
        elif part == "e3k1":
            table, csv_path = unreal.AcousticsExperimentLibrary.run_missed_reflections(w, [1000], 1, E3_SUBDIVISION, 80.0, 0)
            append("E3 calibration (K = 1, 1000 rays)", csv_path, table)
        elif part == "e5b":
            table, csv_path = unreal.AcousticsExperimentLibrary.run_empty_decay_check(w, RAYS_E5B)
            append(f"E5b empty amphitheatre decay ({RAYS_E5B} rays)", csv_path, table)
        else:
            unreal.log_warning(f"M10b experiments: unknown part {part}")
            continue
        unreal.log(f"M10b experiments: {part} took {time.time() - start:.1f} s")
    unreal.log("M10b experiments done")


main([arg for arg in sys.argv[1:] if not arg.startswith("-")] or ["e5b", "e2", "e3"])
