"""Tier -> client resolution with health checks and fallback (Epic 135).

Tiers come from config.OrchestratorConfig.tier_table():
  supervisor: gemini flash (high)
  worker:     openrouter gpt-oss-120b -> gemini flash (low)

Epic 119's MCP Model Router Service is specified to wrap this class.
"""

from __future__ import annotations

from ..config import ModelSpec, OrchestratorConfig
from .base import ChatResponse, Message, ModelClient, ProviderError, ToolSpec
from .gemini import GeminiClient
from .openrouter import OpenRouterClient


def build_client(spec: ModelSpec) -> ModelClient:
    if spec.provider == "gemini":
        return GeminiClient(model=spec.model, api_key=spec.api_key,
                            reasoning=spec.reasoning)
    if spec.provider == "openrouter":
        return OpenRouterClient(model=spec.model, api_key=spec.api_key)
    raise ValueError(f"unknown provider: {spec.provider!r}")


class ModelRouter:
    def __init__(self, config: OrchestratorConfig | None = None,
                 client_factory=build_client):
        self.config = config or OrchestratorConfig.load()
        self._factory = client_factory
        self._clients: dict[str, list[ModelClient]] = {}

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
        to the next. Clients with no API key configured are skipped."""
        errors: list[str] = []
        for client in self.clients(tier):
            if not client.api_key:
                errors.append(f"{client.label}: no API key configured")
                continue
            try:
                return client.chat(messages, tools=tools,
                                   temperature=temperature, max_tokens=max_tokens)
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
