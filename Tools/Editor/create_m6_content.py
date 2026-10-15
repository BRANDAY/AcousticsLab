"""M6 content through the editor's MCP server (docs/ENV.md, "Unreal MCP"): Level_Refraction.

    python Tools/Editor/create_m6_content.py        # editor open on /Game/Maps/Level_Refraction with -ModelContextProtocolStartServer -ModelContextProtocolPort=8000

Creates /Game/Acoustics/Materials/M_Interface (unlit, translucent, two-sided: Emissive <- Constant3Vector, Opacity <- Constant)
and places in the open level: RefractionDemo (ARefractionDemoActor: pool 20 x 10 x 3 m with the water surface at 2 m, and
the lens scene 60 m away on +Y), the sources Pool (omnidirectional, in air, 1.5 m above the water) and Lens (directional,
cone 4.5 deg, in glycerol, 7.5 m in front of the rubber sphere), the receiver grids P (18 x 8 x 1 m, 1 m under the water)
and L (30 x 12 x 0.1 m, vertical, in the axial plane through the sphere), the point receivers R1 / R2 (under water) and
R3 (paraxial focus inside the back of the sphere) / R4 (behind the sphere), the SimulationManager (beam tracer, refraction
on, level 4, order 3, source Pool, receiver radius 0 = own radii) and the PlayerStart beside the pool. Safe to re-run:
existing actors are found by label and their properties are applied again. Positions in meters here, cm in the calls.
"""
import json
import sys

from mcp_client import McpClient, text_of

SCENE = "editor_toolset.toolsets.scene.SceneTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
MATERIAL = "editor_toolset.toolsets.material.MaterialTools"

MATERIAL_FOLDER = "/Game/Acoustics/Materials"
MATERIAL_NAME = "M_Interface"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"
LEVEL = "/Game/Maps/Level_Refraction.Level_Refraction:PersistentLevel"

CM = 100.0
LENS_OFFSET = (0.0, 60.0, 2.0)        # ARefractionDemoActor::LensOffsetMeters (the lens source position)
LENS_CENTRE = (7.5, 60.0, 2.0)        # + LensCentreOffsetMeters
LENS_RADIUS = 0.5
FOCUS_X = LENS_CENTRE[0] + 0.35       # paraxial focus of the ball lens: n R / (2 (n - 1)) = 0.47 m from the centre for n = 2.13;
                                      # with the source at 7.5 m (not at infinity) the focus moves a little further back, so
                                      # R3 sits at +0.35 m, inside the back of the sphere where the beams converge


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
        if isinstance(parsed, dict) and "returnValue" in parsed:
            return parsed["returnValue"]
        return parsed

    @staticmethod
    def ref(path):
        return {"refPath": path}

    @staticmethod
    def ref_of(value):
        if isinstance(value, dict):
            for key in ("refPath", "path", "objectPath"):
                if key in value:
                    return value[key]
            for inner in value.values():
                found = Editor.ref_of(inner)
                if found:
                    return found
        if isinstance(value, str) and value.startswith("/"):
            return value
        return None


def set_props(editor, ref, values):
    editor.call(OBJECT, "set_properties", instance=Editor.ref(ref), values=json.dumps(values))


def ensure_material(editor):
    """M_Interface: Unlit, Translucent, two-sided; Emissive <- Constant3Vector (water blue), Opacity <- Constant 0.3."""
    if editor.call(ASSET, "exists", path=MATERIAL_PATH) is True:
        ref = f"{MATERIAL_PATH}.{MATERIAL_NAME}"
        print("material exists:", ref)
    else:
        ref = Editor.ref_of(editor.call(MATERIAL, "create_material", folder_path=MATERIAL_FOLDER, asset_name=MATERIAL_NAME)) or f"{MATERIAL_PATH}.{MATERIAL_NAME}"
        print("created", ref)
    set_props(editor, ref, {"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Translucent", "TwoSided": True})

    expressions = editor.call(MATERIAL, "get_expressions", material_or_function=Editor.ref(ref)) or []
    color_node = None
    opacity_node = None
    for entry in expressions:
        path = Editor.ref_of(entry) or ""
        if "MaterialExpressionConstant3Vector" in path:
            color_node = path
        elif "MaterialExpressionConstant_" in path or path.endswith("MaterialExpressionConstant"):
            opacity_node = path
    if not color_node:
        color_node = Editor.ref_of(editor.call(MATERIAL, "add_expression", material_or_function=Editor.ref(ref),
                                               expression_class=Editor.ref("/Script/Engine.MaterialExpressionConstant3Vector"), x=-400, y=0))
        print("added Constant3Vector", color_node)
    if not opacity_node:
        opacity_node = Editor.ref_of(editor.call(MATERIAL, "add_expression", material_or_function=Editor.ref(ref),
                                                 expression_class=Editor.ref("/Script/Engine.MaterialExpressionConstant"), x=-400, y=200))
        print("added Constant", opacity_node)
    set_props(editor, color_node, {"Constant": {"R": 0.12, "G": 0.45, "B": 0.95, "A": 1.0}})
    set_props(editor, opacity_node, {"R": 0.3})
    editor.call(MATERIAL, "connect_to_output", expression=Editor.ref(color_node), output_name="", material_property="MP_EmissiveColor")
    editor.call(MATERIAL, "connect_to_output", expression=Editor.ref(opacity_node), output_name="", material_property="MP_Opacity")
    editor.call(MATERIAL, "recompile", material_or_function=Editor.ref(ref))
    print("material:", editor.call(OBJECT, "get_properties", instance=Editor.ref(ref), properties=["ShadingModel", "BlendMode", "TwoSided"]))
    return ref


def find_actor(editor, label):
    actors = editor.call(SCENE, "find_actors", name=label, tag="", collision_channels=[]) or []
    for actor in actors:
        ref = Editor.ref_of(actor)
        if ref:
            return ref
    return None


def ensure_actor(editor, label, class_path, location_m, rotation=(0.0, 0.0, 0.0), props=None):
    """Finds the actor by label or spawns it; applies props (set_properties re-runs OnConstruction)."""
    ref = find_actor(editor, label)
    if ref:
        print(f"{label} exists: {ref}")
    else:
        ref = Editor.ref_of(editor.call(SCENE, "add_to_scene_from_class", actor_type=Editor.ref(class_path), name=label,
                                        xform={"location": {"x": location_m[0] * CM, "y": location_m[1] * CM, "z": location_m[2] * CM},
                                               "rotation": {"pitch": rotation[0], "yaw": rotation[1], "roll": rotation[2]},
                                               "scale": {"x": 1, "y": 1, "z": 1}}))
        print(f"{label} placed: {ref}")
    if props:
        set_props(editor, ref, props)
    return ref


def main():
    editor = Editor()
    ensure_material(editor)

    # Level setup: kind + the extractor keeps its defaults (materials and media resolve through the presets).
    setup = find_actor(editor, "LevelSetup") or find_actor(editor, "LevelSetupActor")
    if setup:
        set_props(editor, setup, {"LevelKind": "Refraction", "DisplayName": "Pool and acoustic lens: beam tracing with refraction (M6)"})
        print("level setup:", setup)

    demo = ensure_actor(editor, "RefractionDemo", "/Script/AcousticsLab.RefractionDemoActor", (0.0, 0.0, 0.0),
                        props={"bBuildPool": True, "PoolSizeMeters": {"X": 20.0, "Y": 10.0, "Z": 3.0}, "WaterDepthMeters": 2.0,
                               "bBuildLens": True, "LensOffsetMeters": {"X": LENS_OFFSET[0], "Y": LENS_OFFSET[1], "Z": LENS_OFFSET[2]},
                               "LensCentreOffsetMeters": {"X": 7.5, "Y": 0.0, "Z": 0.0}, "LensRadiusMeters": LENS_RADIUS,
                               "LensNumPhi": 24, "LensNumTheta": 48})

    # Sources: Pool above the water (a third along the pool), Lens directional towards the sphere.
    ensure_actor(editor, "SourcePool", "/Script/AcousticsLab.AcousticSource", (-20.0 / 3.0, 0.0, 3.5),
                 props={"SourceId": "Pool", "MediumName": "Air", "bDirectional": False})
    ensure_actor(editor, "SourceLens", "/Script/AcousticsLab.AcousticSource", LENS_OFFSET, rotation=(0.0, 0.0, 0.0),
                 props={"SourceId": "Lens", "MediumName": "Glycerol", "bDirectional": True, "ConeHalfAngleDeg": 4.5})

    # Receivers: R1 under the pool source, R2 aside under water, R3 at the lens focus, R4 behind the sphere.
    ensure_actor(editor, "ReceiverR1", "/Script/AcousticsLab.AcousticReceiver", (-20.0 / 3.0, 0.0, 1.0), props={"ReceiverId": "R1", "RadiusMeters": 0.5})
    ensure_actor(editor, "ReceiverR2", "/Script/AcousticsLab.AcousticReceiver", (-2.0, 1.5, 1.0), props={"ReceiverId": "R2", "RadiusMeters": 0.5})
    ensure_actor(editor, "ReceiverR3", "/Script/AcousticsLab.AcousticReceiver", (FOCUS_X, LENS_CENTRE[1], LENS_CENTRE[2]), props={"ReceiverId": "R3", "RadiusMeters": 0.05})
    ensure_actor(editor, "ReceiverR4", "/Script/AcousticsLab.AcousticReceiver", (LENS_CENTRE[0] + 1.5, LENS_CENTRE[1], LENS_CENTRE[2]), props={"ReceiverId": "R4", "RadiusMeters": 0.1})

    # Grids: P horizontal 1 m under the water surface; L vertical in the axial plane (roll 90 deg: local Y -> world Z).
    ensure_actor(editor, "ReceiverGridPool", "/Script/AcousticsLab.ReceiverGrid", (0.0, 0.0, 1.0),
                 props={"GridId": "P", "NumTilesX": 18, "NumTilesY": 8, "TileSizeMeters": 1.0, "ReceiverHeightMeters": 0.0,
                        "ReceiverRadiusMeters": 0.5, "TileZOffsetMeters": 0.0})
    ensure_actor(editor, "ReceiverGridLens", "/Script/AcousticsLab.ReceiverGrid", LENS_CENTRE, rotation=(0.0, 0.0, 90.0),
                 props={"GridId": "L", "NumTilesX": 30, "NumTilesY": 12, "TileSizeMeters": 0.1, "ReceiverHeightMeters": 0.0,
                        "ReceiverRadiusMeters": 0.05, "TileZOffsetMeters": 0.0})
    # Both grids show the total level (dB): under water and behind the lens there is no direct sound to normalise by.
    for label in ("ReceiverGridPool", "ReceiverGridLens"):
        grid = find_actor(editor, label)
        if grid:
            set_props(editor, grid + ".Heatmap", {"Metric": "TotalLevel"})

    # The placeholder sign of M0 is no longer needed.
    sign = find_actor(editor, "PlaceholderSign")
    if sign:
        editor.call(SCENE, "remove_from_scene", actor=Editor.ref(sign))
        print("removed", sign)

    manager = ensure_actor(editor, "SimulationManager", "/Script/AcousticsLab.AcousticSimulationManager", (0.0, 0.0, 0.0))
    set_props(editor, manager, {"Solver": "BeamTracer", "SourceId": "Pool", "bRunOnBeginPlay": False,
                                "Settings": {"SubdivisionLevel": 4, "MaxBeamOrder": 3, "bRefraction": True, "ReceiverRadius": 0.0,
                                             "TMax": 1.0, "MaxBeamNodes": 2000000, "NumRays": 20000, "MaxOrder": 200}})
    set_props(editor, manager + ".PathVisualizer", {"bEnabled": False})
    set_props(editor, manager + ".BeamVisualizer", {"bEnabled": True, "MaxDrawOrder": 2, "Opacity": 0.15})

    # Player start beside the pool, looking into it.
    start = find_actor(editor, "PlayerStart")
    if not start:
        ensure_actor(editor, "PlayerStart", "/Script/Engine.PlayerStart", (-14.0, -6.0, 4.5), rotation=(-10.0, 20.0, 0.0))
    print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))
    print("M6 content done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
