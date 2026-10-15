"""M4 verification through the editor's MCP server (see docs/ENV.md, "Unreal MCP"): a PIE run on the open Level_Shoebox,
the auralization log lines (IR synthesis, WAV export, IR assignment, A/B routing, receiver swap) and the exported WAVs.

    python Tools/Editor/verify_m4_pie.py            # editor running with -ModelContextProtocolStartServer -ModelContextProtocolPort=8000

Steps: SimulationManager.bRunOnBeginPlay = true -> StartPIE -> wait for "impulse responses for" in LogAcoustics -> console
commands Acoustics.Auralization.Variant 2, .Receiver R3, .Receiver R1, .Variant 1 -> read LogAcoustics -> StopPIE ->
bRunOnBeginPlay = false, save. Then Saved/IR/*.wav are parsed (RIFF by hand: the standard wave module rejects float files)
and printed as a table. Exit code 1 when a check fails.

Limit seen in 5.8.3: no editor toolset executes a console command, so the A/B and receiver steps are skipped with a note
("no console tool"); they were verified by hand at M4 with real mouse / keyboard input (user32 SetCursorPos + mouse_event and
SendKeys): the SlateInspectorToolset Click gives a UMG button focus but does not fire OnClicked, and its PressKey returns False.
"""
import glob
import json
import os
import re
import struct
import sys
import time

from mcp_client import McpClient, text_of

EDITOR_APP = "EditorToolset.EditorAppToolset"
LOGS = "EditorToolset.LogsToolset"
OBJECT = "editor_toolset.toolsets.object.ObjectTools"
ASSET = "editor_toolset.toolsets.asset.AssetTools"
MANAGER_REF = "/Game/Maps/Level_Shoebox.Level_Shoebox:PersistentLevel.AcousticSimulationManager_0"
PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IR_DIR = os.path.join(PROJECT_DIR, "Saved", "IR")


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

    def describe(self, toolset):
        result = self.client.call("describe_toolset", {"toolset_name": toolset})
        text = text_of(result)
        return json.loads(text)

    @staticmethod
    def ref(path):
        return {"refPath": path}


def find_console_tool(editor):
    """The EditorAppToolset tool that executes a console command, with the name of its string argument."""
    description = editor.describe(EDITOR_APP)
    for tool in description.get("tools", []):
        name = tool["name"].split(".")[-1]
        if re.search(r"console", name, re.I) and re.search(r"command|exec", name, re.I) and not re.search(r"variable|get|list|find", name, re.I):
            properties = tool.get("inputSchema", {}).get("properties", {})
            for prop, schema in properties.items():
                if schema.get("type") == "string":
                    return name, prop
    return None, None


def log_entries(editor, pattern, max_entries=200):
    entries = editor.call(LOGS, "GetLogEntries", category="LogAcoustics", pattern=pattern, maxEntries=max_entries) or []
    lines = []
    for entry in entries:
        if isinstance(entry, dict):
            lines.append(entry.get("message") or entry.get("Message") or json.dumps(entry, ensure_ascii=False))
        else:
            lines.append(str(entry))
    return lines


def wait_for(editor, pattern, timeout_seconds):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        lines = log_entries(editor, pattern)
        if lines:
            return lines
        time.sleep(2.0)
    return []


def wav_summary(path):
    """RIFF parsed by hand: the standard wave module rejects float files (format tag 3)."""
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path} is not a RIFF/WAVE file")
    pos, fmt, pcm = 12, None, b""
    while pos + 8 <= len(data):
        tag, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if tag == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif tag == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    format_tag, channels, rate, _, _, bits = fmt
    if format_tag == 3 and bits == 32:
        values = struct.unpack("<%df" % (len(pcm) // 4), pcm)
    elif format_tag == 1 and bits == 16:
        values = [v / 32768.0 for v in struct.unpack("<%dh" % (len(pcm) // 2), pcm)]
    else:
        values = []
    frames = len(values) // max(1, channels)
    peak = max((abs(v) for v in values), default=0.0)
    return channels, bits, rate, frames, frames / rate if rate else 0.0, peak


def main():
    editor = Editor()
    failures = []
    console_tool, console_arg = find_console_tool(editor)
    print("console tool:", console_tool, console_arg)

    def console(command):
        if not console_tool:
            print("no console tool, skipping:", command)
            return
        try:
            editor.call(EDITOR_APP, console_tool, **{console_arg: command})
            print("console:", command)
        except Exception as error:  # noqa: BLE001
            print("console failed:", command, error)
            failures.append(f"console {command}: {error}")

    editor.call(OBJECT, "set_properties", instance=Editor.ref(MANAGER_REF), values=json.dumps({"bRunOnBeginPlay": True}))
    try:
        editor.call(EDITOR_APP, "StartPIE", options={"playMode": "PlayMode_InViewPort"})
        print("PIE started")
        lines = wait_for(editor, "impulse responses for", 120)
        if not lines:
            failures.append("no IR synthesis log within 120 s")
        else:
            print(lines[-1])
        time.sleep(2.0)
        console("Acoustics.Auralization.Variant 2")
        time.sleep(1.0)
        console("Acoustics.Auralization.Receiver R3")
        time.sleep(1.0)
        console("Acoustics.Auralization.Receiver R1")
        time.sleep(1.0)
        console("Acoustics.Auralization.Variant 1")
        time.sleep(1.5)
        print("--- LogAcoustics: Auralization ---")
        for line in log_entries(editor, "Auralization"):
            print(line)
        print("--- LogAcoustics: variant ---")
        for line in log_entries(editor, "variant"):
            print(line)
    finally:
        editor.call(EDITOR_APP, "StopPIE")
        print("PIE stopped")
        editor.call(OBJECT, "set_properties", instance=Editor.ref(MANAGER_REF), values=json.dumps({"bRunOnBeginPlay": False}))
        print("save:", editor.call(ASSET, "save_assets", asset_paths=[]))

    print("--- Saved/IR ---")
    files = sorted(glob.glob(os.path.join(IR_DIR, "*.wav")))
    if not files:
        failures.append("no WAV files in Saved/IR")
    for path in files:
        channels, bits, rate, frames, seconds, peak = wav_summary(path)
        peak_db = 20.0 * (0 if peak <= 0 else __import__("math").log10(peak))
        print(f"{os.path.basename(path)}: {channels} ch, {bits} bit, {rate} Hz, {frames} samples = {seconds:.3f} s, peak {peak_db:.2f} dBFS")
        if rate != 48000 or channels != 1:
            failures.append(f"{path}: unexpected layout")
        if abs(peak_db + 3.0) > 0.05:
            failures.append(f"{path}: peak {peak_db:.2f} dBFS, expected -3")
    if failures:
        print("FAILURES:")
        for failure in failures:
            print(" -", failure)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
