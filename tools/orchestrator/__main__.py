"""CLI for the orchestrator: python -m tools.orchestrator <command>.

Commands: models/health (Epic 135), run (136), duel (137),
graph/status/resume/check-parallel (138).
"""

from __future__ import annotations

import argparse
import sys

from .config import REPO_ROOT, OrchestratorConfig


def cmd_models(config: OrchestratorConfig) -> int:
    for tier, specs in config.tier_table().items():
        print(f"{tier}:")
        for index, spec in enumerate(specs):
            role = "primary" if index == 0 else f"fallback[{index}]"
            key_state = "key set" if spec.api_key else "KEY MISSING"
            print(f"  {role}: {spec.label} [{key_state}]")
    return 0


def cmd_health(config: OrchestratorConfig) -> int:
    from .models.router import ModelRouter

    exit_code = 0
    for tier, results in ModelRouter(config).health().items():
        for label, healthy in results:
            state = "ok" if healthy else "UNAVAILABLE"
            print(f"{tier}: {label}: {state}")
            if not healthy:
                exit_code = 1
    return exit_code


def cmd_run(config: OrchestratorConfig, args: argparse.Namespace) -> int:
    from .models.router import ModelRouter
    from .worker.run import run_story

    class TierClient:
        """Adapter so the harness sees one client but gets router fallback."""

        def __init__(self, router: ModelRouter, tier: str):
            self._router = router
            self._tier = tier
            self.label = f"tier:{tier}"

        def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
            return self._router.chat(self._tier, messages, tools=tools,
                                     temperature=temperature,
                                     max_tokens=max_tokens)

    client = TierClient(ModelRouter(config), "worker")
    outcome = run_story(
        REPO_ROOT, args.story, client,
        branch=args.branch, specialization=args.specialization,
        dry_run=args.dry_run, max_iterations=args.max_iterations,
    )
    print(f"story {outcome.assignment.story_id}: {outcome.harness.status} "
          f"after {outcome.harness.iterations} iteration(s)")
    if outcome.harness.summary:
        print(f"summary: {outcome.harness.summary[:1000]}")
    print(f"transcript: {outcome.harness.transcript_path}")
    if args.dry_run and outcome.diff:
        print("--- diff (dry run, not pushed) ---")
        print(outcome.diff)
    if outcome.pr_url:
        print(f"PR: {outcome.pr_url}")
    return 0 if outcome.harness.status == "finished" else 1


def cmd_duel(config: OrchestratorConfig, args: argparse.Namespace) -> int:
    from .duel import run_duel
    from .models.router import build_client

    worker_specs = config.worker_specs()
    if len(worker_specs) < 2:
        print("duel needs two worker specs in the tier table")
        return 1
    worker_clients = [build_client(spec) for spec in worker_specs[:2]]
    judge_client = build_client(config.supervisor_spec())

    result = run_duel(
        REPO_ROOT, args.story, worker_clients, judge_client,
        specialization=args.specialization, dry_run=args.dry_run,
        max_iterations=args.max_iterations,
    )
    print(f"duel {result.duel_id}: winner={result.winner}")
    print(f"decision: {result.judge_reasoning[:500]}")
    print(f"record: {result.record_path}")
    if result.pr_url:
        print(f"PR: {result.pr_url}")
    return 0 if result.winner in ("a", "b") else 1


def _worker_client_factory(config: OrchestratorConfig):
    from .models.router import ModelRouter

    router = ModelRouter(config)

    class TierClient:
        label = "tier:worker"

        def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
            return router.chat("worker", messages, tools=tools,
                               temperature=temperature, max_tokens=max_tokens)

    return TierClient


def cmd_graph(config: OrchestratorConfig, args: argparse.Namespace,
              resume: bool = False) -> int:
    from .models.router import build_client
    from .supervisor.graph import GraphSupervisor
    from .supervisor.state import RunState

    state = None
    if resume:
        state = RunState.latest(REPO_ROOT / "eval" / "runs")
        if state is None:
            print("no run state to resume")
            return 1
        print(f"resuming {state.summary()}")

    supervisor_client = None
    if config.gemini_api_key:
        supervisor_client = build_client(config.supervisor_spec())

    tier_client_class = _worker_client_factory(config)
    supervisor = GraphSupervisor(
        REPO_ROOT,
        worker_client_factory=tier_client_class,
        supervisor_client=supervisor_client,
        max_workers=args.workers,
        dry_run=args.dry_run,
        state=state,
        max_dispatches=args.max_stories,
    )
    final_state = supervisor.run()
    print(final_state.summary())
    if supervisor.stop_reason:
        print(f"stopped: {supervisor.stop_reason}")
    print(f"state: {final_state.path}")
    return 0 if not final_state.data["escalations"] else 1


def cmd_status() -> int:
    from .supervisor.state import RunState

    state = RunState.latest(REPO_ROOT / "eval" / "runs")
    if state is None:
        print("no runs yet")
        return 0
    print(state.summary())
    for story_id, entry in sorted(state.data["stories"].items()):
        print(f"  {story_id}: {entry['status']} "
              f"(attempts={entry['attempts']}, pr={entry['pr'] or '-'})")
    for escalation in state.data["escalations"]:
        print(f"  ESCALATED {escalation.get('story')}: {escalation['reason']}")
    return 0


def cmd_check_parallel() -> int:
    from .supervisor.board import check_parallel

    problems = check_parallel(REPO_ROOT)
    if problems:
        for problem in problems:
            print(f"DRIFT: {problem}")
        return 1
    print("check-parallel: PARALLEL.md matches the roadmap crawl")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m tools.orchestrator",
        description="Agent orchestration graph (Track P)",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("models")
    subparsers.add_parser("health")
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--story", required=True,
                            help="story id like 12.5 (<epic>.<index>)")
    run_parser.add_argument("--branch")
    run_parser.add_argument("--specialization",
                            choices=["gameplay-cpp-story", "data-content-author",
                                     "ai-behavior-specialist"])
    run_parser.add_argument("--dry-run", action="store_true",
                            help="print the diff; no push, no PR")
    run_parser.add_argument("--max-iterations", type=int)
    duel_parser = subparsers.add_parser("duel")
    duel_parser.add_argument("--story", required=True,
                             help="story id like 12.5 (<epic>.<index>)")
    duel_parser.add_argument("--specialization",
                             choices=["gameplay-cpp-story", "data-content-author",
                                      "ai-behavior-specialist"])
    duel_parser.add_argument("--dry-run", action="store_true",
                             help="score and record; no push, no PR")
    duel_parser.add_argument("--max-iterations", type=int)
    for name in ("graph", "resume"):
        graph_parser = subparsers.add_parser(name)
        graph_parser.add_argument("--workers", type=int, default=2)
        graph_parser.add_argument("--dry-run", action="store_true")
        graph_parser.add_argument("--max-stories", type=int,
                                  help="stop after dispatching N stories")
    subparsers.add_parser("status")
    subparsers.add_parser("check-parallel")
    args = parser.parse_args(argv)

    if args.command == "status":
        return cmd_status()
    if args.command == "check-parallel":
        return cmd_check_parallel()

    config = OrchestratorConfig.load()
    if args.command == "models":
        return cmd_models(config)
    if args.command == "run":
        return cmd_run(config, args)
    if args.command == "duel":
        return cmd_duel(config, args)
    if args.command in ("graph", "resume"):
        return cmd_graph(config, args, resume=(args.command == "resume"))
    return cmd_health(config)


if __name__ == "__main__":
    sys.exit(main())
