"""Gemini client (generateContent REST API) - supervisor tier and the worker
fallback slot. Reasoning level ("high"/"low") maps to the thinking config."""

from __future__ import annotations

import json

from .base import ChatResponse, Message, ModelClient, ToolCall, ToolSpec, Usage, request_json

API_BASE = "https://generativelanguage.googleapis.com/v1beta/models"


class GeminiClient(ModelClient):
    provider = "gemini"

    def __init__(self, model: str, api_key: str, reasoning: str | None = None):
        super().__init__(model=model, api_key=api_key)
        self.reasoning = reasoning

    @property
    def label(self) -> str:
        suffix = f" ({self.reasoning})" if self.reasoning else ""
        return f"{self.provider}:{self.model}{suffix}"

    def chat(self, messages: list[Message], tools: list[ToolSpec] | None = None,
             temperature: float = 0.2, max_tokens: int = 8192) -> ChatResponse:
        payload = self._build_payload(messages, tools, temperature, max_tokens)
        url = f"{API_BASE}/{self.model}:generateContent"
        raw = request_json(url, payload, headers={"x-goog-api-key": self.api_key})
        return self._parse_response(raw)

    # -- request mapping ----------------------------------------------------

    def _build_payload(self, messages: list[Message],
                       tools: list[ToolSpec] | None,
                       temperature: float, max_tokens: int) -> dict:
        system_texts = [m.content for m in messages if m.role == "system"]
        contents: list[dict] = []
        for message in messages:
            if message.role == "system":
                continue
            if message.role == "assistant":
                parts: list[dict] = []
                if message.content:
                    parts.append({"text": message.content})
                for call in message.tool_calls:
                    parts.append({"functionCall": {"name": call.name,
                                                   "args": call.arguments}})
                contents.append({"role": "model", "parts": parts or [{"text": ""}]})
            elif message.role == "tool":
                contents.append({
                    "role": "user",
                    "parts": [{"functionResponse": {
                        "name": message.name,
                        "response": {"result": message.content},
                    }}],
                })
            else:  # user
                contents.append({"role": "user", "parts": [{"text": message.content}]})

        generation_config: dict = {"temperature": temperature,
                                   "maxOutputTokens": max_tokens}
        if self.reasoning:
            generation_config["thinkingConfig"] = {"thinkingLevel": self.reasoning}

        payload: dict = {"contents": contents, "generationConfig": generation_config}
        if system_texts:
            payload["systemInstruction"] = {"parts": [{"text": "\n\n".join(system_texts)}]}
        if tools:
            payload["tools"] = [{"functionDeclarations": [
                {"name": t.name, "description": t.description, "parameters": t.parameters}
                for t in tools
            ]}]
        return payload

    # -- response mapping ---------------------------------------------------

    def _parse_response(self, raw: dict) -> ChatResponse:
        text_parts: list[str] = []
        tool_calls: list[ToolCall] = []
        candidates = raw.get("candidates") or []
        if candidates:
            for index, part in enumerate((candidates[0].get("content") or {}).get("parts") or []):
                if "text" in part:
                    text_parts.append(part["text"])
                elif "functionCall" in part:
                    call = part["functionCall"]
                    args = call.get("args") or {}
                    if isinstance(args, str):  # defensive: some models stringify
                        try:
                            args = json.loads(args)
                        except json.JSONDecodeError:
                            args = {"_raw": args}
                    tool_calls.append(ToolCall(id=f"call_{index}",
                                               name=call.get("name", ""),
                                               arguments=args))
        meta = raw.get("usageMetadata") or {}
        usage = Usage(prompt_tokens=meta.get("promptTokenCount", 0),
                      completion_tokens=meta.get("candidatesTokenCount", 0))
        self.usage.add(usage)
        return ChatResponse(text="".join(text_parts), tool_calls=tool_calls,
                            usage=usage, raw=raw)
