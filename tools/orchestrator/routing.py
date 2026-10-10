"""The capability/cost routing table (Epic 119).

tools/orchestrator/routing.json names, for each kind of task, how much capability it needs and
whether it wants the cheapest model that has it or the best one:

  narration  -> cheapest first: local Ollama, then the free remote tiers, then the paid ones
  strategy   -> best first: Gemini Flash high, then GPT-OSS-120B

The models are config.py's env-driven specs (OrchestratorConfig.named_specs), so a model ID is
set in one place. A task's chain is every model with at least its min_capability, ordered by
its preference; ModelRouter walks the chain like any tier and skips a model whose key or host
isn't set. Epic 82 (game-intelligence hooks) and Epic 96 (commentary) route through the tasks
that name them as consumers.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from .config import ModelSpec, OrchestratorConfig

DEFAULT_TABLE_PATH = Path(__file__).resolve().parent / "routing.json"
PREFERENCES = ("cheapest", "best")


@dataclass(frozen=True)
class ModelEntry:
    """One model the table can route to: config.py's spec by name, with its capability (1 is
    a small local model, higher is better), cost (0 is free and local) and request budget."""

    name: str
    capability: int
    cost: int
    rpm: int = 0  # requests per minute the service allows; 0 for no limit


@dataclass(frozen=True)
class TaskRoute:
    name: str
    min_capability: int
    prefer: str
    max_tokens: int = 2048
    temperature: float = 0.2
    description: str = ""
    consumers: tuple[str, ...] = ()


@dataclass
class RoutingTable:
    models: dict[str, ModelEntry] = field(default_factory=dict)
    tasks: dict[str, TaskRoute] = field(default_factory=dict)
    rate_limit_cooldown_seconds: float = 60.0
    failure_cooldown_seconds: float = 30.0
    failures_before_cooldown: int = 2
    health_ttl_seconds: float = 300.0
    max_wait_seconds: float = 20.0

    @classmethod
    def from_dict(cls, payload: dict) -> "RoutingTable":
        models = {
            name: ModelEntry(name=name, capability=int(entry.get("capability", 0)),
                             cost=int(entry.get("cost", 0)), rpm=int(entry.get("rpm", 0)))
            for name, entry in (payload.get("models") or {}).items()
        }
        tasks = {
            name: TaskRoute(
                name=name,
                min_capability=int(entry.get("min_capability", 0)),
                prefer=str(entry.get("prefer", "")),
                max_tokens=int(entry.get("max_tokens", 2048)),
                temperature=float(entry.get("temperature", 0.2)),
                description=str(entry.get("description", "")),
                consumers=tuple(entry.get("consumers") or ()),
            )
            for name, entry in (payload.get("tasks") or {}).items()
        }
        table = cls(models=models, tasks=tasks)
        for key in ("rate_limit_cooldown_seconds", "failure_cooldown_seconds", "health_ttl_seconds", "max_wait_seconds"):
            if key in payload:
                setattr(table, key, float(payload[key]))
        if "failures_before_cooldown" in payload:
            table.failures_before_cooldown = int(payload["failures_before_cooldown"])
        return table

    @classmethod
    def load(cls, path: Path | None = None) -> "RoutingTable":
        return cls.from_dict(json.loads(Path(path or DEFAULT_TABLE_PATH).read_text(encoding="utf-8")))

    def validate(self, spec_names: set[str] | None = None) -> list[str]:
        """Problems with the table, one line each (empty when sound). With spec_names, every
        model must be one of config.py's named specs."""
        problems = []
        if not self.models:
            problems.append("no models")
        for entry in self.models.values():
            if spec_names is not None and entry.name not in spec_names:
                problems.append(f"model {entry.name!r} is not a config spec ({sorted(spec_names)})")
            if entry.capability < 1 or entry.cost < 0 or entry.rpm < 0:
                problems.append(f"model {entry.name!r}: capability must be 1 or more, cost and rpm 0 or more")
        if not self.tasks:
            problems.append("no tasks")
        for task in self.tasks.values():
            if task.prefer not in PREFERENCES:
                problems.append(f"task {task.name!r}: prefer must be one of {list(PREFERENCES)}")
            if not self.chain_names(task.name):
                problems.append(f"task {task.name!r}: no model has capability {task.min_capability} or more")
            if task.max_tokens < 1 or not 0.0 <= task.temperature <= 2.0:
                problems.append(f"task {task.name!r}: max_tokens must be 1 or more, temperature 0 to 2")
        if self.rate_limit_cooldown_seconds < 0 or self.failure_cooldown_seconds < 0 or self.max_wait_seconds < 0:
            problems.append("cooldowns and max_wait_seconds must be 0 or more")
        if self.failures_before_cooldown < 1 or self.health_ttl_seconds <= 0:
            problems.append("failures_before_cooldown must be 1 or more, health_ttl_seconds above 0")
        return problems

    def chain_names(self, task: str) -> list[str]:
        """The models a task routes to, in the order they are tried."""
        route = self.tasks[task]
        eligible = [entry for entry in self.models.values() if entry.capability >= route.min_capability]
        if route.prefer == "best":
            key = lambda entry: (-entry.capability, entry.cost, entry.name)
        else:
            key = lambda entry: (entry.cost, -entry.capability, entry.name)
        return [entry.name for entry in sorted(eligible, key=key)]

    def tiers(self, config: OrchestratorConfig) -> dict[str, list[ModelSpec]]:
        """Every task as a router tier: its chain of config.py specs."""
        specs = config.named_specs()
        return {task: [specs[name] for name in self.chain_names(task) if name in specs] for task in self.tasks}

    def rpm_by_label(self, config: OrchestratorConfig) -> dict[str, int]:
        """Requests per minute per client label ("provider:model"). Two names on one model share
        its budget: the lower limit wins."""
        limits: dict[str, int] = {}
        for name, spec in config.named_specs().items():
            entry = self.models.get(name)
            if entry is None or entry.rpm <= 0:
                continue
            label = f"{spec.provider}:{spec.model}"
            limits[label] = min(limits.get(label, entry.rpm), entry.rpm)
        return limits
