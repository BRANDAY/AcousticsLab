"""M10 follow-up content (soundwalk, fixes of the fast pass) for Level_Amphitheatre, python commandlet, editor closed:
  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/create_m10_polish.py" -unattended -nopause -nosplash -NullRHI
Look for "M10 polish: done" in the log (the commandlet exits with 1 because of the known LogGameFeatures error).

- /Game/Audio/Ambient/A_Ambient_{Crowd,Birds,Wind} imported from Saved/Audio (Tools/Audio/make_ambient_clips.py), looping;
- three ASoundwalkAmbientSource actors around the cavea (S5: virtual sources, played in the soundwalk, key N);
- /Game/Materials/M_Velarium (translucent, two-sided): the velarium no longer hides the heatmap from the overview camera;
- the level description of the HUD (it still announced the M10 build);
- the simulation manager runs 100 000 rays with the grid on BeginPlay (the ray tracer now bounds its per-chunk memory by
  the receiver count, so the 856 grid cells no longer limit the PIE ray count).
Run before create_m9_content.py "assets steam tracing" (the industry levels are copies of Level_Amphitheatre).
"""
import math
import os

import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_library = unreal.EditorAssetLibrary

AMPHITHEATRE = "/Game/Maps/Level_Amphitheatre"
POSITIONS = "/Game/Acoustics/DA_AmphitheatrePositions"
AMBIENT_FOLDER = "/Game/Audio/Ambient"
MATERIAL_FOLDER = "/Game/Materials"
VELARIUM_MATERIAL = f"{MATERIAL_FOLDER}/M_Velarium"
DISPLAY_NAME = "Elliptical amphitheatre: 20 stepped rows, audience / velarium toggles; menu 1.5 = soundwalk"
NUM_RAYS = 100000

# label, clip, row (0 = above the top), azimuth deg, height above the tread m, volume, inner radius m, falloff m
AMBIENT = [
    ("AmbientCrowd", "A_Ambient_Crowd", 12, 250.0, 1.2, 0.6, 6.0, 45.0),
    ("AmbientBirds", "A_Ambient_Birds", 0, 10.0, 3.0, 0.5, 4.0, 60.0),
    ("AmbientWind", "A_Ambient_Wind", 0, 140.0, 1.5, 0.35, 10.0, 70.0),
]


def log(text):
    unreal.log(f"M10 polish: {text}")


def import_clip(name):
    wav = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Audio", f"{name}.wav")
    if not os.path.exists(wav):
        raise RuntimeError(f"{wav} missing: run python Tools/Audio/make_ambient_clips.py")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", wav)
    task.set_editor_property("destination_path", AMBIENT_FOLDER)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    asset_tools.import_asset_tasks([task])
    clip = asset_library.load_asset(f"{AMBIENT_FOLDER}/{name}")
    if clip is None:
        raise RuntimeError(f"could not import {name}")
    clip.set_editor_property("looping", True)
    asset_library.save_loaded_asset(clip, only_if_is_dirty=False)
    log(f"imported {AMBIENT_FOLDER}/{name}")
    return clip


def make_velarium_material():
    if asset_library.does_asset_exist(VELARIUM_MATERIAL):
        return asset_library.load_asset(VELARIUM_MATERIAL)
    asset_library.make_directory(MATERIAL_FOLDER)
    material = asset_tools.create_asset("M_Velarium", MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.85, 0.72, 0.5, 1.0))
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 200)
    opacity.set_editor_property("r", 0.22)
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(material)
    asset_library.save_loaded_asset(material, only_if_is_dirty=False)
    log(f"created {VELARIUM_MATERIAL}")
    return material


def by_class(name):
    return [a for a in actors.get_all_level_actors() if a.get_class().get_name() == name]


def main():
    clips = {name: import_clip(name) for _, name, *_ in AMBIENT}
    velarium_material = make_velarium_material()

    if not unreal.EditorLoadingAndSavingUtils.load_map(AMPHITHEATRE):
        raise RuntimeError("could not load Level_Amphitheatre")
    positions = unreal.load_asset(POSITIONS)
    a, b = positions.get_editor_property("arena_semi_axes_meters").x, positions.get_editor_property("arena_semi_axes_meters").y
    rows = positions.get_editor_property("num_rows")
    depth = positions.get_editor_property("row_depth_meters")
    rise = positions.get_editor_property("row_rise_meters")

    amphitheatre = by_class("AmphitheatreActor")[0]
    amphitheatre.set_editor_property("velarium_render_material", velarium_material)
    amphitheatre.rebuild()

    setup = by_class("LevelSetupActor")[0]
    setup.set_editor_property("display_name", DISPLAY_NAME)

    manager = by_class("AcousticSimulationManager")[0]
    settings = manager.get_editor_property("settings")
    settings.set_editor_property("num_rays", NUM_RAYS)
    manager.set_editor_property("settings", settings)

    existing = {a.get_actor_label(): a for a in by_class("SoundwalkAmbientSource")}
    for label, clip, row, azimuth, height, volume, inner, falloff in AMBIENT:
        if row > 0:
            offset, z = (row - 0.5) * depth, row * rise + height
        else:
            offset, z = rows * depth - 0.5, rows * rise + height
        t = math.radians(azimuth)
        location = unreal.Vector((a + offset) * math.cos(t) * 100.0, (b + offset) * math.sin(t) * 100.0, z * 100.0)
        source = existing.get(label)
        if source is None:
            source = actors.spawn_actor_from_class(unreal.SoundwalkAmbientSource, location, unreal.Rotator(0.0, 0.0, 0.0))
            source.set_actor_label(label)
            log(f"spawned {label}")
        source.set_actor_location(location, False, False)
        source.set_editor_property("sound", clips[clip])
        source.set_editor_property("label", label.replace("Ambient", "").lower())
        source.set_editor_property("volume", volume)
        source.set_editor_property("inner_radius_meters", inner)
        source.set_editor_property("falloff_meters", falloff)

    if not level_editor.save_current_level():
        raise RuntimeError("could not save Level_Amphitheatre")
    unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/Level_MainMenu")
    log("done")


main()
