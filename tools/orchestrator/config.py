"""Environment and model-tier configuration for the orchestrator (Epic 135).

Reads the repo's .env contract (see .env.example / AGENTS.md):

    GEMINI_API_KEY     - supervisor tier (Gemini 3.5 Flash, high reasoning) and
                         the worker fallback (same key, low reasoning)
    OPENROUTER_API_KEY - primary worker tier (GPT-OSS-120B)
    OLLAMA_HOST        - a local Ollama server, first in the free-tier "bridge"
                         chain (Core 25's .env model router)

Model IDs are overridable via optional env vars so a model rename never needs
a code change:

    ORCH_SUPERVISOR_MODEL (default: gemini-3.5-flash)
    ORCH_WORKER_MODEL     (default: openai/gpt-oss-120b)
    ORCH_FALLBACK_MODEL   (default: gemini-3.5-flash)
    ORCH_LOCAL_MODEL      (default: llama3.1)                 - bridge, on Ollama
    ORCH_BRIDGE_MODEL     (default: openai/gpt-oss-120b:free) - bridge, on OpenRouter

The "bridge" tier is the free-tier chain bridge tasks are delegated to
(`python -m tools.orchestrator delegate`): local Ollama first (no cost, works
offline), then Gemini's free tier, then OpenRouter's free models. The router
skips a link whose host or key isn't set.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

DEFAULT_SUPERVISOR_MODEL = "gemini-3.5-flash"
DEFAULT_WORKER_MODEL = "openai/gpt-oss-120b"
DEFAULT_FALLBACK_MODEL = "gemini-3.5-flash"
DEFAULT_LOCAL_MODEL = "llama3.1"
DEFAULT_BRIDGE_MODEL = "openai/gpt-oss-120b:free"


def load_env_file(path: Path) -> dict[str, str]:
    """Parse a KEY=VALUE .env file (comments/blank lines ignored, no quotes
    interpretation beyond stripping a single matching pair). Missing file -> {}."""
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
            value = value[1:-1]
        if key:
            values[key] = value
    return values


@dataclass(frozen=True)
class ModelSpec:
    """One concrete model choice within a tier."""

    provider: str  # "gemini" | "openrouter" | "ollama"
    model: str
    reasoning: str | None = None  # gemini thinking level: "high" | "low" | None
    api_key: str = ""
    base_url: str = ""  # ollama: the server (OLLAMA_HOST); no key needed

    @property
    def configured(self) -> bool:
        return bool(self.api_key or self.base_url)

    @property
    def label(self) -> str:
        suffix = f" ({self.reasoning})" if self.reasoning else ""
        return f"{self.provider}:{self.model}{suffix}"


@dataclass
class OrchestratorConfig:
    """Resolved configuration: env values + the model-tier table.

    Tiers:
      supervisor - single spec (Gemini Flash high)
      worker     - ordered fallback chain (OpenRouter GPT-OSS-120B ->
                   Gemini Flash low)
    """

    env: dict[str, str] = field(default_factory=dict)

    @classmethod
    def load(cls, repo_root: Path | None = None,
             environ: dict[str, str] | None = None) -> "OrchestratorConfig":
        """Merge .env file values with the process environment (environ wins,
        so exported variables override the checked-out .env)."""
        root = repo_root or REPO_ROOT
        environ = os.environ if environ is None else environ
        merged = load_env_file(root / ".env")
        merged.update({k: v for k, v in environ.items() if v})
        return cls(env=merged)

    @property
    def gemini_api_key(self) -> str:
        return self.env.get("GEMINI_API_KEY", "")

    @property
    def openrouter_api_key(self) -> str:
        return self.env.get("OPENROUTER_API_KEY", "")

    @property
    def ollama_host(self) -> str:
        return self.env.get("OLLAMA_HOST", "")

    def supervisor_spec(self) -> ModelSpec:
        return ModelSpec(
            provider="gemini",
            model=self.env.get("ORCH_SUPERVISOR_MODEL", DEFAULT_SUPERVISOR_MODEL),
            reasoning="high",
            api_key=self.gemini_api_key,
        )

    def worker_specs(self) -> list[ModelSpec]:
        """Ordered worker fallback chain. Specs whose key is missing are still
        listed (the router reports them unhealthy rather than hiding them)."""
        return [
            ModelSpec(
                provider="openrouter",
                model=self.env.get("ORCH_WORKER_MODEL", DEFAULT_WORKER_MODEL),
                api_key=self.openrouter_api_key,
            ),
            ModelSpec(
                provider="gemini",
                model=self.env.get("ORCH_FALLBACK_MODEL", DEFAULT_FALLBACK_MODEL),
                reasoning="low",
                api_key=self.gemini_api_key,
            ),
        ]

    def bridge_specs(self) -> list[ModelSpec]:
        """The free-tier chain for bridge tasks, cheapest first: local Ollama,
        Gemini's free tier (low reasoning), OpenRouter's free models. Every
        link is listed; the router skips the unconfigured ones."""
        return [
            ModelSpec(
                provider="ollama",
                model=self.env.get("ORCH_LOCAL_MODEL", DEFAULT_LOCAL_MODEL),
                base_url=self.ollama_host,
            ),
            ModelSpec(
                provider="gemini",
                model=self.env.get("ORCH_FALLBACK_MODEL", DEFAULT_FALLBACK_MODEL),
                reasoning="low",
                api_key=self.gemini_api_key,
            ),
            ModelSpec(
                provider="openrouter",
                model=self.env.get("ORCH_BRIDGE_MODEL", DEFAULT_BRIDGE_MODEL),
                api_key=self.openrouter_api_key,
            ),
        ]

    def named_specs(self) -> dict[str, ModelSpec]:
        """The distinct models the tiers are built from, by the names Epic 119's routing table
        (tools/orchestrator/routing.json) uses."""
        worker, fallback = self.worker_specs()
        local, _, bridge = self.bridge_specs()
        return {"supervisor": self.supervisor_spec(), "worker": worker, "fallback": fallback,
                "local": local, "bridge": bridge}

    def tier_table(self) -> dict[str, list[ModelSpec]]:
        return {
            "supervisor": [self.supervisor_spec()],
            "worker": self.worker_specs(),
            "bridge": self.bridge_specs(),
        }
