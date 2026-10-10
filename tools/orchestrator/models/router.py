"""Tier -> client resolution with health checks and fallback (Epic 135).

Tiers come from config.OrchestratorConfig.tier_table():
  supervisor: gemini flash (high)
  worker:     openrouter gpt-oss-120b -> gemini flash (low)
  bridge:     ollama (OLLAMA_HOST) -> gemini flash (low) -> openrouter free model

Epic 119's MCP Model Router Service is specified to wrap this class.
"""

from __future__ import annotations

from ..config import ModelSpec, OrchestratorConfig
from .base import ChatResponse, Message, ModelClient, ProviderError, ToolSpec
from .gemini import GeminiClient
from .ollama import OllamaClient
from .openrouter import OpenRouterClient


def build_client(spec: ModelSpec) -> ModelClient:
    if spec.provider == "gemini":
        return GeminiClient(model=spec.model, api_key=spec.api_key,
                            reasoning=spec.reasoning)
    if spec.provider == "openrouter":
        return OpenRouterClient(model=spec.model, api_key=spec.api_key)
    if spec.provider == "ollama":
        return OllamaClient(model=spec.model, host=spec.base_url)
    raise ValueError(f"unknown provider: {spec.provider!r}")


class ModelRouter:
    def __init__(self, config: OrchestratorConfig | None = None,
                 client_factory=build_client):
        self.config = config or OrchestratorConfig.load()
        self._factory = client_factory
        self._clients: dict[str, list[ModelClient]] = {}
        self.last_label = ""  # the client that answered the last chat

    def clients(self, tier: str) -> list[ModelClient]:
        """Ordered client chain for a tier (built lazily, cached)."""
        if tier not in self._clients:
            table = self.config.tier_table()
            if tier not in table:
                raise KeyError(f"unknown tier: {tier!r} (have {sorted(table)})")
            self._clients[tier] = [self._factory(spec) for spec in table[tier]]
        return self._clients[tier]

    def chat(self, tier: str, messages: list[Message],
             tools: list[ToolSpec] | None = None,
             temperature: float = 0.2, max_tokens: int = 8192) -> ChatResponse:
        """Try each client in the tier's chain; on ProviderError fall through
        to the next. Clients with no API key (or, for Ollama, no host) are
        skipped."""
        errors: list[str] = []
        for client in self.clients(tier):
            if not client.api_key:
                missing = ("no host configured (OLLAMA_HOST)" if getattr(client, "provider", "") == "ollama"
                           else "no API key configured")
                errors.append(f"{client.label}: {missing}")
                continue
            try:
                response = client.chat(messages, tools=tools,
                                       temperature=temperature, max_tokens=max_tokens)
                self.last_label = client.label
                return response
            except ProviderError as error:
                errors.append(f"{client.label}: {error}")
        raise ProviderError(
            f"all clients failed for tier {tier!r}: " + "; ".join(errors)
        )

    def health(self) -> dict[str, list[tuple[str, bool]]]:
        """Per-tier (client label, healthy) pairs. Makes one minimal live call
        per configured client - never called from tests or CI."""
        report: dict[str, list[tuple[str, bool]]] = {}
        for tier in self.config.tier_table():
            report[tier] = [(client.label, client.healthy())
                            for client in self.clients(tier)]
        return report


class TierClient:
    """One client for a tier with the router's fallback behind it, so the
    harness and the supervisor see a single ModelClient."""

    def __init__(self, router: ModelRouter, tier: str):
        self._router = router
        self._tier = tier
        self.label = f"tier:{tier}"

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        return self._router.chat(self._tier, messages, tools=tools,
                                 temperature=temperature, max_tokens=max_tokens)
