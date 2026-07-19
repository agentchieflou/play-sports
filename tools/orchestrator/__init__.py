"""Agent orchestration graph (Track P, Epics 135-138).

Turns the prose six-role pipeline (GEMINI.md, .agents/skills/) into a runnable
program: model clients honoring the .env contract (Epic 135), a worker harness
over isolated git worktrees (Epic 136), benchmark duels (Epic 137), and a
supervisor graph consuming roadmap/PARALLEL.md (Epic 138).

Stdlib-only by design, like the rest of tools/.
"""

__all__ = ["config"]
