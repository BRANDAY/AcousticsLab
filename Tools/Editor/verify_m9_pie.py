"""M9 verification through the editor's MCP server (docs/ENV.md, "Unreal MCP"): PIE runs of the two industry levels and
their LogAcoustics lines (spatializer switch, runtime components, listener, plugin source, optional click measurement).

    python Tools/Editor/verify_m9_pie.py level Level_Amphitheatre_SteamAudio   # open the map in the editor
    python Tools/Editor/verify_m9_pie.py run [--measure] [--shot X.png]          # StartPIE, wait for the industry lines, keep PIE running
    python Tools/Editor/verify_m9_pie.py log [pattern]                           # last LogAcoustics lines matching the pattern
    python Tools/Editor/verify_m9_pie.py capture X.png                           # screenshot of the editor window
    python Tools/Editor/verify_m9_pie.py stop                                    # StopPIE

--measure turns UClickResponseRecorder.bMeasureOnBeginPlay on for the PIE copy (the editor actor is restored and not
saved afterwards). Keys 5 / 6 / 1 go to PIE through Tools/Editor/click_screen.ps1 (SendKeys), see docs/ENV.md.
"""
import os
import sys
import time

from verify_m5_pie import EDITOR_APP, Editor, capture, log_entries, set_props, wait_for

SCENE = "editor_toolset.toolsets.scene.SceneTools"
INDUSTRY_CLASS = "/Script/AcousticsLab.IndustryAudioActor"


def industry_ref(editor):
    found = editor.call(SCENE, "find_actors", name="", tag="", collision_channels=[], actor_type={"refPath": INDUSTRY_CLASS})
    refs = found if isinstance(found, list) else (found or {}).get("actors", [])
    for item in refs:
        path = item.get("refPath") if isinstance(item, dict) else str(item)
        if path and "PIE" not in path:
            return path
    raise RuntimeError(f"no IndustryAudioActor in the open level: {str(found)[:400]}")


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "log"
    editor = Editor()
    if command == "level":
        print(editor.call(SCENE, "load_level", level_path=f"/Game/Maps/{sys.argv[2]}"))
    elif command == "run":
        measure = "--measure" in sys.argv
        ref = industry_ref(editor)
        print("industry actor:", ref)
        if measure:
            set_props(editor, ref + ".ClickRecorder", {"bMeasureOnBeginPlay": True})
        baseline = len(log_entries(editor, "Industry"))
        click_baseline = len(log_entries(editor, "Click "))
        try:
            editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
            print("PIE started")
            lines = wait_for(editor, "Industry", 60, minimum=baseline + 4)
            time.sleep(5.0)
            for line in log_entries(editor, "Industry")[baseline:]:
                print("  ", line[:600])
            if not lines:
                print("FAILED: no industry lines within 60 s")
                return 1
            if measure:
                done = wait_for(editor, "Click ", 90, minimum=click_baseline + 5)
                for line in log_entries(editor, "Click ")[click_baseline:]:
                    print("  ", line[:800])
                if not done:
                    print("WARNING: click measurement did not finish within 90 s")
            if "--shot" in sys.argv:
                capture(os.path.abspath(sys.argv[sys.argv.index("--shot") + 1]))
        finally:
            if measure:
                set_props(editor, ref + ".ClickRecorder", {"bMeasureOnBeginPlay": False})
    elif command == "capture":
        capture(os.path.abspath(sys.argv[2]))
    elif command == "log":
        pattern = sys.argv[2] if len(sys.argv) > 2 else "Industry"
        for line in log_entries(editor, pattern)[-30:]:
            print(line[:1500])
    elif command == "stop":
        editor.call(EDITOR_APP, "StopPIE")
        print("PIE stopped")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
