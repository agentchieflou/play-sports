"""The worker loop (Epic 136): drive one model through the jailed tools until
it calls `finish` or exhausts its budget. Transcript persisted for scoring."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path

from ..models.base import ChatResponse, Message, ProviderError, Usage
from .prompts import StoryAssignment, build_system_prompt, build_task_prompt
from .tools import FINISH_TOOL, TOOL_SPECS, WorkerTools

DEFAULT_MAX_ITERATIONS = 40
DEFAULT_MAX_TOKENS_TOTAL = 400_000
NUDGE = ("Respond with tool calls only. Use the tools to work on the story, "
         f"or call {FINISH_TOOL} when it is complete and verified.")


@dataclass
class HarnessResult:
    status: str  # "finished" | "budget_exhausted" | "provider_error"
    summary: str = ""
    iterations: int = 0
    usage: Usage = field(default_factory=Usage)
    transcript_path: str = ""


class WorkerHarness:
    """One run = one model, one story, one worktree."""

    def __init__(self, client, tools: WorkerTools, repo_root: Path,
                 transcript_dir: Path | None = None,
                 max_iterations: int = DEFAULT_MAX_ITERATIONS,
                 max_tokens_total: int = DEFAULT_MAX_TOKENS_TOTAL):
        self.client = client
        self.tools = tools
        self.repo_root = Path(repo_root)
        self.transcript_dir = transcript_dir or (self.repo_root / "eval" / "transcripts")
        self.max_iterations = max_iterations
        self.max_tokens_total = max_tokens_total
        self.events: list[dict] = []

    # -- transcript ---------------------------------------------------------

    def _record(self, kind: str, **data) -> None:
        self.events.append({"t": time.strftime("%Y-%m-%dT%H:%M:%S"),
                            "kind": kind, **data})

    def _save_transcript(self, assignment: StoryAssignment,
                         result: HarnessResult) -> str:
        self.transcript_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        name = f"{assignment.story_id.replace('.', '-')}-{stamp}.json"
        path = self.transcript_dir / name
        payload = {
            "story": assignment.story_id,
            "story_text": assignment.story_text,
            "model": getattr(self.client, "label", "unknown"),
            "status": result.status,
            "summary": result.summary,
            "iterations": result.iterations,
            "usage": {"prompt_tokens": result.usage.prompt_tokens,
                      "completion_tokens": result.usage.completion_tokens},
            "events": self.events,
        }
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        return str(path)

    # -- the loop -----------------------------------------------------------

    def run(self, assignment: StoryAssignment) -> HarnessResult:
        messages = [
            Message(role="system",
                    content=build_system_prompt(self.repo_root, assignment)),
            Message(role="user", content=build_task_prompt(assignment)),
        ]
        result = HarnessResult(status="budget_exhausted")
        usage = Usage()
        self._record("start", story=assignment.story_id,
                     model=getattr(self.client, "label", "unknown"))

        for iteration in range(1, self.max_iterations + 1):
            result.iterations = iteration
            try:
                response: ChatResponse = self.client.chat(messages,
                                                          tools=TOOL_SPECS)
            except ProviderError as error:
                self._record("provider_error", error=str(error))
                result.status = "provider_error"
                result.summary = str(error)
                break
            usage.add(response.usage)

            if not response.tool_calls:
                self._record("nudge", text=response.text[:500])
                messages.append(Message(role="assistant", content=response.text))
                messages.append(Message(role="user", content=NUDGE))
                continue

            messages.append(Message(role="assistant", content=response.text,
                                    tool_calls=tuple(response.tool_calls)))
            finished = False
            for call in response.tool_calls:
                if call.name == FINISH_TOOL:
                    result.status = "finished"
                    result.summary = str(call.arguments.get("summary", ""))
                    self._record("finish", summary=result.summary[:2000])
                    finished = True
                    break
                output = self.tools.dispatch(call)
                self._record("tool", name=call.name,
                             arguments={k: str(v)[:300]
                                        for k, v in call.arguments.items()},
                             result=output[:1000])
                messages.append(Message(role="tool", content=output,
                                        tool_call_id=call.id, name=call.name))
            if finished:
                break
            if usage.prompt_tokens + usage.completion_tokens > self.max_tokens_total:
                self._record("budget", reason="token cap")
                break
        else:
            self._record("budget", reason="iteration cap")

        result.usage = usage
        result.transcript_path = self._save_transcript(assignment, result)
        return result
