"""Core 25's .env model router: the Ollama client, the free-tier "bridge" tier
and `delegate`. HTTP is mocked (request_json is patched) - no network."""

import io
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock

from tools.orchestrator.__main__ import main
from tools.orchestrator.config import (
    DEFAULT_BRIDGE_MODEL,
    DEFAULT_LOCAL_MODEL,
    ModelSpec,
    OrchestratorConfig,
)
from tools.orchestrator.delegate import delegate
from tools.orchestrator.models.base import ChatResponse, Message, ProviderError, ToolCall, ToolSpec
from tools.orchestrator.models.ollama import OllamaClient, normalize_host
from tools.orchestrator.models.router import ModelRouter, build_client

ECHO_TOOL = ToolSpec(name="echo", description="echo back",
                     parameters={"type": "object", "properties": {"text": {"type": "string"}}})


class OllamaClientTests(unittest.TestCase):
    def test_host_normalization(self):
        self.assertEqual(normalize_host("localhost:11434"), "http://localhost:11434")
        self.assertEqual(normalize_host("http://box:11434/"), "http://box:11434")
        self.assertEqual(normalize_host(""), "")

    def test_payload_and_response_mapping(self):
        client = OllamaClient(model="llama3.1", host="localhost:11434")
        captured = {}

        def fake_request(url, payload, headers, **kwargs):
            captured.update(url=url, payload=payload, headers=headers, kwargs=kwargs)
            return {"message": {"role": "assistant", "content": "done",
                                "tool_calls": [{"function": {"name": "echo", "arguments": {"text": "hi"}}},
                                               {"function": {"name": "echo", "arguments": "{\"text\": \"yo\"}"}}]},
                    "prompt_eval_count": 7, "eval_count": 3}

        messages = [
            Message(role="system", content="be terse"),
            Message(role="user", content="hello"),
            Message(role="assistant", content="", tool_calls=(ToolCall(id="c1", name="echo", arguments={"text": "x"}),)),
            Message(role="tool", name="echo", tool_call_id="c1", content="x"),
        ]
        with mock.patch("tools.orchestrator.models.ollama.request_json", side_effect=fake_request):
            response = client.chat(messages, tools=[ECHO_TOOL], temperature=0.1, max_tokens=64)

        self.assertEqual(captured["url"], "http://localhost:11434/api/chat")
        payload = captured["payload"]
        self.assertEqual(payload["model"], "llama3.1")
        self.assertFalse(payload["stream"])
        self.assertEqual(payload["options"], {"temperature": 0.1, "num_predict": 64})
        self.assertEqual([m["role"] for m in payload["messages"]], ["system", "user", "assistant", "tool"])
        self.assertEqual(payload["messages"][2]["tool_calls"][0]["function"],
                         {"name": "echo", "arguments": {"text": "x"}})
        self.assertEqual(payload["messages"][3]["tool_name"], "echo")
        self.assertEqual(payload["tools"][0]["function"]["name"], "echo")
        self.assertNotIn("Authorization", captured["headers"])

        self.assertEqual(response.text, "done")
        self.assertEqual([c.arguments for c in response.tool_calls], [{"text": "hi"}, {"text": "yo"}])
        self.assertEqual([c.id for c in response.tool_calls], ["call_0", "call_1"])
        self.assertEqual((response.usage.prompt_tokens, response.usage.completion_tokens), (7, 3))
        self.assertEqual(client.usage.prompt_tokens, 7)

    def test_built_from_a_spec(self):
        client = build_client(ModelSpec(provider="ollama", model="qwen", base_url="box:11434"))
        self.assertIsInstance(client, OllamaClient)
        self.assertEqual(client.host, "http://box:11434")
        self.assertTrue(client.api_key)  # the host counts as configured
        self.assertFalse(build_client(ModelSpec(provider="ollama", model="qwen")).api_key)


class BridgeTierTests(unittest.TestCase):
    def test_chain_order_and_defaults(self):
        config = OrchestratorConfig(env={"OLLAMA_HOST": "http://localhost:11434",
                                         "GEMINI_API_KEY": "g", "OPENROUTER_API_KEY": "o"})
        local, gemini, openrouter = config.tier_table()["bridge"]
        self.assertEqual((local.provider, local.model, local.base_url),
                         ("ollama", DEFAULT_LOCAL_MODEL, "http://localhost:11434"))
        self.assertEqual((gemini.provider, gemini.reasoning, gemini.api_key), ("gemini", "low", "g"))
        self.assertEqual((openrouter.provider, openrouter.model, openrouter.api_key),
                         ("openrouter", DEFAULT_BRIDGE_MODEL, "o"))
        self.assertTrue(all(spec.configured for spec in (local, gemini, openrouter)))

    def test_overrides_and_unconfigured_links(self):
        config = OrchestratorConfig(env={"ORCH_LOCAL_MODEL": "qwen2.5-coder", "ORCH_BRIDGE_MODEL": "x/y:free"})
        local, gemini, openrouter = config.bridge_specs()
        self.assertEqual(local.model, "qwen2.5-coder")
        self.assertEqual(openrouter.model, "x/y:free")
        self.assertFalse(local.configured or gemini.configured or openrouter.configured)

    def make_router(self, env, failing=()):
        calls = []

        class Fake:
            def __init__(self, spec):
                self.provider = spec.provider
                self.model = spec.model
                self.api_key = spec.api_key or spec.base_url
                self.label = f"{spec.provider}:{spec.model}"

            def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
                calls.append(self.provider)
                if self.provider in failing:
                    raise ProviderError("down", status=503)
                return ChatResponse(text=f"from {self.provider}")

        return ModelRouter(OrchestratorConfig(env=env), client_factory=Fake), calls

    def test_local_model_answers_first(self):
        router, calls = self.make_router({"OLLAMA_HOST": "localhost:11434", "GEMINI_API_KEY": "g"})
        response = router.chat("bridge", [Message(role="user", content="hi")])
        self.assertEqual(response.text, "from ollama")
        self.assertEqual(calls, ["ollama"])
        self.assertEqual(router.last_label, f"ollama:{DEFAULT_LOCAL_MODEL}")

    def test_falls_through_to_free_tiers(self):
        router, calls = self.make_router({"OLLAMA_HOST": "localhost:11434", "OPENROUTER_API_KEY": "o"},
                                         failing=("ollama",))
        response = router.chat("bridge", [Message(role="user", content="hi")])
        self.assertEqual(response.text, "from openrouter")
        self.assertEqual(calls, ["ollama", "openrouter"])  # gemini has no key: skipped, never called

    def test_nothing_configured_says_why(self):
        router, _ = self.make_router({})
        with self.assertRaises(ProviderError) as ctx:
            router.chat("bridge", [Message(role="user", content="hi")])
        self.assertIn("no host configured (OLLAMA_HOST)", str(ctx.exception))
        self.assertIn("no API key configured", str(ctx.exception))


class DelegateTests(unittest.TestCase):
    class Router:
        def __init__(self, fail=False):
            self.fail = fail
            self.seen = []
            self.last_label = ""

        def chat(self, tier, messages, tools=None, temperature=0.2, max_tokens=8192):
            self.seen.append((tier, messages, max_tokens))
            if self.fail:
                raise ProviderError("all clients failed for tier 'bridge'")
            self.last_label = "ollama:llama3.1"
            return ChatResponse(text="the answer")

    def test_delegates_to_the_bridge_tier(self):
        router = self.Router()
        result = delegate(router, "summarize this", system="be brief", max_tokens=100)
        self.assertTrue(result.ok)
        self.assertEqual((result.text, result.model), ("the answer", "ollama:llama3.1"))
        tier, messages, max_tokens = router.seen[0]
        self.assertEqual((tier, max_tokens), ("bridge", 100))
        self.assertEqual([m.role for m in messages], ["system", "user"])

    def test_failures_and_empty_prompts_come_back_as_results(self):
        self.assertFalse(delegate(self.Router(), "   ").ok)
        failed = delegate(self.Router(fail=True), "hi")
        self.assertFalse(failed.ok)
        self.assertIn("all clients failed", failed.error)

    def test_cli(self):
        router = self.Router()
        out, err = io.StringIO(), io.StringIO()
        with mock.patch("tools.orchestrator.models.router.ModelRouter", return_value=router), \
                redirect_stdout(out), redirect_stderr(err):
            code = main(["delegate", "--max-tokens", "50", "what is 11 personnel?"])
        self.assertEqual(code, 0)
        self.assertEqual(out.getvalue().strip(), "the answer")
        self.assertIn("answered by ollama:llama3.1", err.getvalue())
        self.assertEqual(router.seen[0][0], "bridge")

        failing = self.Router(fail=True)
        with mock.patch("tools.orchestrator.models.router.ModelRouter", return_value=failing), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(main(["delegate", "hi"]), 1)


if __name__ == "__main__":
    unittest.main()
