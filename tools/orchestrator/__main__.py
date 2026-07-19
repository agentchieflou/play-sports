"""CLI for the orchestrator: python -m tools.orchestrator <command>.

Epic 135 implements `models` and `health`; `run` (136), `duel` (137), and
`graph`/`status`/`resume`/`check-parallel` (138) are stubs until their epics
land.
"""

from __future__ import annotations

import argparse
import sys

from .config import OrchestratorConfig

PENDING = {
    "run": "Epic 136 (worker harness)",
    "duel": "Epic 137 (benchmark duel mode)",
    "graph": "Epic 138 (supervisor graph mode)",
    "status": "Epic 138 (supervisor graph mode)",
    "resume": "Epic 138 (supervisor graph mode)",
    "check-parallel": "Epic 138 (supervisor graph mode)",
}


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m tools.orchestrator",
        description="Agent orchestration graph (Track P)",
    )
    parser.add_argument("command",
                        choices=["models", "health", *PENDING])
    args, _rest = parser.parse_known_args(argv)

    if args.command in PENDING:
        print(f"'{args.command}' lands in {PENDING[args.command]} - not implemented yet.")
        return 2

    config = OrchestratorConfig.load()
    if args.command == "models":
        return cmd_models(config)
    return cmd_health(config)


if __name__ == "__main__":
    sys.exit(main())
