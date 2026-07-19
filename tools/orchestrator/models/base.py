"""Provider-agnostic chat/tool-calling contract plus shared HTTP plumbing.

Stdlib HTTP only (urllib) - no SDK dependencies, per the Track P reality note.
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.request
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

RETRYABLE_STATUS = {408, 429, 500, 502, 503, 504}
DEFAULT_TIMEOUT_S = 120
DEFAULT_RETRIES = 3


class ProviderError(RuntimeError):
    """A provider call failed after retries (or non-retryably)."""

    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


@dataclass(frozen=True)
class ToolSpec:
    """A tool the model may call. `parameters` is a JSON-Schema object."""

    name: str
    description: str
    parameters: dict


@dataclass(frozen=True)
class ToolCall:
    id: str
    name: str
    arguments: dict


@dataclass(frozen=True)
class Message:
    """One turn of conversation.

    role: "system" | "user" | "assistant" | "tool"
    tool_calls: assistant turns that requested tool invocations
    tool_call_id/name: set on role="tool" result turns
    """

    role: str
    content: str = ""
    tool_calls: tuple[ToolCall, ...] = ()
    tool_call_id: str = ""
    name: str = ""


@dataclass
class Usage:
    prompt_tokens: int = 0
    completion_tokens: int = 0

    def add(self, other: "Usage") -> None:
        self.prompt_tokens += other.prompt_tokens
        self.completion_tokens += other.completion_tokens


@dataclass
class ChatResponse:
    text: str
    tool_calls: list[ToolCall] = field(default_factory=list)
    usage: Usage = field(default_factory=Usage)
    raw: dict = field(default_factory=dict)


def request_json(url: str, payload: dict, headers: dict[str, str],
                 retries: int = DEFAULT_RETRIES,
                 timeout_s: int = DEFAULT_TIMEOUT_S,
                 sleep=time.sleep) -> dict:
    """POST JSON, return parsed JSON; retry with exponential backoff on
    retryable HTTP statuses and network errors, honoring Retry-After."""
    body = json.dumps(payload).encode("utf-8")
    last_error: Exception | None = None
    for attempt in range(retries + 1):
        request = urllib.request.Request(
            url, data=body,
            headers={"Content-Type": "application/json", **headers},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout_s) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            detail = ""
            try:
                detail = error.read().decode("utf-8", "replace")[:500]
            except Exception:
                pass
            if error.code in RETRYABLE_STATUS and attempt < retries:
                retry_after = error.headers.get("Retry-After") if error.headers else None
                try:
                    delay = float(retry_after) if retry_after else 2.0 ** attempt
                except ValueError:
                    delay = 2.0 ** attempt
                sleep(min(delay, 60.0))
                last_error = error
                continue
            raise ProviderError(
                f"HTTP {error.code} from {url}: {detail}", status=error.code
            ) from error
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt < retries:
                sleep(2.0 ** attempt)
                last_error = error
                continue
            raise ProviderError(f"network error calling {url}: {error}") from error
    raise ProviderError(f"retries exhausted calling {url}: {last_error}")


class ModelClient(ABC):
    """One provider+model binding usable by any tier."""

    provider: str = ""

    def __init__(self, model: str, api_key: str):
        self.model = model
        self.api_key = api_key
        self.usage = Usage()  # cumulative across this client's lifetime

    @property
    def label(self) -> str:
        return f"{self.provider}:{self.model}"

    @abstractmethod
    def chat(self, messages: list[Message], tools: list[ToolSpec] | None = None,
             temperature: float = 0.2, max_tokens: int = 8192) -> ChatResponse:
        """Run one chat completion; implementations update self.usage."""

    def healthy(self) -> bool:
        """Cheap availability check: configured key + a minimal round-trip."""
        if not self.api_key:
            return False
        try:
            self.chat([Message(role="user", content="ping")], max_tokens=8)
            return True
        except ProviderError:
            return False
