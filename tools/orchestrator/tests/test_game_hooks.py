"""Epic 82: the relay between the game's model requests (over AgenticLink) and the model router.

No engine and no network: a fake engine answers AgenticLink's JSON-RPC the way the game's
UPSGameIntelligenceSubsystem does through call_function, and a fake router service answers by task.
"""

import io
import json
import unittest
from pathlib import Path

from tools.orchestrator.game_hooks import (AgenticLinkClient, AgenticLinkError, GameHookRelay, SUBSYSTEM,
                                           build_prompt, pick_choice)
from tools.orchestrator.routing import RoutingTable
from tools.orchestrator.service import RouteResult

REPO = Path(__file__).resolve().parents[3]

PLAY_CALL = {
    "id": 1, "kind": "PlayCall", "task": "strategy", "side": "offense", "secondsLeft": 6,
    "instructions": "Answer with exactly one id from 'choices'.",
    "choices": [{"id": "Offense_InsideZone", "note": "Run, I-Form: Inside Zone"},
                {"id": "Offense_SlantFlat", "note": "ShortPass, Trips Right: Slant Flat"},
                {"id": "Offense_Punt", "note": "Punt, Punt: Punt"}],
    "context": {"contract": "play-sports.game-state/1", "situation": {"down": 3, "distance": 2}},
}
SUMMARY = {"id": 2, "kind": "DriveSummary", "task": "summary", "instructions": "One sentence per drive.",
           "context": {"contract": "play-sports.post-game/1", "drives": []}}
ANALYSIS = {"id": 3, "kind": "GameAnalysis", "task": "analysis", "instructions": "Explain the game.",
            "context": {"contract": "play-sports.post-game/1", "keyPlays": []}}


class FakeEngine:
    """AgenticLink in front of the game's subsystem: the JSON-RPC each POST carries, answered."""

    def __init__(self, requests=None, world=True):
        self.requests = [dict(request) for request in (requests or [])]
        self.world = world
        self.messages = []
        self.answers = {}
        self.consulting = (False, False)

    def post(self, message):
        self.messages.append(message)
        if "id" not in message:
            return None
        method = message["method"]
        if method == "initialize":
            return {"jsonrpc": "2.0", "id": message["id"], "result": {"protocolVersion": "2025-06-18"}}
        if method != "tools/call" or message["params"]["name"] != "call_function":
            return {"jsonrpc": "2.0", "id": message["id"], "error": {"code": -32601, "message": "no such method"}}
        arguments = message["params"]["arguments"]
        if not self.world or arguments.get("subsystem") != SUBSYSTEM:
            return self.tool_error(message, f"Editor has no subsystem '{arguments.get('subsystem')}'.")
        call = arguments["arguments"]
        function = arguments["function"]
        if function == "SetConsultation":
            self.consulting = (call["bOffense"], call["bDefense"])
            return self.outputs(message, {"ReturnValue": "True"})
        if function == "GetPendingRequestsJson":
            open_requests = [r for r in self.requests if r["id"] not in self.answers]
            return self.outputs(message, {"ReturnValue": json.dumps({"bridge": True, "requests": open_requests})})
        if function == "AnswerRequest":
            request = next((r for r in self.requests if r["id"] == call["RequestId"]), None)
            choices = [choice["id"] for choice in (request or {}).get("choices", [])]
            if request is None or (choices and call["Answer"] not in choices):
                return self.outputs(message, {"ReturnValue": "False", "OutReason": "not one of the choices"})
            self.answers[call["RequestId"]] = call["Answer"]
            return self.outputs(message, {"ReturnValue": "True", "OutReason": ""})
        return self.tool_error(message, f"no function '{function}'")

    def calls(self, function):
        return [m for m in self.messages if m.get("method") == "tools/call" and m["params"]["arguments"]["function"] == function]

    @staticmethod
    def outputs(message, outputs):
        return {"jsonrpc": "2.0", "id": message["id"], "result": {
            "content": [{"type": "text", "text": json.dumps(outputs)}], "isError": False,
            "structuredContent": {"subsystem": SUBSYSTEM, "outputs": outputs, "undoable": False}}}

    @staticmethod
    def tool_error(message, text):
        return {"jsonrpc": "2.0", "id": message["id"], "result": {"content": [{"type": "text", "text": text}], "isError": True}}


class FakeService:
    """RouterService.complete by task: a scripted answer (or a failure) per task."""

    def __init__(self, replies):
        self.replies = replies
        self.calls = []

    def complete(self, task, prompt="", system="", **_):
        self.calls.append({"task": task, "prompt": prompt, "system": system})
        reply = self.replies.get(task)
        if reply is None:
            return RouteResult(ok=False, task=task, error="every model in the chain failed")
        return RouteResult(ok=True, task=task, text=reply, model=f"fake:{task}")


def make_relay(requests, replies, world=True):
    engine = FakeEngine(requests, world=world)
    service = FakeService(replies)
    return GameHookRelay(AgenticLinkClient(post=engine.post), service), engine, service


class PickChoiceTest(unittest.TestCase):
    CHOICES = ["Offense_InsideZone", "Offense_SlantFlat", "Offense_Punt"]

    def test_a_bare_id_in_any_case(self):
        self.assertEqual(pick_choice(" offense_slantflat\n", self.CHOICES), "Offense_SlantFlat")
        self.assertEqual(pick_choice("`Offense_Punt`.", self.CHOICES), "Offense_Punt")

    def test_the_first_named_in_prose(self):
        text = "Third and two: I'd run Offense_InsideZone rather than Offense_SlantFlat."
        self.assertEqual(pick_choice(text, self.CHOICES), "Offense_InsideZone")

    def test_whole_words_only(self):
        self.assertEqual(pick_choice("Offense_PuntFake looks fun", self.CHOICES), "")
        self.assertEqual(pick_choice("no idea", self.CHOICES), "")


class RelayTest(unittest.TestCase):
    def test_a_play_call_routes_as_strategy_and_answers_with_the_named_choice(self):
        relay, engine, service = make_relay([PLAY_CALL], {"strategy": "Go with Offense_InsideZone, it's short."})
        outcomes = relay.poll_once()
        self.assertEqual(len(outcomes), 1)
        self.assertTrue(outcomes[0].answered, outcomes[0].error)
        self.assertEqual(service.calls[0]["task"], "strategy")
        self.assertEqual(service.calls[0]["system"], PLAY_CALL["instructions"])
        self.assertIn("Offense_SlantFlat: ShortPass", service.calls[0]["prompt"])
        self.assertIn('"down":3', service.calls[0]["prompt"])
        self.assertEqual(engine.answers, {1: "Offense_InsideZone"})
        answer = engine.calls("AnswerRequest")[0]["params"]["arguments"]
        self.assertEqual(answer["subsystem"], SUBSYSTEM)
        self.assertEqual(answer["arguments"], {"RequestId": 1, "Answer": "Offense_InsideZone"})

    def test_post_game_requests_route_by_their_own_tasks_as_free_text(self):
        relay, engine, service = make_relay([SUMMARY, ANALYSIS], {"summary": "Two drives.", "analysis": "Defense won it."})
        outcomes = relay.poll_once()
        self.assertEqual([c["task"] for c in service.calls], ["summary", "analysis"])
        self.assertTrue(all(o.answered for o in outcomes))
        self.assertEqual(engine.answers, {2: "Two drives.", 3: "Defense won it."})
        self.assertEqual(outcomes[1].model, "fake:analysis")

    def test_no_choice_named_sends_nothing_so_the_cpu_calls_its_own(self):
        relay, engine, _ = make_relay([PLAY_CALL], {"strategy": "Something creative."})
        outcome = relay.poll_once()[0]
        self.assertFalse(outcome.answered)
        self.assertIn("none of the choices", outcome.error)
        self.assertEqual(engine.calls("AnswerRequest"), [])

    def test_a_router_failure_sends_nothing(self):
        relay, engine, _ = make_relay([PLAY_CALL], {})
        outcome = relay.poll_once()[0]
        self.assertFalse(outcome.answered)
        self.assertIn("router failed", outcome.error)
        self.assertEqual(engine.calls("AnswerRequest"), [])

    def test_a_refused_answer_reports_the_games_reason(self):
        relay, engine, _ = make_relay([PLAY_CALL], {"strategy": "Offense_Punt"})

        def refuse(message):
            if message.get("method") == "tools/call" and message["params"]["arguments"]["function"] == "AnswerRequest":
                return FakeEngine.outputs(message, {"ReturnValue": "False", "OutReason": "Request 1 is TimedOut, no longer open."})
            return engine.post(message)

        relay.link._post = refuse
        outcome = relay.poll_once()[0]
        self.assertFalse(outcome.answered)
        self.assertIn("no longer open", outcome.error)

    def test_each_request_is_handled_once(self):
        relay, _, service = make_relay([PLAY_CALL], {})
        relay.poll_once()
        relay.poll_once()
        self.assertEqual(len(service.calls), 1)

    def test_consultation_is_asked_with_the_sides(self):
        relay, engine, _ = make_relay([], {})
        self.assertTrue(relay.consult(True, False))
        self.assertEqual(engine.consulting, (True, False))

    def test_serve_once_initializes_consults_and_answers(self):
        relay, engine, _ = make_relay([PLAY_CALL], {"strategy": "Offense_SlantFlat"})
        out = io.StringIO()
        self.assertEqual(relay.serve(offense=True, once=True, out=out), 0)
        self.assertEqual(engine.messages[0]["method"], "initialize")
        self.assertEqual(engine.messages[1]["method"], "notifications/initialized")
        self.assertEqual(engine.consulting, (True, False))
        self.assertEqual(engine.answers, {1: "Offense_SlantFlat"})
        self.assertIn("answered by fake:strategy", out.getvalue())

    def test_serve_waits_for_a_game_world(self):
        relay, engine, service = make_relay([PLAY_CALL], {"strategy": "Offense_SlantFlat"}, world=False)
        out = io.StringIO()
        self.assertEqual(relay.serve(once=True, out=out), 0)
        self.assertIn("waiting for a game world", out.getvalue())
        self.assertEqual(service.calls, [])

    def test_serve_reports_an_engine_that_is_not_there(self):
        def unreachable(_message):
            raise AgenticLinkError("AgenticLink at http://127.0.0.1:8790/mcp is not answering")

        relay = GameHookRelay(AgenticLinkClient(post=unreachable), FakeService({}))
        out = io.StringIO()
        self.assertEqual(relay.serve(once=True, out=out), 1)
        self.assertIn("not answering", out.getvalue())


class ClientTest(unittest.TestCase):
    def test_a_tool_error_raises_with_its_text(self):
        engine = FakeEngine(world=False)
        client = AgenticLinkClient(post=engine.post)
        with self.assertRaisesRegex(AgenticLinkError, "no subsystem"):
            client.call_function(SUBSYSTEM, "GetPendingRequestsJson")

    def test_a_json_rpc_error_raises(self):
        client = AgenticLinkClient(post=FakeEngine().post)
        with self.assertRaisesRegex(AgenticLinkError, "no such method"):
            client.request("resources/list")

    def test_build_prompt_carries_the_context_and_choices(self):
        prompt = build_prompt(PLAY_CALL)
        self.assertIn("play-sports.game-state/1", prompt)
        self.assertIn("- Offense_Punt: Punt, Punt: Punt", prompt)
        self.assertNotIn("Choices", build_prompt(SUMMARY))


class RoutingContractTest(unittest.TestCase):
    def test_the_games_tasks_are_routed_and_strategy_gets_the_better_models(self):
        tuning = json.loads((REPO / "Data" / "game_intelligence.json").read_text(encoding="utf-8"))
        table = RoutingTable.load()
        for field in ("PlayCallTask", "DriveSummaryTask", "GameAnalysisTask"):
            self.assertIn(tuning[field], table.tasks, field)
        play_call = table.tasks[tuning["PlayCallTask"]]
        summary = table.tasks[tuning["DriveSummaryTask"]]
        self.assertGreaterEqual(play_call.min_capability, summary.min_capability)
        self.assertEqual(summary.prefer, "cheapest")


if __name__ == "__main__":
    unittest.main()
