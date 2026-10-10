"""The model router service's MCP surface (Epic 119).

`python -m tools.orchestrator mcp` serves RouterService over the Model Context Protocol's stdio
transport: JSON-RPC 2.0, one message per line on stdin and stdout (logs go to stderr). Stdlib
only. Tools:

  route_task    run a task (narration, summary, analysis, strategy, delegate) through its
                routed chain and return the answer and the model that gave it
  list_routes   every task, what it is for, who consumes it and its chain, with each model's
                state (no network)
  router_health each model's configuration, cooldown and request budget; live=true also makes
                one minimal call per configured model (cached)

Registering it (AGENTS.md, "MCP servers"): Claude Code's .mcp.json and VS Code's .vscode/mcp.json
run it as a stdio command from the repo root; Antigravity's global config takes the same
command. The keys come from the repo's .env, as for every orchestrator command.
"""

from __future__ import annotations

import json
import sys

from .service import RouterService

SERVER_NAME = "play-sports-model-router"
SERVER_VERSION = "1.0.0"
SUPPORTED_PROTOCOLS = ("2025-06-18", "2025-03-26", "2024-11-05")

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602


def tool_definitions(service: RouterService) -> list[dict]:
    return [
        {
            "name": "route_task",
            "description": "Run a task through the free-tier model router: it picks the model by "
                           "the task's capability and cost, and falls back across providers.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "task": {"type": "string", "enum": service.tasks(),
                             "description": "What kind of work this is; list_routes says what each is for."},
                    "prompt": {"type": "string", "description": "The request."},
                    "system": {"type": "string", "description": "Optional system instructions."},
                    "max_tokens": {"type": "integer", "minimum": 1,
                                   "description": "Optional cap on the answer; the task's default otherwise."},
                },
                "required": ["task", "prompt"],
            },
        },
        {
            "name": "list_routes",
            "description": "Every task the router knows, what it is for, who uses it, and the models "
                           "it tries in order, with whether each is configured and available now.",
            "inputSchema": {"type": "object", "properties": {}},
        },
        {
            "name": "router_health",
            "description": "Each model's configuration, cooldown and request budget. live=true also "
                           "makes one minimal call per configured model (cached for a few minutes).",
            "inputSchema": {"type": "object", "properties": {"live": {"type": "boolean"}}},
        },
    ]


class McpServer:
    def __init__(self, service: RouterService):
        self.service = service
        self.protocol = SUPPORTED_PROTOCOLS[0]

    # --- JSON-RPC ----------------------------------------------------------------------------

    def handle_line(self, line: str) -> str | None:
        """One incoming line -> the response line, or None for a notification."""
        line = line.strip()
        if not line:
            return None
        try:
            message = json.loads(line)
        except json.JSONDecodeError as error:
            return self._error(None, PARSE_ERROR, f"parse error: {error}")
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
            return self._error(message.get("id") if isinstance(message, dict) else None, INVALID_REQUEST, "invalid request")
        is_notification = "id" not in message
        try:
            result = self.dispatch(message["method"], message.get("params") or {})
        except LookupError as error:
            return None if is_notification else self._error(message["id"], METHOD_NOT_FOUND, str(error))
        except ValueError as error:
            return None if is_notification else self._error(message["id"], INVALID_PARAMS, str(error))
        if is_notification:
            return None
        return json.dumps({"jsonrpc": "2.0", "id": message["id"], "result": result})

    def dispatch(self, method: str, params: dict) -> dict:
        if method == "initialize":
            requested = params.get("protocolVersion")
            self.protocol = requested if requested in SUPPORTED_PROTOCOLS else SUPPORTED_PROTOCOLS[0]
            return {
                "protocolVersion": self.protocol,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "instructions": "Route model work by task: call list_routes to see the tasks, then "
                                "route_task. Models come from the repo's .env free-tier keys.",
            }
        if method.startswith("notifications/"):
            return {}
        if method == "ping":
            return {}
        if method == "tools/list":
            return {"tools": tool_definitions(self.service)}
        if method == "tools/call":
            name = params.get("name")
            arguments = params.get("arguments") or {}
            if not isinstance(arguments, dict):
                raise ValueError("arguments must be an object")
            return self.call_tool(name, arguments)
        raise LookupError(f"method not found: {method}")

    # --- tools -------------------------------------------------------------------------------

    def call_tool(self, name: str, arguments: dict) -> dict:
        if name == "route_task":
            task = arguments.get("task")
            prompt = arguments.get("prompt")
            if not isinstance(task, str) or not isinstance(prompt, str):
                return self._tool_error("route_task needs a task and a prompt (strings)")
            max_tokens = arguments.get("max_tokens")
            if max_tokens is not None and (not isinstance(max_tokens, int) or isinstance(max_tokens, bool) or max_tokens < 1):
                return self._tool_error("max_tokens must be a whole number, 1 or more")
            result = self.service.complete(task, prompt=prompt, system=str(arguments.get("system") or ""),
                                           max_tokens=max_tokens)
            if not result.ok:
                return self._tool_error(f"{task}: {result.error}")
            structured = {"task": task, "model": result.model, "text": result.text,
                          "waited_seconds": result.waited_seconds}
            return {"content": [{"type": "text", "text": result.text}], "structuredContent": structured,
                    "isError": False}
        if name == "list_routes":
            routes = self.service.describe_routes()
            return {"content": [{"type": "text", "text": json.dumps(routes, indent=2)}],
                    "structuredContent": {"routes": routes}, "isError": False}
        if name == "router_health":
            models = self.service.health(live=bool(arguments.get("live", False)))
            return {"content": [{"type": "text", "text": json.dumps(models, indent=2)}],
                    "structuredContent": {"models": models}, "isError": False}
        return self._tool_error(f"unknown tool: {name!r}")

    @staticmethod
    def _tool_error(text: str) -> dict:
        return {"content": [{"type": "text", "text": text}], "isError": True}

    @staticmethod
    def _error(request_id, code: int, text: str) -> str:
        return json.dumps({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": text}})

    # --- stdio -------------------------------------------------------------------------------

    def serve(self, stdin=None, stdout=None) -> int:
        """Answers each line of stdin on stdout until stdin closes."""
        stdin = stdin or sys.stdin
        stdout = stdout or sys.stdout
        print(f"{SERVER_NAME}: serving {len(self.service.tasks())} tasks on stdio", file=sys.stderr)
        for line in stdin:
            reply = self.handle_line(line)
            if reply is not None:
                stdout.write(reply + "\n")
                stdout.flush()
        return 0
