"""M10 screenshots through the editor's MCP server (docs/ENV.md): Level_Amphitheatre in PIE with the cavea heatmap for
the conditions empty / audience / velarium (the manager runs 20k rays with the grid on BeginPlay).

    python Tools/Editor/verify_m10_pie.py shots [empty audience velarium ...]   # PIE per condition, docs/milestones/img/M10_<cond>.png
    python Tools/Editor/verify_m10_pie.py log [pattern]

The toggles are set on the editor actor before StartPIE and reset to empty at the end (the level is not saved).
"""
import os
import subprocess
import sys
import time

from verify_m5_pie import EDITOR_APP, Editor, capture, log_entries, set_props, wait_for

SCENE = "editor_toolset.toolsets.scene.SceneTools"
IMG = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "milestones", "img")
CONDITIONS = {"empty": (False, False), "audience": (True, False), "velarium": (False, True), "both": (True, True)}


def actor_ref(editor, class_path):
    found = editor.call(SCENE, "find_actors", name="", tag="", collision_channels=[], actor_type={"refPath": class_path})
    refs = found if isinstance(found, list) else (found or {}).get("actors", [])
    for item in refs:
        path = item.get("refPath") if isinstance(item, dict) else str(item)
        if path and "PIE" not in path:
            return path
    raise RuntimeError(f"no {class_path} in the open level: {str(found)[:400]}")


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "log"
    editor = Editor()
    if command == "shots":
        names = sys.argv[2:] or ["empty", "audience", "velarium"]
        ref = actor_ref(editor, "/Script/AcousticsLab.AmphitheatreActor")
        manager = actor_ref(editor, "/Script/AcousticsLab.AcousticSimulationManager")
        print("amphitheatre:", ref, "manager:", manager)
        rays = int(os.environ.get("M10_RAYS", "40000"))
        set_props(editor, manager, {"Settings": {"NumRays": rays}})
        try:
            for name in names:
                audience, velarium = CONDITIONS[name]
                set_props(editor, ref, {"bAudience": audience, "bVelarium": velarium})
                baseline = len(log_entries(editor, "Receiver grid Cavea"))
                editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
                print(name, "PIE started")
                ok = wait_for(editor, "Receiver grid Cavea", 240, minimum=baseline + 1)
                time.sleep(4.0)
                for line in log_entries(editor, "RayTracer|Receiver grid Cavea|Amphitheatre")[-4:]:
                    print("  ", line[:400])
                if not ok:
                    print("  WARNING: no heatmap line within 240 s")
                out = os.path.abspath(os.path.join(IMG, f"M10_heatmap_{name}.png"))
                for attempt in range(3):
                    if capture(out):
                        break
                    time.sleep(3.0)
                # Keep only the level viewport: the editor side panels and screen-edge notifications stay out.
                subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", os.path.join(os.path.dirname(__file__), "crop_png.ps1"),
                                "-Path", out, "-X", "8", "-Y", "137", "-Width", "1520", "-Height", "856"], check=False)
                editor.call(EDITOR_APP, "StopPIE")
                time.sleep(4.0)
        finally:
            set_props(editor, ref, {"bAudience": False, "bVelarium": False})
            set_props(editor, manager, {"Settings": {"NumRays": 20000}})
    elif command == "log":
        pattern = sys.argv[2] if len(sys.argv) > 2 else "Amphitheatre"
        for line in log_entries(editor, pattern)[-30:]:
            print(line[:1500])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
