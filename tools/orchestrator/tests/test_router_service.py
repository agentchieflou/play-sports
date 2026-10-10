"""Epic 119: the routing table, the model router service and its MCP surface.

No network: clients come from a fake factory, and the clock and sleep are fakes.
"""

import io
import json
import unittest
from contextlib import redirect_stdout

from tools.orchestrator.__main__ import main
from tools.orchestrator.config import OrchestratorConfig
from tools.orchestrator.mcp_server import McpServer, SUPPORTED_PROTOCOLS
from tools.orchestrator.models.base import ChatResponse, Message, ProviderError
from tools.orchestrator.routing import RoutingTable
from tools.orchestrator.service import RouterService

ALL_KEYS = {"OLLAMA_HOST": "http://localhost:11434", "GEMINI_API_KEY": "g", "OPENROUTER_API_KEY": "o"}


class FakeClient:
    """A ModelClient stand-in: answers from a script of replies (text, or a ProviderError)."""

    def __init__(self, spec, script):
        self.provider = spec.provider
        self.model = spec.model
        self.api_key = spec.api_key or spec.base_url
        self.reasoning = spec.reasoning
        self.label = spec.label
        self._script = script
        self.calls = 0
        self.health_checks = 0

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.calls += 1
        replies = self._script.get(self.label) or [f"answer from {self.label}"]
        reply = replies.pop(0) if len(replies) > 1 else replies[0]
        if isinstance(reply, ProviderError):
            raise reply
        return ChatResponse(text=reply)

    def healthy(self):
        self.health_checks += 1
        return bool(self.api_key)


class FakeClock:
    def __init__(self):
        self.now = 1000.0
        self.slept = []

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.slept.append(seconds)
        self.now += seconds


def make_service(env=None, script=None, table=None):
    clock = FakeClock()
    built = []
    script = script if script is not None else {}

    def factory(spec):
        client = FakeClient(spec, script)
        built.append(client)
        return client

    service = RouterService(OrchestratorConfig(env=dict(ALL_KEYS if env is None else env)), table=table,
                            client_factory=factory, clock=clock, sleep=clock.sleep)
    return service, clock, built


class RoutingTableTests(unittest.TestCase):
    def test_the_authored_table_is_sound(self):
        table = RoutingTable.load()
        config = OrchestratorConfig(env={})
        self.assertEqual(table.validate(set(config.named_specs())), [])
        for task in ("narration", "summary", "analysis", "strategy", "delegate"):
            self.assertIn(task, table.tasks)
        self.assertTrue(any("Epic 96" in consumer for consumer in table.tasks["narration"].consumers))
        self.assertTrue(any("Epic 82" in consumer for consumer in table.tasks["strategy"].consumers))

    def test_cheap_tasks_go_local_first_and_strategy_goes_to_the_best(self):
        table = RoutingTable.load()
        self.assertEqual(table.chain_names("narration")[0], "local")
        self.assertEqual(table.chain_names("strategy"), ["supervisor", "worker"])
        self.assertNotIn("local", table.chain_names("summary"))
        # Cheapest first, the more capable first among equals.
        names = table.chain_names("narration")
        costs = [table.models[name].cost for name in names]
        self.assertEqual(costs, sorted(costs))

    def test_chains_are_config_specs(self):
        table = RoutingTable.load()
        config = OrchestratorConfig(env=ALL_KEYS)
        tiers = table.tiers(config)
        self.assertEqual([spec.provider for spec in tiers["strategy"]], ["gemini", "openrouter"])
        self.assertEqual(tiers["strategy"][0].reasoning, "high")
        self.assertEqual(tiers["narration"][0].base_url, "http://localhost:11434")
        # One Gemini model under two names: one budget, the lower limit.
        self.assertEqual(table.rpm_by_label(config)["gemini:gemini-3.5-flash"], 10)

    def test_validation_catches_unsound_tables(self):
        table = RoutingTable.from_dict({
            "models": {"local": {"capability": 1, "cost": 0}, "mystery": {"capability": 0, "cost": -1}},
            "tasks": {"strategy": {"min_capability": 9, "prefer": "fastest"}},
            "failures_before_cooldown": 0,
        })
        problems = table.validate({"local", "worker"})
        for needle in ("'mystery' is not a config spec", "capability must be 1 or more", "prefer must be",
                       "no model has capability 9", "failures_before_cooldown"):
            self.assertTrue(any(needle in problem for problem in problems), needle)


class RouterServiceTests(unittest.TestCase):
    def test_routes_a_task_to_its_first_configured_model(self):
        service, _, _ = make_service(env={"GEMINI_API_KEY": "g", "OPENROUTER_API_KEY": "o"})
        result = service.complete("narration", prompt="Describe the touchdown.")
        self.assertTrue(result.ok)
        # No OLLAMA_HOST: the local model is skipped, the cheapest remote answers.
        self.assertEqual(result.model, "openrouter:openai/gpt-oss-120b:free")
        strategy = service.complete("strategy", prompt="3rd and 7: call a play.")
        self.assertEqual(strategy.model, "gemini:gemini-3.5-flash (high)")

    def test_unknown_task_and_empty_prompt_fail_softly(self):
        service, _, _ = make_service()
        self.assertIn("unknown task", service.complete("haiku", prompt="x").error)
        self.assertEqual(service.complete("narration", prompt="  ").error, "empty prompt")

    def test_a_rate_limit_cools_the_model_down_and_falls_through(self):
        script = {"ollama:llama3.1": [ProviderError("HTTP 429: slow down", status=429), "local again"]}
        service, clock, built = make_service(script=script)
        first = service.complete("narration", prompt="one")
        self.assertTrue(first.ok)
        self.assertEqual(first.model, "openrouter:openai/gpt-oss-120b:free")
        local = next(client for client in built if client.provider == "ollama")
        calls = local.calls

        # While it cools down it isn't called at all.
        second = service.complete("narration", prompt="two")
        self.assertEqual(second.model, "openrouter:openai/gpt-oss-120b:free")
        self.assertEqual(local.calls, calls)
        narration = next(route for route in service.describe_routes() if route["task"] == "narration")
        self.assertFalse(narration["chain"][0]["available"], "the routes show the local model unavailable")
        health = {entry["name"]: entry for entry in service.health()}
        self.assertGreater(health["local"]["cooling_down_seconds"], 0)

        # After the cooldown it is first again.
        clock.now += service.table.rate_limit_cooldown_seconds + 1
        third = service.complete("narration", prompt="three")
        self.assertEqual(third.model, "ollama:llama3.1")
        self.assertEqual(third.text, "local again")

    def test_repeated_failures_cool_a_model_down(self):
        failure = ProviderError("HTTP 500: broken", status=500)
        service, _, built = make_service(env={"OPENROUTER_API_KEY": "o", "GEMINI_API_KEY": "g"},
                                         script={"gemini:gemini-3.5-flash (high)": [failure]})
        for _ in range(service.table.failures_before_cooldown):
            self.assertEqual(service.complete("strategy", prompt="x").model, "openrouter:openai/gpt-oss-120b")
        gemini = next(client for client in built if client.label == "gemini:gemini-3.5-flash (high)")
        calls = gemini.calls
        self.assertEqual(service.complete("strategy", prompt="x").model, "openrouter:openai/gpt-oss-120b")
        self.assertEqual(gemini.calls, calls, "a cooling model is skipped, not called")

    def test_request_budget_moves_on_to_the_next_provider(self):
        table = RoutingTable.load()
        table.models["supervisor"] = table.models["supervisor"].__class__("supervisor", 4, 2, rpm=2)
        table.models["fallback"] = table.models["fallback"].__class__("fallback", 2, 1, rpm=2)
        service, clock, _ = make_service(env={"GEMINI_API_KEY": "g", "OPENROUTER_API_KEY": "o"}, table=table)
        models = [service.complete("strategy", prompt=str(n)).model for n in range(3)]
        self.assertEqual(models[:2], ["gemini:gemini-3.5-flash (high)"] * 2)
        self.assertEqual(models[2], "openrouter:openai/gpt-oss-120b", "over Gemini's budget: the next provider")
        clock.now += 61
        self.assertEqual(service.complete("strategy", prompt="later").model, "gemini:gemini-3.5-flash (high)")

    def test_waits_for_a_model_when_every_one_is_cooling(self):
        script = {"gemini:gemini-3.5-flash (high)": [ProviderError("HTTP 429", status=429), "after the wait"]}
        table = RoutingTable.load()
        table.rate_limit_cooldown_seconds = 5
        service, clock, _ = make_service(env={"GEMINI_API_KEY": "g"}, script=script, table=table)
        # Only Gemini is configured for strategy: the first call is rate-limited and nothing else can answer.
        self.assertFalse(service.complete("strategy", prompt="x").ok)
        result = service.complete("strategy", prompt="y")
        self.assertTrue(result.ok)
        self.assertEqual(result.text, "after the wait")
        self.assertEqual(clock.slept, [5])
        self.assertEqual(result.waited_seconds, 5)
        # A wait longer than max_wait_seconds fails instead.
        table.rate_limit_cooldown_seconds = table.max_wait_seconds + 10
        service, _, _ = make_service(env={"GEMINI_API_KEY": "g"}, table=table,
                                     script={"gemini:gemini-3.5-flash (high)": [ProviderError("HTTP 429", status=429), "x"]})
        service.complete("strategy", prompt="x")
        self.assertIn("cooling down", service.complete("strategy", prompt="y").error)

    def test_health_makes_no_call_unless_asked_and_caches_live_checks(self):
        service, clock, built = make_service(env={"GEMINI_API_KEY": "g"})
        report = {entry["name"]: entry for entry in service.health()}
        self.assertTrue(report["supervisor"]["configured"])
        self.assertFalse(report["local"]["configured"])
        self.assertIsNone(report["supervisor"]["live"])
        self.assertEqual(sum(client.health_checks for client in built), 0)
        live = {entry["name"]: entry for entry in service.health(live=True)}
        self.assertTrue(live["supervisor"]["live"])
        checks = sum(client.health_checks for client in built)
        service.health(live=True)
        self.assertEqual(sum(client.health_checks for client in built), checks, "cached within the TTL")
        clock.now += service.table.health_ttl_seconds + 1
        service.health(live=True)
        self.assertGreater(sum(client.health_checks for client in built), checks)


class McpServerTests(unittest.TestCase):
    def call(self, server, method, params=None, request_id=1):
        line = json.dumps({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}})
        return json.loads(server.handle_line(line))

    def test_handshake_and_tool_list(self):
        server = McpServer(make_service()[0])
        result = self.call(server, "initialize", {"protocolVersion": "2025-03-26", "capabilities": {},
                                                  "clientInfo": {"name": "test", "version": "0"}})["result"]
        self.assertEqual(result["protocolVersion"], "2025-03-26")
        self.assertIn("tools", result["capabilities"])
        unknown = self.call(server, "initialize", {"protocolVersion": "1999-01-01"})["result"]
        self.assertEqual(unknown["protocolVersion"], SUPPORTED_PROTOCOLS[0])
        self.assertIsNone(server.handle_line(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"})))
        self.assertEqual(self.call(server, "ping")["result"], {})
        tools = {tool["name"]: tool for tool in self.call(server, "tools/list")["result"]["tools"]}
        self.assertEqual(set(tools), {"route_task", "list_routes", "router_health"})
        self.assertIn("strategy", tools["route_task"]["inputSchema"]["properties"]["task"]["enum"])

    def test_route_task_and_errors(self):
        server = McpServer(make_service(env={"GEMINI_API_KEY": "g"})[0])
        result = self.call(server, "tools/call", {"name": "route_task",
                                                  "arguments": {"task": "strategy", "prompt": "Call a play."}})["result"]
        self.assertFalse(result["isError"])
        self.assertEqual(result["structuredContent"]["model"], "gemini:gemini-3.5-flash (high)")
        self.assertIn("answer from", result["content"][0]["text"])
        bad = self.call(server, "tools/call", {"name": "route_task", "arguments": {"task": "haiku", "prompt": "x"}})
        self.assertTrue(bad["result"]["isError"])
        self.assertTrue(self.call(server, "tools/call", {"name": "route_task", "arguments": {"task": "strategy"}})["result"]["isError"])
        self.assertTrue(self.call(server, "tools/call", {"name": "nope"})["result"]["isError"])
        routes = self.call(server, "tools/call", {"name": "list_routes"})["result"]["structuredContent"]["routes"]
        self.assertEqual({route["task"] for route in routes}, {"narration", "summary", "analysis", "strategy", "delegate"})
        health = self.call(server, "tools/call", {"name": "router_health"})["result"]["structuredContent"]["models"]
        self.assertTrue(any(entry["name"] == "supervisor" and entry["configured"] for entry in health))
        self.assertEqual(self.call(server, "no/such/method")["error"]["code"], -32601)
        self.assertEqual(json.loads(server.handle_line("{not json"))["error"]["code"], -32700)
        self.assertEqual(json.loads(server.handle_line(json.dumps({"id": 3})))["error"]["code"], -32600)

    def test_serves_lines_on_stdio(self):
        server = McpServer(make_service(env={"GEMINI_API_KEY": "g"})[0])
        stdin = io.StringIO("\n".join([
            json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
            json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}),
            json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/list"}),
        ]) + "\n")
        stdout = io.StringIO()
        self.assertEqual(server.serve(stdin, stdout), 0)
        replies = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual([reply["id"] for reply in replies], [1, 2])


class CliTests(unittest.TestCase):
    def test_routes_command_prints_every_chain(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = main(["routes"])
        self.assertEqual(code, 0)
        text = out.getvalue()
        for task in ("narration", "summary", "analysis", "strategy", "delegate"):
            self.assertIn(task, text)


if __name__ == "__main__":
    unittest.main()
