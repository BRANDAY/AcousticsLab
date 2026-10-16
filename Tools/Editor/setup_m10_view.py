"""M10: lights for Level_Amphitheatre (it had none), PlayerStart above the cavea, ray paths off for the heatmap views.
Through the editor's MCP server; saves the level.  python Tools/Editor/setup_m10_view.py
"""
from verify_m5_pie import Editor, set_props
from verify_m10_pie import actor_ref

SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"


def add(editor, class_path, location, rotation):
    try:
        return actor_ref(editor, class_path)
    except RuntimeError:
        pass
    result = editor.call(SCENE, "add_to_scene_from_class", actor_type={"refPath": class_path}, name=class_path.rsplit(".", 1)[-1],
                         xform={"location": dict(zip("xyz", location)), "rotation": dict(zip(("pitch", "yaw", "roll"), rotation)), "scale": {"x": 1, "y": 1, "z": 1}})
    print("added", class_path, str(result)[:200])
    return actor_ref(editor, class_path)


def main():
    editor = Editor()
    sun = add(editor, "/Script/Engine.DirectionalLight", (0, 0, 5000), (-40, 30, 0))
    set_props(editor, sun + ".LightComponent0", {"bAtmosphereSunLight": True, "Intensity": 8.0})
    add(editor, "/Script/Engine.SkyAtmosphere", (0, 0, 0), (0, 0, 0))
    sky = add(editor, "/Script/Engine.SkyLight", (0, 0, 3000), (0, 0, 0))
    set_props(editor, sky + ".SkyLightComponent0", {"bRealTimeCapture": True})
    manager = actor_ref(editor, "/Script/AcousticsLab.AcousticSimulationManager")
    set_props(editor, manager + ".PathVisualizer", {"bEnabled": False})
    start = actor_ref(editor, "/Script/Engine.PlayerStart")
    print("player start", start)
    print(editor.call(ACTOR, "set_actor_transform", actor={"refPath": start},
                      xform={"location": {"x": 0, "y": -6000, "z": 3800}, "rotation": {"pitch": -30, "yaw": 90, "roll": 0}, "scale": {"x": 1, "y": 1, "z": 1}}))
    print(editor.call(ASSET, "save_assets", asset_paths=[]))


if __name__ == "__main__":
    main()
