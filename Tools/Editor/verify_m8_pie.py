"""M8 verification through the editor's MCP server (docs/ENV.md, "Unreal MCP"): a hybrid run of Level_Shoebox in PIE,
its LogAcoustics lines (hybrid summary, compare-panel table) and screenshots of the editor window.

    python Tools/Editor/verify_m8_pie.py prepare            # Solver = Hybrid, bRunOnBeginPlay on (editor actor of the open Level_Shoebox)
    python Tools/Editor/verify_m8_pie.py run [--shot X.png]  # StartPIE, wait for the hybrid summary, optional screenshot, keep PIE running
    python Tools/Editor/verify_m8_pie.py capture X.png       # screenshot of the editor window (PIE included)
    python Tools/Editor/verify_m8_pie.py log [pattern]       # last LogAcoustics lines matching the pattern
    python Tools/Editor/verify_m8_pie.py stop                # StopPIE
    python Tools/Editor/verify_m8_pie.py restore            # Solver = RayTracer, bRunOnBeginPlay off, save

The comparison panel opens in PIE with key H (or from the main menu, items 1.4 / 3). Keys and clicks must come from the
PowerShell tool (Tools/Editor/click_screen.ps1, SendKeys), see docs/ENV.md "Recorded at M7".
"""
import os
import sys
import time

from verify_m5_pie import ASSET, EDITOR_APP, MANAGER_REF, Editor, capture, log_entries, set_props, wait_for


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "log"
    editor = Editor()
    if command == "prepare":
        set_props(editor, MANAGER_REF, {"Solver": "Hybrid", "bRunOnBeginPlay": True})
        print("prepared")
    elif command == "run":
        baseline = len(log_entries(editor, "Hybrid K="))
        editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
        print("PIE started")
        lines = wait_for(editor, "Hybrid K=", 240, minimum=baseline + 1)
        if not lines:
            print("FAILED: no hybrid summary within 240 s")
            return 1
        print(lines[-1][:600])
        time.sleep(4.0)
        if "--shot" in sys.argv:
            capture(os.path.abspath(sys.argv[sys.argv.index("--shot") + 1]))
    elif command == "capture":
        capture(os.path.abspath(sys.argv[2]))
    elif command == "log":
        pattern = sys.argv[2] if len(sys.argv) > 2 else "Hybrid"
        for line in log_entries(editor, pattern)[-20:]:
            print(line[:1500])
    elif command == "stop":
        editor.call(EDITOR_APP, "StopPIE")
        print("PIE stopped")
    elif command == "restore":
        set_props(editor, MANAGER_REF, {"Solver": "RayTracer", "bRunOnBeginPlay": False})
        print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
