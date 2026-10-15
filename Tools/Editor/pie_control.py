"""Start / stop PIE in the open level through the editor's MCP server (docs/ENV.md).

    python Tools/Editor/pie_control.py start|stop|status
"""
import json
import sys

from mcp_client import McpClient, text_of

EDITOR_APP = "EditorToolset.EditorAppToolset"


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "status"
    client = McpClient()
    client.initialize()
    tool, arguments = {
        "start": ("StartPIE", {"options": {"playMode": "PlayMode_InViewPort"}}),
        "stop": ("StopPIE", {}),
        "status": ("IsPIERunning", {}),
    }[command]
    result = client.call("call_tool", {"toolset_name": EDITOR_APP, "tool_name": tool, "arguments": arguments})
    print(command, "->", text_of(result)[:300], "(error)" if result.get("isError") else "")
    return 1 if result.get("isError") else 0


if __name__ == "__main__":
    sys.exit(main())
