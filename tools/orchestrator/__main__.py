"""CLI for the orchestrator: python -m tools.orchestrator <command>.

Epic 135: `models`, `health`. Epic 136: `run`. Epic 137: `duel`.
Epic 138: `graph`, `status`, `resume`, `check-parallel`.
Core 25 (.env model router): `delegate`.
Epic 119 (model router service): `routes`, `route`, `mcp`.
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
            if spec.provider == "ollama":
                key_state = f"host {spec.base_url}" if spec.base_url else "HOST MISSING (OLLAMA_HOST)"
            else:
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
    from .models.router import ModelRouter, TierClient
    from .worker.run import run_story

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


def cmd_delegate(config: OrchestratorConfig, args: argparse.Namespace) -> int:
    from .delegate import delegate
    from .models.router import ModelRouter

    prompt = args.prompt if args.prompt is not None else sys.stdin.read()
    result = delegate(ModelRouter(config), prompt, tier=args.tier, system=args.system,
                      max_tokens=args.max_tokens)
    if not result.ok:
        print(f"delegate: {result.error}", file=sys.stderr)
        return 1
    print(result.text)
    print(f"[answered by {result.model}]", file=sys.stderr)
    return 0


def cmd_routes(config: OrchestratorConfig) -> int:
    """Each task's chain and each model's state; no network."""
    from .service import RouterService

    service = RouterService(config)
    problems = service.table.validate(set(config.named_specs()))
    for route in service.describe_routes():
        print(f"{route['task']} ({route['prefer']}, capability {route['min_capability']}+): {route['description']}")
        for index, link in enumerate(route["chain"]):
            state = "available" if link["available"] else "unavailable"
            if not link["configured"]:
                state = "not configured"
            print(f"  {index + 1}. {link['name']}: {link['label']} [{state}]")
    for problem in problems:
        print(f"routing.json: {problem}")
    return 1 if problems else 0


def cmd_route(config: OrchestratorConfig, args: argparse.Namespace) -> int:
    from .service import RouterService

    prompt = args.prompt if args.prompt is not None else sys.stdin.read()
    result = RouterService(config).complete(args.task, prompt=prompt, system=args.system,
                                            max_tokens=args.max_tokens)
    if not result.ok:
        print(f"route: {result.error}", file=sys.stderr)
        return 1
    print(result.text)
    print(f"[{args.task}: answered by {result.model}]", file=sys.stderr)
    return 0


def cmd_mcp(config: OrchestratorConfig) -> int:
    from .mcp_server import McpServer
    from .service import RouterService

    return McpServer(RouterService(config)).serve()


def cmd_check_parallel() -> int:
    from .supervisor.board import check_parallel, crawl, load_matrix

    problems = check_parallel(crawl(REPO_ROOT), load_matrix(REPO_ROOT))
    for problem in problems:
        print(problem)
    print(f"check-parallel: {len(problems)} problem(s)" if problems
          else "check-parallel: roadmap/PARALLEL.md matches the roadmap")
    return 1 if problems else 0


def cmd_status(args: argparse.Namespace) -> int:
    from .supervisor.state import RunState, RunStateError, default_run_dir

    try:
        state = RunState.load_run(default_run_dir(REPO_ROOT), args.run_id)
    except RunStateError as error:
        print(error)
        return 1
    print("\n".join(state.summary_lines()))
    return 0


def cmd_graph(config: OrchestratorConfig, args: argparse.Namespace) -> int:
    from .models.router import ModelRouter, TierClient, build_client
    from .supervisor.board import load_matrix
    from .supervisor.graph import GraphRunner, resume_cleanup
    from .supervisor.state import RunState, RunStateError, default_run_dir

    run_dir = default_run_dir(REPO_ROOT)
    try:
        if args.command == "resume":
            state = RunState.load_run(run_dir, args.run_id)
            reset = resume_cleanup(REPO_ROOT, state)
            print(f"resuming run {state.run_id}; reset to pending: {', '.join(reset) or 'none'}")
        else:
            state = RunState.create(run_dir, mode="graph",
                                    matrix_sha=load_matrix(REPO_ROOT).sha256,
                                    run_id=args.run_id)
            print(f"run {state.run_id}: {state.path}")
    except RunStateError as error:
        print(error)
        return 1

    supervisor_spec = config.supervisor_spec()
    supervisor = build_client(supervisor_spec) if supervisor_spec.api_key else None
    if supervisor is None:
        print("no GEMINI_API_KEY: the program's own order picks the stories")
    router = ModelRouter(config)
    runner = GraphRunner(
        REPO_ROOT, state, worker_client_factory=lambda assignment: TierClient(router, "worker"),
        supervisor_client=supervisor, max_workers=args.workers, dry_run=args.dry_run,
        max_stories=args.max_stories, max_iterations=args.max_iterations,
        allow_xl=args.allow_xl)
    runner.run()
    print("\n".join(state.summary_lines()))
    return 1 if state.halted else 0


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
        graph_parser.add_argument("--run-id", help="resume: the run (default: the latest)"
                                  if name == "resume" else "name for the new run")
        graph_parser.add_argument("--workers", type=int, default=2)
        graph_parser.add_argument("--max-stories", type=int,
                                  help="stop dispatching after this many stories")
        graph_parser.add_argument("--max-iterations", type=int)
        graph_parser.add_argument("--dry-run", action="store_true",
                                  help="diffs only; no push, no PR")
        graph_parser.add_argument("--allow-xl", action="store_true",
                                  help="dispatch XL epics' stories too")
    status_parser = subparsers.add_parser("status")
    status_parser.add_argument("--run-id", help="default: the latest run")
    subparsers.add_parser("check-parallel")
    delegate_parser = subparsers.add_parser(
        "delegate", help="send one task to a free-tier model and print the answer")
    delegate_parser.add_argument("prompt", nargs="?", help="the task (default: read stdin)")
    delegate_parser.add_argument("--tier", default="bridge",
                                 choices=["bridge", "worker", "supervisor"])
    delegate_parser.add_argument("--system", default="", help="an optional system prompt")
    delegate_parser.add_argument("--max-tokens", type=int, default=2048)
    subparsers.add_parser("routes", help="the routing table: each task's model chain (no network)")
    route_parser = subparsers.add_parser(
        "route", help="run one task through the model router service and print the answer")
    route_parser.add_argument("task", help="narration, summary, analysis, strategy or delegate")
    route_parser.add_argument("prompt", nargs="?", help="the request (default: read stdin)")
    route_parser.add_argument("--system", default="", help="an optional system prompt")
    route_parser.add_argument("--max-tokens", type=int, help="default: the task's")
    subparsers.add_parser("mcp", help="serve the model router service over MCP on stdio")
    args = parser.parse_args(argv)

    if args.command == "check-parallel":
        return cmd_check_parallel()
    if args.command == "status":
        return cmd_status(args)

    config = OrchestratorConfig.load()
    if args.command in ("graph", "resume"):
        return cmd_graph(config, args)
    if args.command == "delegate":
        return cmd_delegate(config, args)
    if args.command == "routes":
        return cmd_routes(config)
    if args.command == "route":
        return cmd_route(config, args)
    if args.command == "mcp":
        return cmd_mcp(config)
    if args.command == "models":
        return cmd_models(config)
    if args.command == "run":
        return cmd_run(config, args)
    if args.command == "duel":
        return cmd_duel(config, args)
    return cmd_health(config)


if __name__ == "__main__":
    sys.exit(main())
