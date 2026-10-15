"""M6 verification through the editor's MCP server (docs/ENV.md, "Unreal MCP"): PIE runs of the beam tracer with refraction
on the open Level_Refraction, their LogAcoustics lines and screenshots of the editor window.

    python Tools/Editor/verify_m6_pie.py [--shots docs/milestones/img]

Runs: (1) pool, source "Pool" in air above the water, level 4 / order 3 - the beams bend at the surface (water beams in
cyan/blue), the log reports refracted beams and total internal reflections; (2) lens, directional source "Lens" in glycerol,
level 6 / order 3 - the heatmap on the vertical grid L (metric "Total level") shows the focal spot inside the back of the
rubber sphere. The PlayerStart is moved in front of each scene before PIE (the PIE pawn spawns there). Afterwards the
manager goes back to source Pool, bRunOnBeginPlay = false, PlayerStart back beside the pool, save.
"""
import json
import os
import subprocess
import sys
import time

from mcp_client import McpClient, text_of

EDITOR_APP = "EditorToolset.EditorAppToolset"
LOGS = "EditorToolset.LogsToolset"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CAPTURE = os.path.join(PROJECT_DIR, "Tools", "Editor", "capture_window.ps1")
CM = 100.0

POOL_START = ((-24.0, -16.0, 5.5), (-7.0, 68.0, 0.0))  # the pool sits left of the HUD panel
LENS_START = ((4.2, 56.8, 2.7), (-6.0, 74.0, 0.0))


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


def log_entries(editor, pattern, max_entries=300):
    entries = editor.call(LOGS, "GetLogEntries", category="LogAcoustics", pattern=pattern, maxEntries=max_entries) or []
    lines = []
    for entry in entries:
        if isinstance(entry, dict):
            lines.append(entry.get("message") or entry.get("Message") or json.dumps(entry, ensure_ascii=False))
        else:
            lines.append(str(entry))
    return lines


def wait_for(editor, pattern, timeout_seconds, minimum=1):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        lines = log_entries(editor, pattern)
        if len(lines) >= minimum:
            return lines
        time.sleep(2.0)
    return []


def capture(out_path):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    completed = subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", CAPTURE, "-TitlePattern", "Unreal Editor", "-Out", out_path],
                               capture_output=True, text=True, timeout=60)
    print("capture:", out_path, (completed.stdout or completed.stderr).strip()[:200])
    return completed.returncode == 0 and os.path.exists(out_path)


def set_props(editor, ref, values):
    editor.call(OBJECT, "set_properties", instance=Editor.ref(ref), values=json.dumps(values))


def find_actor(editor, label):
    for actor in editor.call(SCENE, "find_actors", name=label, tag="", collision_channels=[]) or []:
        ref = Editor.ref_of(actor)
        if ref:
            return ref
    return None


def move_player_start(editor, start_ref, location_m, rotation):
    """ActorTools.set_actor_transform {actor, xform: {location, rotation, scale}} (cm, degrees)."""
    if not start_ref:
        return
    editor.call(ACTOR, "set_actor_transform", actor=Editor.ref(start_ref),
                xform={"location": {"x": location_m[0] * CM, "y": location_m[1] * CM, "z": location_m[2] * CM},
                       "rotation": {"pitch": rotation[0], "yaw": rotation[1], "roll": rotation[2]},
                       "scale": {"x": 1, "y": 1, "z": 1}})


def run_case(editor, manager, name, manager_props, beam_props, pattern, shots, failures, settle=8.0):
    # The output log keeps the lines of earlier PIE sessions: wait for one more run summary than there are now.
    baseline = len(log_entries(editor, pattern))
    set_props(editor, manager, dict({"Solver": "BeamTracer", "bRunOnBeginPlay": True}, **manager_props))
    if beam_props:
        set_props(editor, manager + ".BeamVisualizer", beam_props)
    try:
        editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
        print(f"PIE started: {name}")
        lines = wait_for(editor, pattern, 900, minimum=baseline + 1)
        if not lines:
            failures.append(f"{name}: no new '{pattern}' line within 900 s")
        else:
            print(lines[-1][:400])
        time.sleep(settle)
        if shots:
            capture(os.path.join(shots, f"M6_{name}.png"))
        summary = []
        for line in log_entries(editor, "BeamTracer")[-14:] + log_entries(editor, "Refraction")[-6:]:
            print("  ", line[:300])
            summary.append(line)
        return summary
    finally:
        editor.call(EDITOR_APP, "StopPIE")
        print("PIE stopped")
        time.sleep(3.0)


def main():
    shots = None
    if "--shots" in sys.argv:
        shots = os.path.abspath(sys.argv[sys.argv.index("--shots") + 1])
    editor = Editor()
    manager = find_actor(editor, "SimulationManager")
    if not manager:
        print("SimulationManager not found in the open level")
        return 1
    start = find_actor(editor, "PlayerStart")
    failures = []
    try:
        set_props(editor, manager + ".PathVisualizer", {"bEnabled": False})
        # (1) Pool: beams from the air source bend at the water surface; grazing beams are totally reflected.
        move_player_start(editor, start, *POOL_START)
        pool = run_case(editor, manager, "pool_refraction",
                        {"SourceId": "Pool", "Settings": {"SubdivisionLevel": 4, "MaxBeamOrder": 3, "bRefraction": True, "ReceiverRadius": 0.0}},
                        {"bEnabled": True, "MinDrawOrder": 1, "MaxDrawOrder": 1, "bOnlyBeamsContainingReceiver": False, "bDrawEscapedBeams": False, "Opacity": 0.08, "HighlightOpacity": 0.3},
                        "nodes by order", shots, failures)
        if not any("refracted" in line for line in pool):
            failures.append("pool: no refraction counters in the log")
        run_case(editor, manager, "pool_receiver",
                 {"SourceId": "Pool", "Settings": {"SubdivisionLevel": 4, "MaxBeamOrder": 3, "bRefraction": True, "ReceiverRadius": 0.0}},
                 {"bEnabled": True, "MinDrawOrder": 0, "MaxDrawOrder": 3, "bOnlyBeamsContainingReceiver": True, "bDrawEscapedBeams": True, "Opacity": 0.06, "HighlightOpacity": 0.25},
                 "nodes by order", shots, failures)
        # (2) Lens: directional source in glycerol, rubber sphere; focal spot on the vertical heatmap.
        move_player_start(editor, start, *LENS_START)
        lens = run_case(editor, manager, "lens_focus",
                        {"SourceId": "Lens", "Settings": {"SubdivisionLevel": 6, "MaxBeamOrder": 3, "bRefraction": True, "ReceiverRadius": 0.0}},
                        {"bEnabled": True, "MinDrawOrder": 0, "MaxDrawOrder": 3, "bOnlyBeamsContainingReceiver": False, "bDrawEscapedBeams": False, "Opacity": 0.05, "HighlightOpacity": 0.3},
                        "nodes by order", shots, failures, settle=10.0)
        if not any("refracted" in line for line in lens):
            failures.append("lens: no refraction counters in the log")
    finally:
        move_player_start(editor, start, *POOL_START)
        set_props(editor, manager, {"Solver": "BeamTracer", "SourceId": "Pool", "bRunOnBeginPlay": False,
                                    "Settings": {"SubdivisionLevel": 4, "MaxBeamOrder": 3, "bRefraction": True, "ReceiverRadius": 0.0}})
        set_props(editor, manager + ".PathVisualizer", {"bEnabled": False})
        set_props(editor, manager + ".BeamVisualizer", {"bEnabled": True, "MinDrawOrder": 0, "MaxDrawOrder": 2, "bOnlyBeamsContainingReceiver": False, "bDrawEscapedBeams": False, "Opacity": 0.12, "HighlightOpacity": 0.4})
        print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))
    print("--- LogAcoustics: warnings / errors ---")
    for line in log_entries(editor, "Warning")[-10:] + log_entries(editor, "Error")[-10:]:
        print("  ", line[:300])
    if failures:
        print("FAILURES:")
        for failure in failures:
            print(" -", failure)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
