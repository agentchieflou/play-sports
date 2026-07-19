"""Environment and model-tier configuration for the orchestrator (Epic 135).

Reads the repo's .env contract (see .env.example / AGENTS.md):

    GEMINI_API_KEY     - supervisor tier (Gemini 3.5 Flash, high reasoning) and
                         the worker fallback (same key, low reasoning)
    OPENROUTER_API_KEY - primary worker tier (GPT-OSS-120B)
    OLLAMA_HOST        - reserved slot, not consumed yet (documented in the
                         Track P reality note)

Model IDs are overridable via optional env vars so a model rename never needs
a code change:

    ORCH_SUPERVISOR_MODEL (default: gemini-3.5-flash)
    ORCH_WORKER_MODEL     (default: openai/gpt-oss-120b)
    ORCH_FALLBACK_MODEL   (default: gemini-3.5-flash)
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

DEFAULT_SUPERVISOR_MODEL = "gemini-3.5-flash"
DEFAULT_WORKER_MODEL = "openai/gpt-oss-120b"
DEFAULT_FALLBACK_MODEL = "gemini-3.5-flash"


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

    provider: str  # "gemini" | "openrouter"
    model: str
    reasoning: str | None = None  # gemini thinking level: "high" | "low" | None
    api_key: str = ""

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

    def tier_table(self) -> dict[str, list[ModelSpec]]:
        return {
            "supervisor": [self.supervisor_spec()],
            "worker": self.worker_specs(),
        }
