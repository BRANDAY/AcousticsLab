"""M9 content: amphitheatre positions, Steam Audio and Audio Tracing assets, the two industry levels (SPEC §3.4, §3.8).

Run from the project root while no editor has this project open (python commandlet, like create_m4_content.py):

  "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/AcousticsLab.uproject"
      -run=pythonscript -script="%CD%/Tools/Editor/create_m9_content.py [phase ...]"
      -unattended -nopause -nosplash -NullRHI -stdout -FullStdOutLogOutput

Phases (default: all, in this order): assets, amphitheatre, steam, tracing.

  assets        /Game/Acoustics/DA_AmphitheatrePositions (UAmphitheatrePositionsAsset, SPEC §3.4 defaults),
                /Game/Industry/A_Anechoic_TestSignal_Master (copy of the anechoic clip routed to the master submix),
                Steam Audio: /Game/Industry/SteamAudio/SAM_<preset> materials, SAS_Binaural / SAO_Occlusion /
                SAR_Reflections plugin settings, ATT_SteamAudio attenuation;
                Audio Tracing (when its plugin is loaded): /Game/Industry/AudioTracing/ATM_<preset> sound materials,
                ATT_AudioTracing attenuation.
  amphitheatre  /Game/Maps/Level_Amphitheatre: arena floor stand-in (the cavea is M10), source S1, receivers R1..R4,
                LevelSetup.Positions = DA_AmphitheatrePositions (positions applied).
  steam         /Game/Maps/Level_Amphitheatre_SteamAudio = copy of Level_Amphitheatre + both industry actors (Steam
                Audio key 5, starts; Audio Tracing key 6) + Steam Audio Geometry and Audio Tracing Sound Material on
                every "Acoustic" mesh + static geometry exported to /Game/Industry/SteamAudio/SAG_<level>.
  tracing       /Game/Maps/Level_Amphitheatre_AudioTracing = the same with Audio Tracing starting.

Keys 5 / 6 switch between the plugins at runtime in both levels (AIndustryAudioActor::HandleVariantKeyInWorld).

Rerun after a change of Level_Amphitheatre: the industry levels are rebuilt from it (M10: the AAmphitheatreActor is baked
into static mesh actors, see bake_amphitheatre; run create_m10_polish.py first).
Existing assets are reused and their settings re-applied.
"""
import sys

import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_library = unreal.EditorAssetLibrary
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

MAPS = "/Game/Maps"
AMPHITHEATRE = f"{MAPS}/Level_Amphitheatre"
STEAM_LEVEL = f"{MAPS}/Level_Amphitheatre_SteamAudio"
TRACING_LEVEL = f"{MAPS}/Level_Amphitheatre_AudioTracing"
POSITIONS = "/Game/Acoustics/DA_AmphitheatrePositions"
INDUSTRY = "/Game/Industry"
STEAM_FOLDER = f"{INDUSTRY}/SteamAudio"
TRACING_FOLDER = f"{INDUSTRY}/AudioTracing"
MESH_FOLDER = f"{INDUSTRY}/Meshes"
CLIP_SOURCE = "/Game/Audio/Anechoic/A_Anechoic_TestSignal"
CLIP_MASTER = f"{INDUSTRY}/A_Anechoic_TestSignal_Master"
CLICK = f"{INDUSTRY}/A_Click_48k"  # Saved/Audio/A_Click_48k.wav from Tools/Audio/make_click.py

# Our material presets (Source/AcousticsLab/Private/Scene/AcousticMaterialPresets.cpp): name -> (alpha, scattering).
PRESETS = {
    "Concrete": (0.03, 0.10),
    "Plaster": (0.05, 0.10),
    "Wood": (0.10, 0.15),
    "Audience": (0.70, 0.60),
    "WaterSurface": (0.01, 0.05),
    "Absorber": (0.90, 0.20),
    "Stone": (0.02, 0.20),  # M10: the amphitheatre rows and arena
}
# Broadband transmission through a wall (Steam Audio needs a value; ours has none): almost opaque.
TRANSMISSION = 0.01

STEAM_SPATIALIZER = "Steam Audio Spatialization"
# Audio Tracing (Fab): filled from the installed plugin (see docs/milestones/M9.md).
TRACING_MODULE = "AudioTracing"
TRACING_SPATIALIZER = "Audio Tracing Spatializer"
TRACING_MATERIAL_CLASS = f"/Script/{TRACING_MODULE}.ATSoundMaterial"
TRACING_MATERIAL_COMPONENT = f"/Script/{TRACING_MODULE}.ATSoundMaterialComponent"
TRACING_MAX_REFLECTION_DEPTH = "4"

# Arena floor stand-in: the engine plane (1 x 1 m) scaled to 100 x 80 m covers the arena and the cavea footprint.
FLOOR_LABEL = "ArenaFloor_StandIn"
FLOOR_SCALE = (100.0, 80.0, 1.0)


def log(text):
    unreal.log(f"M9 content: {text}")


def ensure_asset(name, folder, asset_class):
    path = f"{folder}/{name}"
    if asset_library.does_asset_exist(path):
        asset = asset_library.load_asset(path)
    else:
        asset_library.make_directory(folder)
        asset = asset_tools.create_asset(name, folder, asset_class, None)
        if asset is None:
            raise RuntimeError(f"could not create {path}")
        log(f"created {path}")
    return asset


def object_path(package_path):
    return f"{package_path}.{package_path.rsplit('/', 1)[-1]}"


def save(asset):
    asset_library.save_loaded_asset(asset, only_if_is_dirty=False)


def find_class(path):
    try:
        return unreal.load_class(None, path)
    except Exception:
        return None


# ---------------------------------------------------------------------------------------------------------------- assets

def make_positions():
    positions = ensure_asset("DA_AmphitheatrePositions", "/Game/Acoustics", unreal.AmphitheatrePositionsAsset)
    save(positions)
    return positions


def make_master_clip():
    if not asset_library.does_asset_exist(CLIP_MASTER):
        asset_library.make_directory(INDUSTRY)
        if not asset_library.duplicate_asset(CLIP_SOURCE, CLIP_MASTER):
            raise RuntimeError(f"could not duplicate {CLIP_SOURCE}")
        log(f"created {CLIP_MASTER}")
    clip = asset_library.load_asset(CLIP_MASTER)
    clip.set_editor_property("sound_submix_object", None)  # master submix: the plugin is the room here
    clip.set_editor_property("looping", False)
    save(clip)
    return clip


def make_click():
    import os
    wav = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Audio", "A_Click_48k.wav")
    if not os.path.exists(wav):
        unreal.log_warning(f"M9 content: {wav} missing (run Tools/Audio/make_click.py), click sound skipped")
        return None
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", wav)
    task.set_editor_property("destination_path", INDUSTRY)
    task.set_editor_property("destination_name", "A_Click_48k")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    asset_tools.import_asset_tasks([task])
    click = asset_library.load_asset(CLICK)
    if click is None:
        raise RuntimeError("could not import the click")
    click.set_editor_property("sound_submix_object", None)
    click.set_editor_property("looping", False)
    save(click)
    log(f"imported {CLICK}")
    return click


def make_attenuation(name, folder, spatial, occlusion, reverb, source_data_override):
    attenuation = ensure_asset(name, folder, unreal.SoundAttenuation)
    settings = attenuation.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(100.0, 0.0, 0.0))  # 1 m inner radius
    settings.set_editor_property("falloff_distance", 15000.0)  # 150 m: the whole amphitheatre
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("spatialization_algorithm", unreal.SoundSpatializationAlgorithm.SPATIALIZATION_HRTF)
    plugin_settings = settings.get_editor_property("plugin_settings")
    plugin_settings.set_editor_property("spatialization_plugin_settings_array", [spatial] if spatial else [])
    plugin_settings.set_editor_property("occlusion_plugin_settings_array", [occlusion] if occlusion else [])
    plugin_settings.set_editor_property("reverb_plugin_settings_array", [reverb] if reverb else [])
    plugin_settings.set_editor_property("source_data_override_plugin_settings_array", [source_data_override] if source_data_override else [])
    settings.set_editor_property("plugin_settings", plugin_settings)
    settings.set_editor_property("enable_occlusion", occlusion is not None)
    settings.set_editor_property("enable_reverb_send", reverb is not None)
    settings.set_editor_property("enable_source_data_override", source_data_override is not None or name.endswith("AudioTracing"))
    attenuation.set_editor_property("attenuation", settings)
    save(attenuation)
    return attenuation


def make_steam_assets():
    if not hasattr(unreal, "SteamAudioMaterial"):
        unreal.log_warning("M9 content: Steam Audio plugin not loaded, its assets are skipped")
        return False
    for name, (alpha, scattering) in PRESETS.items():
        material = ensure_asset(f"SAM_{name}", STEAM_FOLDER, unreal.SteamAudioMaterial)
        for band in ("low", "mid", "high"):
            material.set_editor_property(f"absorption_{band}", alpha)
            material.set_editor_property(f"transmission_{band}", TRANSMISSION)
        material.set_editor_property("scattering", scattering)
        save(material)
    spatial = ensure_asset("SAS_Binaural", STEAM_FOLDER, unreal.SteamAudioSpatializationSettings)
    spatial.set_editor_property("binaural", True)
    save(spatial)
    occlusion = ensure_asset("SAO_Occlusion", STEAM_FOLDER, unreal.SteamAudioOcclusionSettings)
    occlusion.set_editor_property("apply_occlusion", True)
    occlusion.set_editor_property("apply_transmission", False)
    occlusion.set_editor_property("apply_air_absorption", False)  # our solvers run without air absorption by default
    occlusion.set_editor_property("apply_distance_attenuation", False)  # the UE curve of the attenuation asset does it
    save(occlusion)
    reverb = ensure_asset("SAR_Reflections", STEAM_FOLDER, unreal.SteamAudioReverbSettings)
    reverb.set_editor_property("apply_reflections", True)
    save(reverb)
    make_attenuation("ATT_SteamAudio", STEAM_FOLDER, spatial, occlusion, reverb, None)
    return True


def make_tracing_assets():
    material_class = find_class(TRACING_MATERIAL_CLASS)
    if material_class is None:
        unreal.log_warning(f"M9 content: {TRACING_MATERIAL_CLASS} not found (Audio Tracing not installed / enabled), its assets are skipped")
        return False
    for name, (alpha, scattering) in PRESETS.items():
        material = ensure_asset(f"ATM_{name}", TRACING_FOLDER, material_class)
        material.set_editor_property("reflection_factor", 1.0 - alpha)
        material.set_editor_property("scattering_factor", scattering)
        save(material)
    make_attenuation("ATT_AudioTracing", TRACING_FOLDER, None, None, None, None)
    return True


# ---------------------------------------------------------------------------------------------------------- amphitheatre

def load_map(path):
    if not unreal.EditorLoadingAndSavingUtils.load_map(path):
        raise RuntimeError(f"could not load {path}")
    return {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}


def spawn(class_path, label, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0)):
    actor_class = unreal.load_class(None, class_path)
    actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), unreal.Rotator(*rotation))
    if actor is None:
        raise RuntimeError(f"could not spawn {label}")
    actor.set_actor_label(label)
    log(f"spawned {label}")
    return actor


def setup_amphitheatre(positions):
    by_label = load_map(AMPHITHEATRE)
    floor = by_label.get(FLOOR_LABEL)
    if floor is None:
        floor = spawn("/Script/Engine.StaticMeshActor", FLOOR_LABEL)
    component = floor.get_editor_property("static_mesh_component")
    component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    component.set_editor_property("component_tags", ["Acoustic", "AcMat:Concrete"])
    floor.set_actor_scale3d(unreal.Vector(*FLOOR_SCALE))
    floor.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)

    sources = [a for a in actors.get_all_level_actors() if a.get_class().get_name() == "AcousticSource"]
    if not any(str(s.get_editor_property("source_id")) == "S1" for s in sources):
        source = spawn("/Script/AcousticsLab.AcousticSource", "SourceS1")
        source.set_editor_property("source_id", "S1")
    receivers = {str(a.get_editor_property("receiver_id")) for a in actors.get_all_level_actors() if a.get_class().get_name() == "AcousticReceiver"}
    for receiver_id in ("R1", "R2", "R3", "R4"):
        if receiver_id not in receivers:
            receiver = spawn("/Script/AcousticsLab.AcousticReceiver", f"Receiver{receiver_id}")
            receiver.set_editor_property("receiver_id", receiver_id)

    setup = next(a for a in actors.get_all_level_actors() if a.get_class().get_name() == "LevelSetupActor")
    setup.set_editor_property("positions", positions)
    setup.apply_positions()
    # Spawn the player on R1 facing the source (the arena centre).
    player_start = by_label.get("PlayerStart")
    if player_start:
        seat = positions.get_seat_position_meters(positions.get_editor_property("seats")[0])
        player_start.set_actor_location(unreal.Vector(seat.x * 100.0, seat.y * 100.0 + 200.0, seat.z * 100.0 + 50.0), False, False)
        player_start.set_actor_rotation(unreal.Rotator(0.0, -10.0, -90.0), False)
    if not level_editor.save_current_level():
        raise RuntimeError("could not save Level_Amphitheatre")
    log("Level_Amphitheatre: floor stand-in, S1, R1..R4, positions applied, saved")


# --------------------------------------------------------------------------------------------------------- industry levels

def copy_amphitheatre(target):
    # "Save Current Level As" over the target: delete_asset + duplicate_asset fail in the commandlet, the force-deleted
    # map stays in the asset registry ("An asset already exists at this location").
    unreal.EditorLoadingAndSavingUtils.load_map(f"{MAPS}/Level_MainMenu")  # nothing of the target may stay loaded
    load_map(AMPHITHEATRE)
    if not unreal.EditorLoadingAndSavingUtils.save_map(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), target):
        raise RuntimeError(f"could not save {AMPHITHEATRE} as {target}")
    log(f"copied {AMPHITHEATRE} -> {target}")
    return load_map(target)


def bake_amphitheatre():
    """M10: Steam Audio exports only static meshes of AStaticMeshActors (SteamAudioScene.cpp, GetActorsForStaticGeometryExport),
    so the industry copy replaces the AAmphitheatreActor (dynamic meshes) by static mesh actors of the same triangles and
    tags: /Game/Industry/Meshes/SM_Amphitheatre_<part> (Allow CPU Access for our extractor). The audience / velarium
    toggles are therefore not available in the industry levels (empty stone amphitheatre, the reference condition)."""
    found = [a for a in actors.get_all_level_actors() if a.get_class().get_name() == "AmphitheatreActor"]
    if not found:
        return 0
    amphitheatre = found[0]
    amphitheatre.rebuild()
    asset_library.make_directory(MESH_FOLDER)
    count = 0
    for component_name, part in (("arena_floor", "Floor"), ("cavea", "Cavea"), ("velarium", "Velarium")):
        component = amphitheatre.get_editor_property(component_name)
        tags = [str(tag) for tag in component.get_editor_property("component_tags")]
        if "Acoustic" not in tags:
            continue
        mesh_path = f"{MESH_FOLDER}/SM_Amphitheatre_{part}"
        if asset_library.does_asset_exist(mesh_path):
            mesh = asset_library.load_asset(mesh_path)  # the builder is deterministic: the same triangles as before
        else:
            options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
            options.set_editor_property("enable_recompute_normals", True)
            options.set_editor_property("enable_collision", True)
            options.set_editor_property("collision_mode", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            mesh, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(component.get_dynamic_mesh(), mesh_path, options)
            if mesh is None:
                raise RuntimeError(f"could not create {mesh_path} ({outcome})")
            log(f"created {mesh_path}")
        mesh.set_editor_property("allow_cpu_access", True)
        save(mesh)
        actor = spawn("/Script/Engine.StaticMeshActor", f"Amphitheatre{part}")
        static = actor.get_editor_property("static_mesh_component")
        static.set_static_mesh(mesh)
        static.set_editor_property("component_tags", tags)
        render = amphitheatre.get_editor_property("stone_render_material")
        if render:
            static.set_material(0, render)
        actor.set_actor_location(amphitheatre.get_actor_location(), False, False)
        count += 1
    actors.destroy_actor(amphitheatre)
    log(f"amphitheatre baked into {count} static mesh actors")
    return count


def spec(class_path, properties=None):
    result = unreal.IndustryComponentSpec()
    result.set_editor_property("component_class", unreal.SoftClassPath(class_path))
    result.set_editor_property("properties", properties or {})
    return result


def configure_setup(kind, display_name):
    setup = next(a for a in actors.get_all_level_actors() if a.get_class().get_name() == "LevelSetupActor")
    setup.set_editor_property("level_kind", kind)
    setup.set_editor_property("display_name", display_name)


def add_steam_actor(level_name, auto_play):
    """Steam Audio industry actor (key 5) + Steam Audio Geometry on the Acoustic meshes + static geometry export."""
    industry = spawn("/Script/AcousticsLab.IndustryAudioActor", "IndustrySteamAudio")
    industry.set_editor_property("plugin", unreal.IndustryPlugin.STEAM_AUDIO)
    industry.set_editor_property("variant_key", 5)
    industry.set_editor_property("auto_play", auto_play)
    industry.set_editor_property("spatialization_plugin_name", STEAM_SPATIALIZER)
    industry.set_editor_property("clip", unreal.load_asset(CLIP_MASTER))
    if asset_library.does_asset_exist(CLICK):
        industry.get_editor_property("click_recorder").set_editor_property("click_sound", unreal.load_asset(CLICK))
    industry.set_editor_property("attenuation", unreal.load_asset(f"{STEAM_FOLDER}/ATT_SteamAudio"))
    industry.set_editor_property("runtime_components", [
        spec("/Script/SteamAudio.SteamAudioSourceComponent", {
            "bSimulateOcclusion": "True",
            "bSimulateReflections": "True",
            "ReflectionsType": "REALTIME",
        }),
        spec("/Script/SteamAudio.SteamAudioListenerComponent", {"bSimulateReverb": "False"}),
    ])
    industry.set_editor_property("mesh_component", spec("/Script/SteamAudio.SteamAudioGeometryComponent"))
    industry.set_editor_property("attach_mesh_component_to_primitive", False)
    industry.set_editor_property("mesh_material_property", "Material")
    industry.set_editor_property("materials_by_name", {name: unreal.SoftObjectPath(object_path(f"{STEAM_FOLDER}/SAM_{name}")) for name in PRESETS})
    added = industry.prepare_level()
    log(f"{level_name}: Steam Audio geometry components added: {added}")
    geometry = f"{STEAM_FOLDER}/SAG_{level_name}"
    if not unreal.IndustryLevelLibrary.export_steam_audio_static_geometry(industry, object_path(geometry)):
        raise RuntimeError(f"Steam Audio static geometry export failed ({level_name})")
    return industry


def add_tracing_actor(level_name, auto_play):
    """Audio Tracing industry actor (key 6) + Audio Tracing Sound Material under the Acoustic meshes (when installed)."""
    industry = spawn("/Script/AcousticsLab.IndustryAudioActor", "IndustryAudioTracing")
    industry.set_editor_property("plugin", unreal.IndustryPlugin.AUDIO_TRACING)
    industry.set_editor_property("variant_key", 6)
    industry.set_editor_property("auto_play", auto_play)
    industry.set_editor_property("spatialization_plugin_name", TRACING_SPATIALIZER)
    industry.set_editor_property("console_variables", {"AudioTracing.HardwareRayTracing.MaxReflectionDepth": TRACING_MAX_REFLECTION_DEPTH})
    industry.set_editor_property("clip", unreal.load_asset(CLIP_MASTER))
    if asset_library.does_asset_exist(CLICK):
        industry.get_editor_property("click_recorder").set_editor_property("click_sound", unreal.load_asset(CLICK))
    if asset_library.does_asset_exist(f"{TRACING_FOLDER}/ATT_AudioTracing"):
        industry.set_editor_property("attenuation", unreal.load_asset(f"{TRACING_FOLDER}/ATT_AudioTracing"))
    industry.set_editor_property("mesh_component", spec(TRACING_MATERIAL_COMPONENT))
    industry.set_editor_property("attach_mesh_component_to_primitive", True)
    industry.set_editor_property("mesh_material_property", "Material")
    industry.set_editor_property("materials_by_name", {name: unreal.SoftObjectPath(object_path(f"{TRACING_FOLDER}/ATM_{name}")) for name in PRESETS})
    if find_class(TRACING_MATERIAL_COMPONENT) is not None:
        log(f"{level_name}: Audio Tracing sound material components added: {industry.prepare_level()}")
    else:
        unreal.log_warning(f"M9 content: Audio Tracing not loaded, {level_name} gets its industry actor without sound materials (rerun 'steam tracing' after installing it)")
    return industry


def setup_industry_level(target, kind, display_name, own_plugin):
    """Both plugins in each industry level, switched at runtime by keys 5 / 6; the level's own plugin starts."""
    if not hasattr(unreal, "SteamAudioMaterial"):
        unreal.log_warning(f"M9 content: Steam Audio plugin not loaded, {target} skipped")
        return
    copy_amphitheatre(target)
    configure_setup(kind, display_name)
    bake_amphitheatre()
    level_name = target.rsplit("/", 1)[-1]
    add_steam_actor(level_name, own_plugin == "steam")
    add_tracing_actor(level_name, own_plugin == "tracing")
    if not level_editor.save_current_level():
        raise RuntimeError(f"could not save {level_name}")
    asset_library.save_directory(STEAM_FOLDER, only_if_is_dirty=False, recursive=True)
    log(f"{level_name} saved (keys 5 / 6 switch the plugin, {own_plugin} starts)")


def setup_steam_level():
    setup_industry_level(STEAM_LEVEL, unreal.AcousticLevelKind.INDUSTRY_STEAM_AUDIO, "2.1 Steam Audio (Valve)", "steam")


def setup_tracing_level():
    setup_industry_level(TRACING_LEVEL, unreal.AcousticLevelKind.INDUSTRY_AUDIO_TRACING, "2.2 Audio Tracing (Pinpoint)", "tracing")


def main(phases):
    positions = None
    if "assets" in phases:
        positions = make_positions()
        make_master_clip()
        make_click()
        make_steam_assets()
        make_tracing_assets()
    if "amphitheatre" in phases:
        setup_amphitheatre(positions or unreal.load_asset(POSITIONS))
    if "steam" in phases:
        setup_steam_level()
    if "tracing" in phases:
        setup_tracing_level()
    unreal.EditorLoadingAndSavingUtils.load_map(f"{MAPS}/Level_MainMenu")
    log("done")


main([arg for arg in sys.argv[1:] if not arg.startswith("-")] or ["assets", "amphitheatre", "steam", "tracing"])
