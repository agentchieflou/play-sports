"""Epic 135: client payload/response mapping, retry, and router fallback tests.

All HTTP is mocked by patching base.request_json (clients) or the router's
client factory - zero network access.
"""

import unittest
from unittest import mock

from tools.orchestrator.config import ModelSpec, OrchestratorConfig
from tools.orchestrator.models.base import (
    Message,
    ProviderError,
    ToolCall,
    ToolSpec,
    request_json,
)
from tools.orchestrator.models.gemini import GeminiClient
from tools.orchestrator.models.openrouter import OpenRouterClient
from tools.orchestrator.models.router import ModelRouter

ECHO_TOOL = ToolSpec(name="echo", description="echo back",
                     parameters={"type": "object",
                                 "properties": {"text": {"type": "string"}}})


class RequestJsonRetryTests(unittest.TestCase):
    def test_retries_on_429_then_succeeds(self):
        import io
        import urllib.error

        attempts = []

        def fake_urlopen(request, timeout=0):
            attempts.append(request.full_url)
            if len(attempts) == 1:
                raise urllib.error.HTTPError(
                    request.full_url, 429, "rate limited", {"Retry-After": "0"},
                    io.BytesIO(b"slow down"))
            return io.BytesIO(b'{"ok": true}')

        with mock.patch("urllib.request.urlopen", side_effect=fake_urlopen):
            result = request_json("https://x.test/api", {}, {}, sleep=lambda _s: None)
        self.assertEqual(result, {"ok": True})
        self.assertEqual(len(attempts), 2)

    def test_non_retryable_status_raises_provider_error(self):
        import io
        import urllib.error

        def fake_urlopen(request, timeout=0):
            raise urllib.error.HTTPError(request.full_url, 401, "unauthorized",
                                         {}, io.BytesIO(b"bad key"))

        with mock.patch("urllib.request.urlopen", side_effect=fake_urlopen):
            with self.assertRaises(ProviderError) as ctx:
                request_json("https://x.test/api", {}, {}, sleep=lambda _s: None)
        self.assertEqual(ctx.exception.status, 401)


class GeminiClientTests(unittest.TestCase):
    def test_payload_mapping(self):
        client = GeminiClient(model="gemini-3.5-flash", api_key="k", reasoning="high")
        captured = {}

        def fake_request(url, payload, headers, **_kwargs):
            captured.update(url=url, payload=payload, headers=headers)
            return {"candidates": [{"content": {"parts": [{"text": "hi"}]}}],
                    "usageMetadata": {"promptTokenCount": 5,
                                      "candidatesTokenCount": 2}}

        messages = [
            Message(role="system", content="be terse"),
            Message(role="user", content="hello"),
            Message(role="assistant", content="",
                    tool_calls=(ToolCall(id="c1", name="echo",
                                         arguments={"text": "x"}),)),
            Message(role="tool", name="echo", tool_call_id="c1", content="x"),
        ]
        with mock.patch("tools.orchestrator.models.gemini.request_json",
                        side_effect=fake_request):
            response = client.chat(messages, tools=[ECHO_TOOL])

        self.assertIn("gemini-3.5-flash:generateContent", captured["url"])
        self.assertEqual(captured["headers"]["x-goog-api-key"], "k")
        payload = captured["payload"]
        self.assertEqual(payload["systemInstruction"]["parts"][0]["text"], "be terse")
        self.assertEqual(payload["generationConfig"]["thinkingConfig"],
                         {"thinkingLevel": "high"})
        roles = [c["role"] for c in payload["contents"]]
        self.assertEqual(roles, ["user", "model", "user"])
        self.assertEqual(payload["contents"][1]["parts"][0]["functionCall"]["name"],
                         "echo")
        self.assertEqual(
            payload["tools"][0]["functionDeclarations"][0]["name"], "echo")
        self.assertEqual(response.text, "hi")
        self.assertEqual(client.usage.prompt_tokens, 5)

    def test_parses_function_call_response(self):
        client = GeminiClient(model="m", api_key="k")
        raw = {"candidates": [{"content": {"parts": [
            {"functionCall": {"name": "echo", "args": {"text": "y"}}}]}}]}
        with mock.patch("tools.orchestrator.models.gemini.request_json",
                        return_value=raw):
            response = client.chat([Message(role="user", content="go")])
        self.assertEqual(len(response.tool_calls), 1)
        self.assertEqual(response.tool_calls[0].name, "echo")
        self.assertEqual(response.tool_calls[0].arguments, {"text": "y"})


class OpenRouterClientTests(unittest.TestCase):
    def test_payload_and_tool_call_parsing(self):
        client = OpenRouterClient(model="openai/gpt-oss-120b", api_key="ork")
        captured = {}

        def fake_request(url, payload, headers, **_kwargs):
            captured.update(url=url, payload=payload, headers=headers)
            return {"choices": [{"message": {
                        "content": None,
                        "tool_calls": [{"id": "tc1", "type": "function",
                                        "function": {"name": "echo",
                                                     "arguments": '{"text": "z"}'}}],
                    }}],
                    "usage": {"prompt_tokens": 7, "completion_tokens": 3}}

        with mock.patch("tools.orchestrator.models.openrouter.request_json",
                        side_effect=fake_request):
            response = client.chat([Message(role="user", content="go")],
                                   tools=[ECHO_TOOL])

        self.assertEqual(captured["headers"]["Authorization"], "Bearer ork")
        self.assertEqual(captured["payload"]["model"], "openai/gpt-oss-120b")
        self.assertEqual(captured["payload"]["tools"][0]["function"]["name"], "echo")
        self.assertEqual(response.text, "")
        self.assertEqual(response.tool_calls[0].id, "tc1")
        self.assertEqual(response.tool_calls[0].arguments, {"text": "z"})
        self.assertEqual(client.usage.completion_tokens, 3)


class FakeClient:
    provider = "fake"

    def __init__(self, spec, fail=False):
        self.model = spec.model
        self.api_key = spec.api_key
        self.fail = fail
        self.calls = 0

    @property
    def label(self):
        return f"fake:{self.model}"

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.calls += 1
        if self.fail:
            raise ProviderError("boom", status=503)
        from tools.orchestrator.models.base import ChatResponse
        return ChatResponse(text=f"answer from {self.model}")


class RouterFallbackTests(unittest.TestCase):
    def make_router(self, fail_primary=True, primary_key="ok"):
        config = OrchestratorConfig(env={"GEMINI_API_KEY": "g",
                                         "OPENROUTER_API_KEY": primary_key})
        built = []

        def factory(spec: ModelSpec):
            client = FakeClient(spec, fail=(spec.provider == "openrouter"
                                            and fail_primary))
            built.append(client)
            return client

        return ModelRouter(config, client_factory=factory), built

    def test_falls_back_to_second_client(self):
        router, built = self.make_router(fail_primary=True)
        response = router.chat("worker", [Message(role="user", content="hi")])
        self.assertEqual(response.text, "answer from gemini-3.5-flash")
        self.assertEqual(built[0].calls, 1)  # primary was tried first

    def test_primary_used_when_healthy(self):
        router, _ = self.make_router(fail_primary=False)
        response = router.chat("worker", [Message(role="user", content="hi")])
        self.assertEqual(response.text, "answer from openai/gpt-oss-120b")

    def test_missing_key_skipped_without_call(self):
        router, built = self.make_router(fail_primary=False, primary_key="")
        response = router.chat("worker", [Message(role="user", content="hi")])
        self.assertEqual(response.text, "answer from gemini-3.5-flash")
        self.assertEqual(built[0].calls, 0)  # never attempted

    def test_all_failed_raises_with_details(self):
        config = OrchestratorConfig(env={})  # no keys at all
        router = ModelRouter(config,
                             client_factory=lambda spec: FakeClient(spec, fail=True))
        with self.assertRaises(ProviderError) as ctx:
            router.chat("worker", [Message(role="user", content="hi")])
        self.assertIn("no API key configured", str(ctx.exception))

    def test_unknown_tier(self):
        router, _ = self.make_router()
        with self.assertRaises(KeyError):
            router.clients("nonsense")


if __name__ == "__main__":
    unittest.main()
