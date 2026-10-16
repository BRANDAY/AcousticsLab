"""M10 headless experiments (SPEC §5.1 and §5.5) through the python commandlet, editor closed:
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Experiments/run_m10_experiments.py" -unattended -nopause -nosplash -NullRHI
Writes Saved/Experiments/*.csv and Saved/Experiments/m10_tables.md; look for "M10 experiments done" in the log
(the commandlet exits with 1 because of the known LogGameFeatures error, see docs/ENV.md).
"""
import os
import unreal

RAYS_SHOEBOX = 20000
RAYS_AMPHITHEATRE = 200000


def main():
    out = []
    table, csv_path = unreal.AcousticsExperimentLibrary.run_shoebox_alpha_sweep(RAYS_SHOEBOX)
    out.append(f"## E1 shoebox alpha sweep ({RAYS_SHOEBOX} rays)\n\nCSV: {csv_path}\n\n{table}\n")
    unreal.log(table)

    if not unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Level_Amphitheatre"):
        raise RuntimeError("could not load Level_Amphitheatre")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    table, csv_path = unreal.AcousticsExperimentLibrary.run_amphitheatre_conditions(world, RAYS_AMPHITHEATRE)
    out.append(f"## E5 amphitheatre conditions\n\nCSV: {csv_path}\n\n{table}\n")
    unreal.log(table)

    path = os.path.join(unreal.Paths.project_saved_dir(), "Experiments", "m10_tables.md")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(out))
    unreal.log(f"M10 experiments done: {path}")


main()
