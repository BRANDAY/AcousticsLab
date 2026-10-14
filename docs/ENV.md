# ENV.md — environment of this checkout (recorded at M0)

- **Engine**: Unreal Engine 5.8.3 (launcher build, CL 58210709, branch `++UE5+Release-5.8`), installed at `C:\Program Files\Epic Games\UE_5.8`.
- **Toolchain**: Visual Studio 2022 Community 17.14.41 (MSVC under `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC`), Windows SDK 10.0.22621.0.
- **OS / GPU**: Windows 11 Pro 10.0.26300, DX12.
- **Project**: `E:\FESB_Project\AcousticsLab`, public repo https://github.com/BRANDAY/AcousticsLab (branch `main`).
- **Build / tests**: the commands in `CLAUDE.md` work as written with this engine path. If an Unreal editor with Live Coding is running (even on another project or the project browser), `Build.bat` stops with "Unable to build while Live Coding is active"; add `-NoHotReloadFromIDE` to the build command — safe as long as this project is not open in that editor.
- **Memory**: with the editor open the machine has ~4 GB free, so UBT limits itself to 2 parallel compile actions; a full editor-target build takes ~2.5 min.
- **Placeholder maps**: `Tools/Editor/create_placeholder_levels.py`, run headless through `UnrealEditor-Cmd.exe -run=pythonscript` (the `PythonScriptPlugin` is enabled for the Editor target only for this reason).
- **Plugins enabled in M0**: GeometryScripting, ProceduralMeshComponent, EnhancedInput, AudioCapture, ModelingToolsEditorMode (editor), PythonScriptPlugin (editor).
- **TODO (M9)**: add `SteamAudio` and `AudioTracing` to the `Plugins` list of `AcousticsLab.uproject` (JSON allows no comments, so the reminder lives here).
