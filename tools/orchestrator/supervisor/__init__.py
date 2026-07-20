"""Supervisor graph mode (Epic 138): a Gemini-Flash supervisor dispatches N
concurrent workers on disjoint roadmap stories, guided by roadmap/PARALLEL.md
and guarded by code-enforced disjointness and one-story-one-run rules."""
