"""Creates the AcousticsLab placeholder maps under /Game/Maps (milestone M0).

Run from the project root while no editor has this project open:

  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/create_placeholder_levels.py"
      -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

Maps that already exist are skipped, so the script is safe to re-run.
"""
import unreal

MAPS_FOLDER = "/Game/Maps"
TEMPLATE_MAP = "/Engine/Maps/Templates/Template_Default"

# (map name, EAcousticLevelKind value, display name shown in the HUD)
LEVELS = [
    ("Level_MainMenu", "MAIN_MENU", "Main menu"),
    ("Level_Shoebox", "SHOEBOX", "Shoebox 10 x 7 x 3 m validation room (geometry comes in M1)"),
    ("Level_Amphitheatre", "AMPHITHEATRE", "Elliptical amphitheatre (Geometry Script build comes in M10)"),
    ("Level_Refraction", "REFRACTION", "Pool and acoustic lens, beam tracing with refraction (M6)"),
    ("Level_Amphitheatre_SteamAudio", "INDUSTRY_STEAM_AUDIO", "Amphitheatre with Steam Audio (M9)"),
    ("Level_Amphitheatre_AudioTracing", "INDUSTRY_AUDIO_TRACING", "Amphitheatre with Audio Tracing (M9)"),
]

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def spawn(actor_class, location, rotation=None, label=None):
    if rotation is None:
        rotation = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)
    actor = actors.spawn_actor_from_class(actor_class, location, rotation)
    if actor is None:
        raise RuntimeError(f"could not spawn {actor_class}")
    if label:
        actor.set_actor_label(label)
    return actor


def create_level(map_name, kind_name, display_name):
    asset_path = f"{MAPS_FOLDER}/{map_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log(f"[AcousticsLab] {asset_path} already exists, skipping")
        return

    created = False
    if unreal.EditorAssetLibrary.does_asset_exist(TEMPLATE_MAP):
        created = level_editor.new_level_from_template(asset_path, TEMPLATE_MAP)
    if not created:
        created = level_editor.new_level(asset_path)
        if created:
            spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(roll=0.0, pitch=-45.0, yaw=30.0), "Sun")
            spawn(unreal.SkyLight, unreal.Vector(0, 0, 500), label="SkyLight")
    if not created:
        raise RuntimeError(f"could not create {asset_path}")

    kind = getattr(unreal.AcousticLevelKind, kind_name)
    setup = spawn(unreal.LevelSetupActor, unreal.Vector(0, 0, 0), label="LevelSetup")
    setup.set_editor_property("level_kind", kind)
    setup.set_editor_property("display_name", unreal.Text(display_name))

    if kind_name != "MAIN_MENU":
        spawn(unreal.PlayerStart, unreal.Vector(0, 0, 200), label="PlayerStart")
        sign = spawn(unreal.TextRenderActor, unreal.Vector(700, 0, 250), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), "PlaceholderSign")
        sign.text_render.set_text(unreal.Text(f"{map_name}\n{display_name}\n\nPlaceholder - Esc returns to the main menu"))
        sign.text_render.set_world_size(30.0)

    if not level_editor.save_current_level():
        raise RuntimeError(f"could not save {asset_path}")
    unreal.log(f"[AcousticsLab] created {asset_path}")


for name, kind_name, display_name in LEVELS:
    create_level(name, kind_name, display_name)

unreal.log("[AcousticsLab] placeholder maps done")
