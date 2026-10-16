"""M10 content: Level_Amphitheatre with the elliptical amphitheatre (SPEC §3.4).
Run from the project root while no editor has this project open (python commandlet):
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/create_m10_content.py" -unattended -nopause -nosplash -NullRHI
Replaces the M9 arena-floor stand-in with AAmphitheatreActor (arena, 20 stepped rows, audience / velarium toggles),
adds the cavea receiver grid "Cavea" (2 m plan step over all rows, from DA_AmphitheatrePositions), sets the simulation
manager to a 20k-ray run with the grid on BeginPlay and puts the PlayerStart above the cavea looking at the arena.
Afterwards rerun create_m9_content.py with the phases "steam tracing" to copy the new geometry into the industry levels.
"""
import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

AMPHITHEATRE = "/Game/Maps/Level_Amphitheatre"
POSITIONS = "/Game/Acoustics/DA_AmphitheatrePositions"
STAND_IN = "ArenaFloor_StandIn"


def log(text):
    unreal.log(f"M10 content: {text}")


def by_class(name):
    return [a for a in actors.get_all_level_actors() if a.get_class().get_name() == name]


def spawn(class_path, label, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0)):
    actor = actors.spawn_actor_from_class(unreal.load_class(None, class_path), unreal.Vector(*location), unreal.Rotator(*rotation))
    if actor is None:
        raise RuntimeError(f"could not spawn {label}")
    actor.set_actor_label(label)
    log(f"spawned {label}")
    return actor


def main():
    if not unreal.EditorLoadingAndSavingUtils.load_map(AMPHITHEATRE):
        raise RuntimeError("could not load Level_Amphitheatre")
    positions = unreal.load_asset(POSITIONS)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() == STAND_IN:
            actors.destroy_actor(actor)
            log("removed the arena floor stand-in")

    found = by_class("AmphitheatreActor")
    amphitheatre = found[0] if found else spawn("/Script/AcousticsLab.AmphitheatreActor", "Amphitheatre")
    amphitheatre.set_editor_property("positions", positions)
    amphitheatre.set_audience(False)  # bAudience / bVelarium clash with the component names in python, use the setters
    amphitheatre.set_velarium(False)
    amphitheatre.rebuild()

    grids = [g for g in by_class("ReceiverGrid") if str(g.get_editor_property("grid_id")) == "Cavea"]
    grid = grids[0] if grids else spawn("/Script/AcousticsLab.ReceiverGrid", "GridCavea")
    grid.set_editor_property("grid_id", "Cavea")
    grid.set_editor_property("receiver_height_meters", positions.get_editor_property("seated_ear_height_meters"))
    grid.set_editor_property("receiver_radius_meters", 0.5)
    grid.set_editor_property("tile_z_offset_meters", 0.05)
    grid.get_editor_property("heatmap").set_editor_property("tile_fill_fraction", 0.55)
    count = amphitheatre.apply_grid(grid)
    log(f"cavea grid: {count} points")

    managers = by_class("AcousticSimulationManager")
    manager = managers[0] if managers else spawn("/Script/AcousticsLab.AcousticSimulationManager", "SimulationManager")
    manager.set_editor_property("solver", unreal.AcousticSolver.RAY_TRACER)
    manager.set_editor_property("include_receiver_grids", True)
    manager.set_editor_property("run_on_begin_play", True)
    settings = manager.get_editor_property("settings")
    settings.set_editor_property("num_rays", 20000)
    manager.set_editor_property("settings", settings)

    starts = by_class("PlayerStart")
    if starts:
        # Above the last rows on the minor axis, looking down at the arena centre.
        starts[0].set_actor_location(unreal.Vector(0.0, -5200.0, 2600.0), False, False)
        starts[0].set_actor_rotation(unreal.Rotator(0.0, -22.0, 90.0), False)

    if not level_editor.save_current_level():
        raise RuntimeError("could not save Level_Amphitheatre")
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Level_MainMenu")
    log("done")


main()
