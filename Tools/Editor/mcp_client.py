"""Minimal JSON-RPC client for the editor's Model Context Protocol server (Streamable HTTP, UE 5.8).

Use it when the IDE's unreal-mcp connector is not connected (it only connects at session start),
e.g. after relaunching the editor:

    UnrealEditor.exe AcousticsLab.uproject /Game/Maps/Level_Shoebox -ModelContextProtocolStartServer -ModelContextProtocolPort=8000

Examples:
    python Tools/Editor/mcp_client.py list
    python Tools/Editor/mcp_client.py describe editor_toolset.toolsets.scene.SceneTools
    python Tools/Editor/mcp_client.py call <tool_name> '{"arg": "value"}'

The server exposes `list_toolsets`, `describe_toolset` and `call_tool` (tool search mode), see docs/ENV.md.
"""
import json
import sys
import urllib.error
import urllib.request

URL = "http://127.0.0.1:8000/mcp"


class McpClient:
    def __init__(self, url=URL):
        self.url = url
        self.session_id = None
        self.next_id = 1

    def _post(self, payload, expect_response=True):
        data = json.dumps(payload).encode("utf-8")
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        request = urllib.request.Request(self.url, data=data, headers=headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=600) as response:
                session = response.headers.get("Mcp-Session-Id")
                if session:
                    self.session_id = session
                body = response.read().decode("utf-8", errors="replace")
                content_type = response.headers.get("Content-Type", "")
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {error.code}: {body[:2000]}")
        if not expect_response or not body.strip():
            return None
        if "text/event-stream" in content_type:
            messages = []
            for line in body.splitlines():
                if line.startswith("data:"):
                    chunk = line[5:].strip()
                    if chunk:
                        messages.append(json.loads(chunk))
            # The response to our request is the last message carrying a result or an error.
            for message in reversed(messages):
                if "result" in message or "error" in message:
                    return message
            return messages[-1] if messages else None
        return json.loads(body)

    def request(self, method, params=None):
        payload = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            payload["params"] = params
        response = self._post(payload)
        if response is None:
            return None
        if "error" in response:
            raise RuntimeError(json.dumps(response["error"], ensure_ascii=False))
        return response.get("result")

    def notify(self, method, params=None):
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        self._post(payload, expect_response=False)

    def initialize(self):
        result = self.request("initialize", {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "acousticslab-mcp-client", "version": "0.1"},
        })
        self.notify("notifications/initialized")
        return result

    def list_tools(self):
        return self.request("tools/list", {})

    def call(self, name, arguments=None):
        result = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        return result


def text_of(result):
    """Concatenated text content of a tools/call result."""
    if not result:
        return ""
    parts = []
    for item in result.get("content", []):
        if item.get("type") == "text":
            parts.append(item.get("text", ""))
    return "\n".join(parts)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    client = McpClient()
    client.initialize()
    command = argv[1]
    if command == "list":
        tools = client.list_tools()
        for tool in tools.get("tools", []):
            print(tool["name"], "-", (tool.get("description") or "").splitlines()[0][:120])
        return 0
    if command == "toolsets":
        print(text_of(client.call("list_toolsets", {})))
        return 0
    if command == "describe":
        print(text_of(client.call("describe_toolset", {"toolset_name": argv[2]})))
        return 0
    if command == "call":
        arguments = json.loads(argv[3]) if len(argv) > 3 else {}
        result = client.call(argv[2], arguments)
        print(json.dumps(result, ensure_ascii=False, indent=1))
        return 0
    print("unknown command", command)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
