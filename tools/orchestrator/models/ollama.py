"""Ollama client (local models at OLLAMA_HOST): the zero-cost, offline head of
the bridge tier (Core 25's .env model router). Stdlib HTTP only, like the
other clients."""

from __future__ import annotations

import json

from .base import ChatResponse, Message, ModelClient, ToolCall, ToolSpec, Usage, request_json


def normalize_host(host: str) -> str:
    """http://localhost:11434 from "localhost:11434" or "http://localhost:11434/"."""
    host = host.strip().rstrip("/")
    if host and "://" not in host:
        host = f"http://{host}"
    return host


class OllamaClient(ModelClient):
    provider = "ollama"

    def __init__(self, model: str, host: str):
        # Ollama needs no key; the configured host stands in for one, so the
        # router's "is this client configured?" check (api_key) works unchanged.
        super().__init__(model=model, api_key=normalize_host(host))
        self.host = normalize_host(host)

    def chat(self, messages: list[Message], tools: list[ToolSpec] | None = None,
             temperature: float = 0.2, max_tokens: int = 8192) -> ChatResponse:
        payload = self._build_payload(messages, tools, temperature, max_tokens)
        # A local model can be slow to load; one retry is enough for a local server.
        raw = request_json(f"{self.host}/api/chat", payload, headers={}, retries=1, timeout_s=600)
        return self._parse_response(raw)

    def _build_payload(self, messages: list[Message], tools: list[ToolSpec] | None,
                       temperature: float, max_tokens: int) -> dict:
        wire_messages: list[dict] = []
        for message in messages:
            entry: dict = {"role": message.role, "content": message.content}
            if message.role == "assistant" and message.tool_calls:
                entry["tool_calls"] = [{"function": {"name": call.name, "arguments": call.arguments}}
                                       for call in message.tool_calls]
            if message.role == "tool" and message.name:
                entry["tool_name"] = message.name
            wire_messages.append(entry)
        payload: dict = {
            "model": self.model,
            "messages": wire_messages,
            "stream": False,
            "options": {"temperature": temperature, "num_predict": max_tokens},
        }
        if tools:
            payload["tools"] = [{
                "type": "function",
                "function": {"name": t.name, "description": t.description, "parameters": t.parameters},
            } for t in tools]
        return payload

    def _parse_response(self, raw: dict) -> ChatResponse:
        message = raw.get("message") or {}
        tool_calls: list[ToolCall] = []
        for index, call in enumerate(message.get("tool_calls") or []):
            function = call.get("function") or {}
            arguments = function.get("arguments") or {}
            if isinstance(arguments, str):
                try:
                    arguments = json.loads(arguments)
                except json.JSONDecodeError:
                    arguments = {"_raw": arguments}
            tool_calls.append(ToolCall(id=call.get("id") or f"call_{index}",
                                       name=function.get("name", ""), arguments=arguments))
        usage = Usage(prompt_tokens=raw.get("prompt_eval_count", 0) or 0,
                      completion_tokens=raw.get("eval_count", 0) or 0)
        self.usage.add(usage)
        return ChatResponse(text=message.get("content") or "", tool_calls=tool_calls,
                            usage=usage, raw=raw)
