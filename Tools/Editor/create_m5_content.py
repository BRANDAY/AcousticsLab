"""M5 content through the editor's MCP server (see docs/ENV.md, "Unreal MCP"):

    python Tools/Editor/create_m5_content.py        # editor running with -ModelContextProtocolStartServer -ModelContextProtocolPort=8000

Creates /Game/Acoustics/Materials/M_BeamFrustum: Unlit, Translucent, two-sided, EmissiveColor <- VertexColor.rgb,
Opacity <- VertexColor.a. UBeamVisualizerComponent writes the order color and the opacity into the vertex color overlay of
its UDynamicMeshComponents, so one material serves every order. Same MaterialTools recipe as create_m3_content.py.
"""
import json
import sys

from mcp_client import McpClient, text_of

MATERIAL = "editor_toolset.toolsets.material.MaterialTools"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
MATERIAL_FOLDER = "/Game/Acoustics/Materials"
MATERIAL_NAME = "M_BeamFrustum"
MATERIAL_PATH = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"


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


def material_ref():
    return f"{MATERIAL_PATH}.{MATERIAL_NAME}"


def create_beam_material(editor):
    """M_BeamFrustum: Unlit, Translucent, two-sided; Emissive <- VertexColor.rgb, Opacity <- VertexColor.a."""
    if editor.call(ASSET, "exists", path=MATERIAL_PATH) is True:
        print(f"{MATERIAL_PATH} exists; updating its properties and expressions")
        ref = material_ref()
    else:
        ref = Editor.ref_of(editor.call(MATERIAL, "create_material", folder_path=MATERIAL_FOLDER, asset_name=MATERIAL_NAME)) or material_ref()
        print(f"created {ref}")
    editor.call(OBJECT, "set_properties", instance=Editor.ref(ref),
                values=json.dumps({"ShadingModel": "MSM_Unlit", "BlendMode": "BLEND_Translucent", "TwoSided": True,
                                   "bUsedWithInstancedStaticMeshes": False, "bDisableDepthTest": False}))

    expressions = editor.call(MATERIAL, "get_expressions", material_or_function=Editor.ref(ref))
    vertex_color = None
    if isinstance(expressions, list):
        for entry in expressions:
            path = Editor.ref_of(entry) or ""
            if "MaterialExpressionVertexColor" in path:
                vertex_color = path
    if not vertex_color:
        node = editor.call(MATERIAL, "add_expression", material_or_function=Editor.ref(ref),
                           expression_class=Editor.ref("/Script/Engine.MaterialExpressionVertexColor"), x=-400, y=0)
        vertex_color = Editor.ref_of(node)
        print(f"added VertexColor expression {vertex_color}")
    editor.call(MATERIAL, "connect_to_output", expression=Editor.ref(vertex_color), output_name="", material_property="MP_EmissiveColor")
    editor.call(MATERIAL, "connect_to_output", expression=Editor.ref(vertex_color), output_name="A", material_property="MP_Opacity")
    editor.call(MATERIAL, "recompile", material_or_function=Editor.ref(ref))
    props = editor.call(OBJECT, "get_properties", instance=Editor.ref(ref), properties=["ShadingModel", "BlendMode", "TwoSided"])
    print("material properties:", props)
    return ref


def main():
    editor = Editor()
    create_beam_material(editor)
    print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))
    print("M5 content done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
