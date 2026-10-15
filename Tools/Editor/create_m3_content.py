"""M3 content through the editor's MCP server (see docs/ENV.md, "Unreal MCP").

Creates /Game/Acoustics/Materials/M_HeatmapTile (unlit, two-sided, used with instanced static meshes,
emissive = PerInstanceCustomData3Vector index 0, the material UHeatmapComponent expects) and places a ReceiverGrid
actor (10 x 7 tiles of 1 m, receivers at 1.2 m) in the open Level_Shoebox, then saves every dirty package.
Safe to re-run: an existing material / expression / actor is reused and its settings are applied again.

    python Tools/Editor/create_m3_content.py            # editor running with -ModelContextProtocolStartServer -ModelContextProtocolPort=8000

Tool shapes verified on UE 5.8.3 (describe_toolset): AssetTools.exists {path}; MaterialTools.create_material
{folder_path, asset_name}, get_expressions {material_or_function}, add_expression {material_or_function,
expression_class, x, y}, connect_to_output {expression, output_name, material_property}, recompile
{material_or_function}; SceneTools.find_actors needs name, tag and collision_channels (pass "" and []);
add_to_scene_from_class xform is {location {x,y,z}, rotation {pitch,yaw,roll}, scale {x,y,z}}.
"""
import json
import sys

from mcp_client import McpClient, text_of

SCENE = "editor_toolset.toolsets.scene.SceneTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
MATERIAL = "editor_toolset.toolsets.material.MaterialTools"

MATERIAL_FOLDER = "/Game/Acoustics/Materials"
MATERIAL_NAME = "M_HeatmapTile"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}.{MATERIAL_NAME}"
EXPRESSION_CLASS = "/Script/Engine.MaterialExpressionPerInstanceCustomData3Vector"
GRID_LABEL = "ReceiverGrid"
GRID_CLASS = "/Script/AcousticsLab.ReceiverGrid"


class Editor:
    def __init__(self):
        self.client = McpClient()
        self.client.initialize()

    def call(self, toolset, tool, **arguments):
        result = self.client.call("call_tool", {"toolset_name": toolset, "tool_name": tool, "arguments": arguments})
        text = text_of(result)
        if result.get("isError"):
            raise RuntimeError(f"{toolset}.{tool} failed: {text[:2000]}")
        try:
            parsed = json.loads(text)
        except (ValueError, TypeError):
            return text
        # The tools answer {"returnValue": ...}; tools without a return value answer {} or {"returnValue": null}.
        if isinstance(parsed, dict) and "returnValue" in parsed:
            return parsed["returnValue"]
        return parsed

    @staticmethod
    def ref(path):
        return {"refPath": path}

    @staticmethod
    def ref_of(value):
        """refPath of an object returned by a tool ({"refPath": ...})."""
        if isinstance(value, dict) and "refPath" in value:
            return value["refPath"]
        raise RuntimeError(f"no refPath in {str(value)[:300]}")


def ensure_material(editor):
    """M_HeatmapTile: Unlit, two-sided, EmissiveColor <- PerInstanceCustomData3Vector(DataIndex 0)."""
    if editor.call(ASSET, "exists", path=MATERIAL_PATH) is True:
        material_ref = MATERIAL_PATH
        print("material exists:", material_ref)
    else:
        material_ref = Editor.ref_of(editor.call(MATERIAL, "create_material", folder_path=MATERIAL_FOLDER, asset_name=MATERIAL_NAME))
        print("created", material_ref)

    editor.call(OBJECT, "set_properties", instance=Editor.ref(material_ref),
                values=json.dumps({"ShadingModel": "MSM_Unlit", "TwoSided": True, "bUsedWithInstancedStaticMeshes": True}))

    expressions = editor.call(MATERIAL, "get_expressions", material_or_function=Editor.ref(material_ref)) or []
    node_ref = next((Editor.ref_of(e) for e in expressions if "PerInstanceCustomData3Vector" in Editor.ref_of(e)), None)
    if node_ref is None:
        node_ref = Editor.ref_of(editor.call(MATERIAL, "add_expression", material_or_function=Editor.ref(material_ref),
                                             expression_class=Editor.ref(EXPRESSION_CLASS), x=-400, y=0))
        print("expression added:", node_ref)
    else:
        print("expression exists:", node_ref)
    editor.call(OBJECT, "set_properties", instance=Editor.ref(node_ref),
                values=json.dumps({"DataIndex": 0, "ConstDefaultValue": {"R": 0.05, "G": 0.05, "B": 0.06, "A": 1.0}}))
    editor.call(MATERIAL, "connect_to_output", expression=Editor.ref(node_ref), output_name="", material_property="MP_EmissiveColor")
    editor.call(MATERIAL, "recompile", material_or_function=Editor.ref(material_ref))
    print("material graph built:", editor.call(OBJECT, "get_properties", instance=Editor.ref(material_ref),
                                               properties=["ShadingModel", "TwoSided", "bUsedWithInstancedStaticMeshes"]))
    return material_ref


def ensure_grid(editor):
    actors = editor.call(SCENE, "find_actors", name=GRID_LABEL, tag="", collision_channels=[]) or []
    if actors:
        actor_ref = Editor.ref_of(actors[0])
        print("grid exists:", actor_ref)
    else:
        actor_ref = Editor.ref_of(editor.call(SCENE, "add_to_scene_from_class", actor_type=Editor.ref(GRID_CLASS), name=GRID_LABEL,
                                              xform={"location": {"x": 0, "y": 0, "z": 0},
                                                     "rotation": {"pitch": 0, "yaw": 0, "roll": 0},
                                                     "scale": {"x": 1, "y": 1, "z": 1}}))
        print("grid placed:", actor_ref)
    # Setting properties re-runs the actor's construction (it loads the heatmap material in OnConstruction).
    editor.call(OBJECT, "set_properties", instance=Editor.ref(actor_ref),
                values=json.dumps({"GridId": "G", "NumTilesX": 10, "NumTilesY": 7, "TileSizeMeters": 1.0,
                                   "ReceiverHeightMeters": 1.2, "ReceiverRadiusMeters": 0.5, "TileZOffsetMeters": 0.02}))
    return actor_ref


def main():
    editor = Editor()
    ensure_material(editor)
    ensure_grid(editor)
    print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
