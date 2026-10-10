"""The model router service (Epic 119).

RouterService turns the .env free-tier contract into one long-lived service that tasks route
through by what they need, not by which provider they name:

  - Routing: each task in the routing table (tools/orchestrator/routing.json: narration,
    summary, analysis, strategy, delegate) is a tier of Epic 135's ModelRouter, its chain built
    from config.py's specs by capability and cost. Callers such as Epic 82's hooks and Epic 96's
    commentary ask for a task.
  - Fallback chains: ModelRouter walks the chain; a provider error falls through to the next
    model (Epic 135).
  - Rate-limit handling across providers: each call to a provider already retries a 429 after
    its Retry-After (models/base.py). On top of that the service
      * keeps each model within its requests-per-minute budget, skipping it while it is full;
      * cools a model down after a rate limit (rate_limit_cooldown_seconds) or after
        failures_before_cooldown failures in a row (failure_cooldown_seconds), and skips it
        until then, so one throttled provider doesn't slow every request;
      * when every configured model of a chain is skipped, waits up to max_wait_seconds for
        the first to come free instead of failing at once.
  - Health: what each model's configuration, cooldown and budget say, without a network call;
    with live=True, one minimal call per configured model, cached for health_ttl_seconds.

The MCP surface is tools/orchestrator/mcp_server.py. The clock and sleep are injectable so tests
run with no network and no waiting.
"""

from __future__ import annotations

import time
from collections import deque
from dataclasses import dataclass, field

from .config import OrchestratorConfig
from .models.base import ChatResponse, Message, ModelClient, ProviderError, ToolSpec
from .models.router import ModelRouter, build_client
from .routing import RoutingTable

RATE_LIMITED = 429
WINDOW_SECONDS = 60.0


def quota_key(client: ModelClient) -> str:
    """"provider:model": what a provider's rate limit counts against. Gemini's high and low
    reasoning are one model, so they share a budget and a cooldown."""
    return f"{client.provider}:{client.model}"


@dataclass
class ModelState:
    """What the service has seen of one model (keyed by quota_key)."""

    label: str
    rpm: int = 0
    cooldown_until: float = 0.0
    consecutive_failures: int = 0
    last_error: str = ""
    requests: deque = field(default_factory=deque)  # times of recent requests
    live_ok: bool | None = None
    live_checked_at: float = -1.0


@dataclass
class RouteResult:
    ok: bool
    task: str
    text: str = ""
    model: str = ""  # the client that answered
    tool_calls: list = field(default_factory=list)
    error: str = ""
    waited_seconds: float = 0.0


class RouterService:
    def __init__(self, config: OrchestratorConfig | None = None, table: RoutingTable | None = None,
                 client_factory=build_client, clock=time.monotonic, sleep=time.sleep):
        self.config = config or OrchestratorConfig.load()
        self.table = table or RoutingTable.load()
        self.router = ModelRouter(self.config, client_factory=client_factory,
                                  extra_tiers=self.table.tiers(self.config))
        self._factory = client_factory
        self._clock = clock
        self._sleep = sleep
        self._rpm = self.table.rpm_by_label(self.config)
        self._states: dict[str, ModelState] = {}

    # --- routing ---------------------------------------------------------------------------

    def tasks(self) -> list[str]:
        return list(self.table.tasks)

    def chain(self, task: str) -> list[ModelClient]:
        """The clients a task tries, in order."""
        if task not in self.table.tasks:
            raise KeyError(f"unknown task: {task!r} (have {sorted(self.table.tasks)})")
        return self.router.clients(task)

    def describe_routes(self) -> list[dict]:
        """Each task, what it is for, and its chain with each model's state: no network."""
        routes = []
        for task, route in self.table.tasks.items():
            links = []
            for name, client in zip(self.table.chain_names(task), self.chain(task)):
                links.append({"name": name, "label": client.label, "configured": bool(client.api_key),
                              "available": self.blocked_reason(client) is None})
            routes.append({"task": task, "description": route.description, "consumers": list(route.consumers),
                           "prefer": route.prefer, "min_capability": route.min_capability, "chain": links})
        return routes

    def complete(self, task: str, prompt: str = "", system: str = "", messages: list[Message] | None = None,
                 tools: list[ToolSpec] | None = None, max_tokens: int | None = None,
                 temperature: float | None = None) -> RouteResult:
        """One chat for a task down its chain. Never raises for a provider failure or an unknown
        task: the result says what went wrong, model by model."""
        if task not in self.table.tasks:
            return RouteResult(ok=False, task=task, error=f"unknown task: {task!r} (have {sorted(self.table.tasks)})")
        route = self.table.tasks[task]
        if messages is None:
            if not prompt.strip():
                return RouteResult(ok=False, task=task, error="empty prompt")
            messages = ([Message(role="system", content=system)] if system.strip() else []) \
                + [Message(role="user", content=prompt)]
        tokens = max_tokens or route.max_tokens
        warmth = route.temperature if temperature is None else temperature

        waited = 0.0
        while True:
            attempted = []

            def observe(client, response, error):
                attempted.append(client.label)
                self.observe(client, response, error)

            try:
                response = self.router.chat(task, messages, tools=tools, temperature=warmth, max_tokens=tokens,
                                            gate=self.gate, observe=observe)
                return RouteResult(ok=True, task=task, text=response.text, model=self.router.last_label,
                                   tool_calls=list(response.tool_calls), waited_seconds=waited)
            except ProviderError as error:
                # Models were called and all failed: that is the answer. When none could be
                # called (every configured one cooling down or over budget), wait for the first
                # to come free, within max_wait_seconds, rather than fail at once.
                delay = None if attempted else self.seconds_until_available(task)
                if delay is None or waited + delay > self.table.max_wait_seconds:
                    return RouteResult(ok=False, task=task, error=str(error), waited_seconds=waited)
                self._sleep(delay)
                waited += delay

    # --- rate limits, cooldowns and failures ---------------------------------------------------

    def state(self, label: str) -> ModelState:
        if label not in self._states:
            self._states[label] = ModelState(label=label, rpm=self._rpm.get(label, 0))
        return self._states[label]

    def blocked_reason(self, client: ModelClient) -> str | None:
        """Why the service would skip this client right now, or None."""
        now = self._clock()
        model = self.state(quota_key(client))
        if now < model.cooldown_until:
            return f"cooling down for {model.cooldown_until - now:.0f}s ({model.last_error or 'rate limited'})"
        while model.requests and now - model.requests[0] >= WINDOW_SECONDS:
            model.requests.popleft()
        if model.rpm and len(model.requests) >= model.rpm:
            return f"at its budget of {model.rpm} requests a minute"
        return None

    def gate(self, client: ModelClient) -> str | None:
        """ModelRouter's gate: blocked_reason, and a client let through counts against its
        request budget."""
        reason = self.blocked_reason(client)
        if reason is None:
            self.state(quota_key(client)).requests.append(self._clock())
        return reason

    def observe(self, client: ModelClient, response: ChatResponse | None, error: ProviderError | None) -> None:
        model = self.state(quota_key(client))
        if error is None:
            model.consecutive_failures = 0
            model.last_error = ""
            return
        model.last_error = str(error)[:200]
        if getattr(error, "status", None) == RATE_LIMITED:
            model.cooldown_until = self._clock() + self.table.rate_limit_cooldown_seconds
            model.consecutive_failures = 0
            return
        model.consecutive_failures += 1
        if model.consecutive_failures >= self.table.failures_before_cooldown:
            model.cooldown_until = self._clock() + self.table.failure_cooldown_seconds
            model.consecutive_failures = 0

    def seconds_until_available(self, task: str) -> float | None:
        """How long until a configured model in the task's chain can be called again; None when
        none is configured."""
        now = self._clock()
        waits = []
        for client in self.chain(task):
            if not client.api_key:
                continue
            model = self.state(quota_key(client))
            wait = max(model.cooldown_until - now, 0.0)
            if model.rpm and len(model.requests) >= model.rpm:
                wait = max(wait, WINDOW_SECONDS - (now - model.requests[0]))
            waits.append(wait)
        return min(waits) if waits else None

    # --- health ----------------------------------------------------------------------------

    def health(self, live: bool = False) -> list[dict]:
        """One entry per model the table routes to. Without live, no network call is made."""
        now = self._clock()
        report = []
        for name, spec in self.config.named_specs().items():
            if name not in self.table.models:
                continue
            label = f"{spec.provider}:{spec.model}"
            model = self.state(label)
            stale = model.live_checked_at < 0 or now - model.live_checked_at >= self.table.health_ttl_seconds
            if live and spec.configured and stale:
                model.live_ok = self._factory(spec).healthy()
                model.live_checked_at = now
            report.append({
                "name": name,
                "label": spec.label,
                "configured": spec.configured,
                "cooling_down_seconds": round(max(model.cooldown_until - now, 0.0), 1),
                "requests_last_minute": sum(1 for at in model.requests if now - at < WINDOW_SECONDS),
                "rpm": model.rpm,
                "last_error": model.last_error,
                "live": model.live_ok,
            })
        return report
