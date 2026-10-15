"""M5 verification through the editor's MCP server (docs/ENV.md, "Unreal MCP"): PIE runs of the beam tracer and of the
image-source solver on the open Level_Shoebox, their LogAcoustics lines and screenshots of the editor window.

    python Tools/Editor/verify_m5_pie.py [--shots docs/milestones/img]

Steps per solver: SimulationManager.Solver + bRunOnBeginPlay = true (and the visualizer component properties for the
view) -> StartPIE -> wait for the run summary in LogAcoustics -> screenshot (Tools/Editor/capture_window.ps1) -> StopPIE.
Afterwards Solver = RayTracer, bRunOnBeginPlay = false, save. No console-command toolset exists in 5.8.3 (see M4), so the
view toggles are set as component properties on the editor actor before PIE (the PIE copy inherits them).
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
MANAGER_REF = "/Game/Maps/Level_Shoebox.Level_Shoebox:PersistentLevel.AcousticSimulationManager_0"
PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CAPTURE = os.path.join(PROJECT_DIR, "Tools", "Editor", "capture_window.ps1")


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


def run_case(editor, solver, pattern, shots, failures, views):
    """One PIE run of a solver; views = list of (suffix, manager props, image-visualizer props, beam-visualizer props)."""
    for suffix, manager_props, image_props, beam_props in views:
        baseline = len(log_entries(editor, "Simulation run"))
        set_props(editor, MANAGER_REF, dict({"Solver": solver, "bRunOnBeginPlay": True}, **manager_props))
        if image_props:
            set_props(editor, MANAGER_REF + ".ImageSourceVisualizer", image_props)
        if beam_props:
            set_props(editor, MANAGER_REF + ".BeamVisualizer", beam_props)
        try:
            editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
            print(f"PIE started: {solver} {suffix}")
            lines = wait_for(editor, pattern, 180, minimum=1)
            if not lines:
                failures.append(f"{solver} {suffix}: no '{pattern}' line within 180 s")
            else:
                print(lines[-1][:400])
            time.sleep(6.0)  # let the meshes and the panel settle
            if shots:
                capture(os.path.join(shots, f"M5_{solver.lower()}_{suffix}.png"))
            for line in log_entries(editor, solver)[-12:]:
                print("  ", line[:300])
        finally:
            editor.call(EDITOR_APP, "StopPIE")
            print("PIE stopped")
            time.sleep(3.0)


def main():
    shots = None
    if "--shots" in sys.argv:
        shots = os.path.abspath(sys.argv[sys.argv.index("--shots") + 1])
    editor = Editor()
    failures = []
    try:
        # Ray paths off while the beams are shown: the white polylines hide the frustums.
        set_props(editor, MANAGER_REF + ".PathVisualizer", {"bEnabled": False})
        run_case(editor, "BeamTracer", "BeamTracer:", shots, failures, [
            # Low opacities for the captures: the camera always sits inside several frustums (the beams that contain the
            # receiver arrive from every direction and continue to the walls), so the default values fill the view.
            ("beams_receiver", {}, {"bEnabled": False}, {"bEnabled": True, "MinDrawOrder": 2, "MaxDrawOrder": 3, "bOnlyBeamsContainingReceiver": True, "HighlightOpacity": 0.08}),
            ("beams_order1", {}, {"bEnabled": False}, {"bEnabled": True, "MinDrawOrder": 0, "MaxDrawOrder": 1, "bOnlyBeamsContainingReceiver": False, "Opacity": 0.03}),
        ])
        set_props(editor, MANAGER_REF + ".PathVisualizer", {"bEnabled": True})
        run_case(editor, "ImageSource", "ImageSource:", shots, failures, [
            ("spheres_paths", {}, {"bEnabled": True, "bOnlyValidForReceiver": False, "MaxDrawOrder": 2}, {"bEnabled": False}),
        ])
    finally:
        set_props(editor, MANAGER_REF, {"Solver": "RayTracer", "bRunOnBeginPlay": False})
        set_props(editor, MANAGER_REF + ".PathVisualizer", {"bEnabled": True})
        set_props(editor, MANAGER_REF + ".BeamVisualizer", {"bEnabled": True, "MinDrawOrder": 0, "MaxDrawOrder": 1, "bOnlyBeamsContainingReceiver": False, "Opacity": 0.22, "HighlightOpacity": 0.55})
        set_props(editor, MANAGER_REF + ".ImageSourceVisualizer", {"bEnabled": True, "bOnlyValidForReceiver": True, "MaxDrawOrder": 3})
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
