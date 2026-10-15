"""M4 content: auralization submix, convolution reverb preset, anechoic clip, receivers R3 / R4 (SPEC §3.7).

Run from the project root while no editor has this project open (like create_placeholder_levels.py):

  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/create_m4_content.py"
      -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

Creates
  /Game/Audio/SFX_AuralizationConvolution  SubmixEffectConvolutionReverbPreset (wet 0 dB, dry -96 dB, no hardware acceleration; the
                                           impulse response is assigned at runtime by UAuralizationComponent)
  /Game/Audio/SM_Auralization              SoundSubmix (parent = master) with the preset in its effect chain
  /Game/Audio/Anechoic/A_Anechoic_TestSignal  SoundWave imported from Saved/Audio/A_Anechoic_TestSignal.wav
                                           (Tools/Audio/make_anechoic_clip.py), Base Submix = SM_Auralization, not looping
and adds AcousticReceiver actors R3 (1 m in front of SourceS1) and R4 (far corner) to /Game/Maps/Level_Shoebox.
Existing assets / actors are reused and their settings re-applied, so the script is safe to re-run.
"""
import os

import unreal

AUDIO_FOLDER = "/Game/Audio"
ANECHOIC_FOLDER = "/Game/Audio/Anechoic"
PRESET_NAME = "SFX_AuralizationConvolution"
SUBMIX_NAME = "SM_Auralization"
CLIP_NAME = "A_Anechoic_TestSignal"
MAP_PATH = "/Game/Maps/Level_Shoebox"
RECEIVER_CLASS = "/Script/AcousticsLab.AcousticReceiver"

# (actor label, ReceiverId, location in cm). SourceS1 sits at (-300, -150, 150); R1 (300, 150, 150); R2 (0, 250, 120).
RECEIVERS = [
    ("ReceiverR3", "R3", (-200.0, -150.0, 150.0)),  # 1.0 m from the source: direct sound dominates
    ("ReceiverR4", "R4", (430.0, 280.0, 100.0)),  # far corner, 8.5 m from the source
]

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_library = unreal.EditorAssetLibrary


def ensure_asset(name, folder, asset_class):
    path = f"{folder}/{name}"
    if asset_library.does_asset_exist(path):
        asset = asset_library.load_asset(path)
        unreal.log(f"exists: {path}")
    else:
        asset_library.make_directory(folder)
        asset = asset_tools.create_asset(name, folder, asset_class, None)
        if asset is None:
            raise RuntimeError(f"could not create {path}")
        unreal.log(f"created: {path}")
    return asset


def ensure_preset():
    preset = ensure_asset(PRESET_NAME, AUDIO_FOLDER, unreal.SubmixEffectConvolutionReverbPreset)
    settings = preset.get_editor_property("settings")
    settings.set_editor_property("wet_volume_db", 0.0)
    settings.set_editor_property("dry_volume_db", -96.0)
    settings.set_editor_property("bypass", False)
    preset.set_settings(settings)
    preset.set_editor_property("enable_hardware_acceleration", False)
    return preset


def ensure_submix(preset):
    submix = ensure_asset(SUBMIX_NAME, AUDIO_FOLDER, unreal.SoundSubmix)
    chain = [entry for entry in submix.get_editor_property("submix_effect_chain") if entry is not None]
    if not any(entry.get_path_name() == preset.get_path_name() for entry in chain):
        chain.append(preset)
        submix.set_editor_property("submix_effect_chain", chain)
        unreal.log(f"effect chain of {SUBMIX_NAME}: {[entry.get_name() for entry in chain]}")
    return submix


def ensure_clip(submix):
    path = f"{ANECHOIC_FOLDER}/{CLIP_NAME}"
    if not asset_library.does_asset_exist(path):
        saved_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
        wav = os.path.join(saved_dir, "Audio", f"{CLIP_NAME}.wav")
        if not os.path.isfile(wav):
            raise RuntimeError(f"{wav} not found: run Tools/Audio/make_anechoic_clip.py first")
        asset_library.make_directory(ANECHOIC_FOLDER)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", wav)
        task.set_editor_property("destination_path", ANECHOIC_FOLDER)
        task.set_editor_property("destination_name", CLIP_NAME)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        asset_tools.import_asset_tasks([task])
        unreal.log(f"imported: {task.get_editor_property('imported_object_paths')}")
    clip = asset_library.load_asset(path)
    if clip is None:
        raise RuntimeError(f"could not load {path}")
    clip.set_editor_property("sound_submix_object", submix)
    clip.set_editor_property("looping", False)
    unreal.log(f"clip: {clip.get_path_name()}, duration {clip.get_editor_property('duration'):.3f} s, submix {submix.get_name()}")
    return clip


def ensure_receivers():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(MAP_PATH):
        raise RuntimeError(f"could not load {MAP_PATH}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    by_label = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}
    receiver_class = unreal.load_class(None, RECEIVER_CLASS)
    if receiver_class is None:
        raise RuntimeError(f"class {RECEIVER_CLASS} not found (is the AcousticsLab module compiled?)")
    for label, receiver_id, location in RECEIVERS:
        actor = by_label.get(label)
        if actor is None:
            actor = actors.spawn_actor_from_class(receiver_class, unreal.Vector(*location), unreal.Rotator(0.0, 0.0, 0.0))
            if actor is None:
                raise RuntimeError(f"could not spawn {label}")
            actor.set_actor_label(label)
            unreal.log(f"placed: {label}")
        else:
            unreal.log(f"exists: {label}")
        actor.set_editor_property("receiver_id", receiver_id)
        actor.set_actor_location(unreal.Vector(*location), False, False)
    if not level_editor.save_current_level():
        raise RuntimeError("could not save the level")
    unreal.log(f"saved: {MAP_PATH}")


def main():
    preset = ensure_preset()
    submix = ensure_submix(preset)
    ensure_clip(submix)
    for asset in (preset, submix):
        asset_library.save_loaded_asset(asset, only_if_is_dirty=False)
    asset_library.save_directory(ANECHOIC_FOLDER, only_if_is_dirty=False, recursive=True)
    ensure_receivers()
    unreal.log("M4 content done")


main()
