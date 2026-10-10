"""Delegating one bridge task to a free-tier model (Core 25's .env model router).

`python -m tools.orchestrator delegate "<task>"` sends one prompt down a tier's
fallback chain and prints the answer. The default tier is "bridge": local
Ollama (OLLAMA_HOST), then Gemini's free tier (GEMINI_API_KEY), then
OpenRouter's free models (OPENROUTER_API_KEY). Any agent working with the
engine bridge, or Claude Code wanting a second opinion as AGENTS.md
describes, can hand off this way without an SDK.
"""

from __future__ import annotations

from dataclasses import dataclass

from .models.base import Message, ProviderError
from .models.router import ModelRouter

DEFAULT_TIER = "bridge"


@dataclass
class Delegation:
    ok: bool
    text: str
    model: str = ""   # the client that answered
    error: str = ""


def delegate(router: ModelRouter, prompt: str, tier: str = DEFAULT_TIER,
             system: str = "", max_tokens: int = 2048) -> Delegation:
    """One chat down `tier`'s chain. Never raises for a provider failure: the
    result says what went wrong, link by link."""
    if not prompt.strip():
        return Delegation(ok=False, text="", error="empty prompt")
    messages = []
    if system.strip():
        messages.append(Message(role="system", content=system))
    messages.append(Message(role="user", content=prompt))
    try:
        response = router.chat(tier, messages, max_tokens=max_tokens)
    except (ProviderError, KeyError) as error:
        return Delegation(ok=False, text="", error=str(error))
    return Delegation(ok=True, text=response.text, model=router.last_label)
