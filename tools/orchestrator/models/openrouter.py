"""OpenRouter client (OpenAI-compatible chat completions) - primary worker
tier (GPT-OSS-120B per the Track P tier table)."""

from __future__ import annotations

import json

from .base import ChatResponse, Message, ModelClient, ToolCall, ToolSpec, Usage, request_json

API_URL = "https://openrouter.ai/api/v1/chat/completions"


class OpenRouterClient(ModelClient):
    provider = "openrouter"

    def chat(self, messages: list[Message], tools: list[ToolSpec] | None = None,
             temperature: float = 0.2, max_tokens: int = 8192) -> ChatResponse:
        payload = self._build_payload(messages, tools, temperature, max_tokens)
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            # OpenRouter attribution headers (optional but polite)
            "HTTP-Referer": "https://github.com/agentchieflou/play-sports",
            "X-Title": "play-sports orchestrator",
        }
        raw = request_json(API_URL, payload, headers=headers)
        return self._parse_response(raw)

    # -- request mapping ----------------------------------------------------

    def _build_payload(self, messages: list[Message],
                       tools: list[ToolSpec] | None,
                       temperature: float, max_tokens: int) -> dict:
        wire_messages: list[dict] = []
        for message in messages:
            entry: dict = {"role": message.role, "content": message.content}
            if message.role == "assistant" and message.tool_calls:
                entry["tool_calls"] = [{
                    "id": call.id,
                    "type": "function",
                    "function": {"name": call.name,
                                 "arguments": json.dumps(call.arguments)},
                } for call in message.tool_calls]
            if message.role == "tool":
                entry["tool_call_id"] = message.tool_call_id
                entry["name"] = message.name
            wire_messages.append(entry)

        payload: dict = {
            "model": self.model,
            "messages": wire_messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
        }
        if tools:
            payload["tools"] = [{
                "type": "function",
                "function": {"name": t.name, "description": t.description,
                             "parameters": t.parameters},
            } for t in tools]
        return payload

    # -- response mapping ---------------------------------------------------

    def _parse_response(self, raw: dict) -> ChatResponse:
        choices = raw.get("choices") or []
        message = (choices[0].get("message") or {}) if choices else {}
        tool_calls: list[ToolCall] = []
        for index, call in enumerate(message.get("tool_calls") or []):
            function = call.get("function") or {}
            arguments_raw = function.get("arguments") or "{}"
            try:
                arguments = json.loads(arguments_raw)
            except json.JSONDecodeError:
                arguments = {"_raw": arguments_raw}
            tool_calls.append(ToolCall(id=call.get("id", f"call_{index}"),
                                       name=function.get("name", ""),
                                       arguments=arguments))
        raw_usage = raw.get("usage") or {}
        usage = Usage(prompt_tokens=raw_usage.get("prompt_tokens", 0),
                      completion_tokens=raw_usage.get("completion_tokens", 0))
        self.usage.add(usage)
        return ChatResponse(text=message.get("content") or "",
                            tool_calls=tool_calls, usage=usage, raw=raw)
