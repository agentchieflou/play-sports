"""Epic 82's relay: the game's requests for outside models, answered through the model router.

The game keeps a list of open requests in UPSGameIntelligenceSubsystem, reachable while the editor
serves AgenticLink's MCP endpoint (Epic 25, `-AgenticLinkMcp`, http://127.0.0.1:8790/mcp): a CPU
side's play call (its call waits for the answer, up to a timeout), and the drive summary and game
analysis at the final whistle. Each request names the model router's task it is for (Epic 119's
routing.json: strategy for a play call, summary and analysis after the game). This relay is the
agent between the two, and consumes both rather than twinning either:

  1. it opens an MCP session with AgenticLink;
  2. it turns play-call consultation on for the sides asked (SetConsultation);
  3. it polls GetPendingRequestsJson, and sends each new request down its task's chain with
     RouterService.complete (the router picks the model; this relay never does);
  4. it answers with AnswerRequest. A play call's answer is the first of its choices the model's
     text names; when the model names none, or the router fails, nothing is sent and the game's
     timeout has the CPU make its own call.

Every engine call is AgenticLink's call_function on the subsystem. Stdlib only; the transport, the
router and the clock are injectable, so the tests run with no engine, no network and no waiting.

    python -m tools.orchestrator game-hooks [--url URL] [--offense] [--defense] [--once]
"""

from __future__ import annotations

import json
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass

DEFAULT_URL = "http://127.0.0.1:8790/mcp"
SUBSYSTEM = "PSGameIntelligenceSubsystem"
PROTOCOL_VERSION = "2025-06-18"


class AgenticLinkError(RuntimeError):
    """The engine refused a call, or couldn't be reached."""


class AgenticLinkClient:
    """MCP over Streamable HTTP to AgenticLink: one JSON-RPC message per POST."""

    def __init__(self, url: str = DEFAULT_URL, post=None, timeout: float = 10.0):
        self.url = url
        self.timeout = timeout
        self._post = post or self._http_post
        self._next_id = 0
        # The engine is on this machine: never send it through a proxy.
        self._opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def _http_post(self, message: dict) -> dict | None:
        request = urllib.request.Request(
            self.url, data=json.dumps(message).encode("utf-8"), method="POST",
            headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
        try:
            with self._opener.open(request, timeout=self.timeout) as response:
                body = response.read().decode("utf-8")
        except (urllib.error.URLError, OSError) as error:
            raise AgenticLinkError(f"AgenticLink at {self.url} is not answering ({error}); "
                                   "start the editor with -AgenticLinkMcp") from error
        return json.loads(body) if body.strip() else None

    def request(self, method: str, params: dict | None = None) -> dict:
        self._next_id += 1
        reply = self._post({"jsonrpc": "2.0", "id": self._next_id, "method": method, "params": params or {}})
        if not reply:
            raise AgenticLinkError(f"{method}: no answer")
        if "error" in reply:
            raise AgenticLinkError(f"{method}: {reply['error'].get('message', reply['error'])}")
        return reply.get("result") or {}

    def notify(self, method: str, params: dict | None = None) -> None:
        self._post({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def initialize(self) -> dict:
        result = self.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "capabilities": {},
                                             "clientInfo": {"name": "play-sports-game-hooks", "version": "1.0"}})
        self.notify("notifications/initialized")
        return result

    def call_function(self, subsystem: str, function: str, arguments: dict | None = None) -> dict:
        """The outputs (name -> text) of a BlueprintCallable function on a world subsystem."""
        result = self.request("tools/call", {"name": "call_function", "arguments": {
            "subsystem": subsystem, "function": function, "arguments": arguments or {}}})
        if result.get("isError"):
            content = result.get("content") or [{}]
            raise AgenticLinkError(content[0].get("text", f"{function} failed"))
        return (result.get("structuredContent") or {}).get("outputs") or {}


def pick_choice(text: str, choices: list[str]) -> str:
    """The choice the model's text names: the whole answer when it is one, else the first named
    in it (as a whole word, any case). Empty when it names none."""
    by_lower = {choice.lower(): choice for choice in choices}
    stripped = text.strip().strip("`'\".").lower()
    if stripped in by_lower:
        return by_lower[stripped]
    earliest = None
    for choice in choices:
        match = re.search(rf"(?<![A-Za-z0-9_]){re.escape(choice)}(?![A-Za-z0-9_])", text, re.IGNORECASE)
        if match and (earliest is None or match.start() < earliest[0]):
            earliest = (match.start(), choice)
    return earliest[1] if earliest else ""


def build_prompt(request: dict) -> str:
    """What the model reads: the request's facts as JSON, and its choices when it has them."""
    lines = [f"The game asks ({request.get('kind', 'request')} {request.get('id')}).",
             "Game data (JSON):", json.dumps(request.get("context", {}), separators=(",", ":"))]
    choices = request.get("choices") or []
    if choices:
        lines.append(f"You call for the {request.get('side', 'CPU')}. Choices (answer with one id only):")
        lines.extend(f"- {choice.get('id')}: {choice.get('note', '')}" for choice in choices)
    return "\n".join(lines)


@dataclass
class HookOutcome:
    request_id: int
    kind: str
    task: str
    answered: bool = False
    answer: str = ""
    model: str = ""
    error: str = ""

    def describe(self) -> str:
        if self.answered:
            return f"request {self.request_id} ({self.kind}, {self.task}): answered by {self.model}: {self.answer[:80]}"
        return f"request {self.request_id} ({self.kind}, {self.task}): not answered: {self.error}"


class GameHookRelay:
    """Polls the game's requests and answers them through the model router service."""

    def __init__(self, link: AgenticLinkClient, service, subsystem: str = SUBSYSTEM):
        self.link = link
        self.service = service
        self.subsystem = subsystem
        self._handled: set[int] = set()

    def consult(self, offense: bool, defense: bool) -> bool:
        """Asks the game to wait for this relay's play calls on those CPU sides."""
        outputs = self.link.call_function(self.subsystem, "SetConsultation", {"bOffense": offense, "bDefense": defense})
        return outputs.get("ReturnValue") == "True"

    def pending(self) -> list[dict]:
        outputs = self.link.call_function(self.subsystem, "GetPendingRequestsJson")
        payload = json.loads(outputs.get("ReturnValue") or "{}")
        return payload.get("requests") or []

    def poll_once(self) -> list[HookOutcome]:
        """Each request not seen before, routed and answered once."""
        outcomes = []
        for request in self.pending():
            request_id = request.get("id")
            if not isinstance(request_id, int) or request_id in self._handled:
                continue
            self._handled.add(request_id)
            outcomes.append(self.handle(request))
        return outcomes

    def handle(self, request: dict) -> HookOutcome:
        outcome = HookOutcome(request_id=request["id"], kind=str(request.get("kind", "")), task=str(request.get("task", "")))
        result = self.service.complete(outcome.task, prompt=build_prompt(request), system=str(request.get("instructions", "")))
        if not result.ok:
            outcome.error = f"the router failed: {result.error}"
            return outcome
        outcome.model = result.model
        answer = result.text.strip()
        choices = [str(choice.get("id")) for choice in request.get("choices") or []]
        if choices:
            answer = pick_choice(result.text, choices)
            if not answer:
                outcome.error = "the model named none of the choices; the CPU calls its own"
                return outcome
        outputs = self.link.call_function(self.subsystem, "AnswerRequest", {"RequestId": request["id"], "Answer": answer})
        outcome.answered = outputs.get("ReturnValue") == "True"
        outcome.answer = answer
        if not outcome.answered:
            outcome.error = outputs.get("OutReason") or "refused"
        return outcome

    def serve(self, offense: bool = False, defense: bool = False, interval: float = 0.5, once: bool = False,
              sleep=time.sleep, out=None) -> int:
        """Answers the game's requests until interrupted (or after one poll with once). A game
        world that comes (back) up gets the consultation asked for again."""
        out = out or sys.stdout
        try:
            self.link.initialize()
        except AgenticLinkError as error:
            print(f"game-hooks: {error}", file=out)
            return 1
        in_world = False
        try:
            while True:
                try:
                    if not in_world and (offense or defense) and not self.consult(offense, defense):
                        print("game-hooks: the game refused consultation (is the bridge online?)", file=out)
                    if not in_world:
                        # A new world numbers its requests from 1 again.
                        self._handled.clear()
                        print("game-hooks: in a game world; answering its requests", file=out)
                    in_world = True
                    for outcome in self.poll_once():
                        print(f"game-hooks: {outcome.describe()}", file=out)
                except AgenticLinkError as error:
                    if in_world or once:
                        print(f"game-hooks: waiting for a game world ({error})", file=out)
                    in_world = False
                if once:
                    return 0
                sleep(interval)
        except KeyboardInterrupt:
            if in_world and (offense or defense):
                try:
                    self.consult(False, False)
                except AgenticLinkError:
                    pass
            return 0
