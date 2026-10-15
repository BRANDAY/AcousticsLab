"""Headless M7 benchmark: the beam tracer on the real levels, editor closed, nothing else rendering.

Runs inside the editor's python commandlet (the map is loaded into the editor world and extracted exactly like PIE):

    UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<this file> <plan> [<plan> ...]"
        -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

Plans (see PLANS below): calibrate, shoebox, shoebox_split, refraction, all. Every plan writes
Saved/Benchmarks/<Level>_<timestamp>.csv and _summary.csv (UAcousticsBenchmarkLibrary) and appends the Markdown table to
Saved/Benchmarks/m7_tables.md. The commandlet exits with code 1 because of a pre-existing LogGameFeatures error; check the
log for "M7 benchmark done" instead of the exit code (docs/ENV.md).
"""
import os
import sys
import time

import unreal

PLANS = {
    # Thread scaling on a trivially small scene: 12 triangles, many beams.
    "shoebox": dict(map="/Game/Maps/Level_Shoebox", level=4, order=4, split=1, source=None, grids=True),
    # The same room with every triangle split 8 x 8 (768 triangles): the triangle search matters.
    "shoebox_split": dict(map="/Game/Maps/Level_Shoebox", level=2, order=2, split=8, source=None, grids=True),
    # Pool + acoustic lens (2124 triangles, refraction on), source Pool: one root beam sees the lens sphere and carries
    # most of the work (the critical path of a per-root decomposition).
    "refraction": dict(map="/Game/Maps/Level_Refraction", level=2, order=3, split=1, source="Pool", grids=True),
    # The same level, directional source Lens in glycerol: 110 root beams of level 6, all of them on the sphere (balanced).
    "lens": dict(map="/Game/Maps/Level_Refraction", level=6, order=3, split=1, source="Lens", grids=True),
}

CALIBRATE = [
    ("shoebox", dict(level=4, order=4)),
    ("shoebox_split", dict(level=2, order=3)),
    ("refraction", dict(level=3, order=3)),
]


def editor_world():
    subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    return subsystem.get_editor_world()


def run(name, plan, repetitions=3, threads=None, with_bvh=True, without_bvh=True, note=""):
    unreal.log("M7 benchmark: plan {} on {}".format(name, plan["map"]))
    if not unreal.EditorLoadingAndSavingUtils.load_map(plan["map"]):
        unreal.log_error("M7 benchmark: could not load {}".format(plan["map"]))
        return None
    options = unreal.BeamBenchmarkOptions()
    options.subdivision_level = plan["level"]
    options.max_beam_order = plan["order"]
    options.surface_divisions = plan["split"]
    options.repetitions = repetitions
    options.include_receiver_grids = plan["grids"]
    options.with_bvh = with_bvh
    options.without_bvh = without_bvh
    if threads:
        options.thread_counts = threads
    if plan["source"]:
        options.source_id = plan["source"]
    options.note = note or "headless python commandlet (-NullRHI), editor closed, plan {}".format(name)
    start = time.time()
    table, csv_path = unreal.AcousticsBenchmarkLibrary.run_beam_benchmark(editor_world(), options)
    unreal.log("M7 benchmark: plan {} took {:.1f} s, CSV {}".format(name, time.time() - start, csv_path))
    out = os.path.join(unreal.Paths.project_saved_dir(), "Benchmarks", "m7_tables.md")
    with open(out, "a", encoding="utf-8") as handle:
        handle.write("\n## {} (level {}, order {}, split {}, source {})\n\nCSV: {}\n\n{}\n".format(
            name, plan["level"], plan["order"], plan["split"], plan["source"], csv_path, table))
    return table


def main(args):
    if not args:
        args = ["calibrate"]
    for arg in args:
        if arg == "calibrate":
            for name, override in CALIBRATE:
                plan = dict(PLANS[name])
                plan.update(override)
                run("calibrate_" + name, plan, repetitions=1, threads=[1, 12], without_bvh=True, note="calibration")
        elif arg == "all":
            for name in ("shoebox", "shoebox_split", "lens", "refraction"):
                run(name, PLANS[name])
        elif arg in PLANS:
            run(arg, PLANS[arg])
        else:
            unreal.log_error("M7 benchmark: unknown plan " + arg)
    unreal.log("M7 benchmark done")


main(sys.argv[1:])
