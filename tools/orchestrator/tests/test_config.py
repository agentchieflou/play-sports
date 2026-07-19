"""Epic 135: .env parsing and tier-table tests (no network)."""

import tempfile
import unittest
from pathlib import Path

from tools.orchestrator.config import (
    DEFAULT_SUPERVISOR_MODEL,
    DEFAULT_WORKER_MODEL,
    OrchestratorConfig,
    load_env_file,
)


class LoadEnvFileTests(unittest.TestCase):
    def test_parses_values_comments_and_quotes(self):
        with tempfile.TemporaryDirectory() as tmp:
            env_path = Path(tmp) / ".env"
            env_path.write_text(
                "# comment\n"
                "GEMINI_API_KEY=abc123\n"
                "OPENROUTER_API_KEY=\"quoted\"\n"
                "\n"
                "MALFORMED LINE\n"
                "OLLAMA_HOST=http://localhost:11434\n",
                encoding="utf-8",
            )
            values = load_env_file(env_path)
        self.assertEqual(values["GEMINI_API_KEY"], "abc123")
        self.assertEqual(values["OPENROUTER_API_KEY"], "quoted")
        self.assertEqual(values["OLLAMA_HOST"], "http://localhost:11434")
        self.assertNotIn("MALFORMED LINE", values)

    def test_missing_file_returns_empty(self):
        self.assertEqual(load_env_file(Path("does/not/exist/.env")), {})


class TierTableTests(unittest.TestCase):
    def make_config(self, **env):
        with tempfile.TemporaryDirectory() as tmp:
            return OrchestratorConfig.load(repo_root=Path(tmp), environ=env)

    def test_environ_overrides_env_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / ".env").write_text("GEMINI_API_KEY=from-file\n",
                                            encoding="utf-8")
            config = OrchestratorConfig.load(
                repo_root=Path(tmp), environ={"GEMINI_API_KEY": "from-environ"})
        self.assertEqual(config.gemini_api_key, "from-environ")

    def test_default_tier_table_shape(self):
        config = self.make_config(GEMINI_API_KEY="g", OPENROUTER_API_KEY="o")
        table = config.tier_table()
        self.assertEqual(sorted(table), ["supervisor", "worker"])

        (supervisor,) = table["supervisor"]
        self.assertEqual(supervisor.provider, "gemini")
        self.assertEqual(supervisor.model, DEFAULT_SUPERVISOR_MODEL)
        self.assertEqual(supervisor.reasoning, "high")
        self.assertEqual(supervisor.api_key, "g")

        primary, fallback = table["worker"]
        self.assertEqual(primary.provider, "openrouter")
        self.assertEqual(primary.model, DEFAULT_WORKER_MODEL)
        self.assertEqual(primary.api_key, "o")
        self.assertEqual(fallback.provider, "gemini")
        self.assertEqual(fallback.reasoning, "low")
        self.assertEqual(fallback.api_key, "g")

    def test_model_overrides(self):
        config = self.make_config(ORCH_WORKER_MODEL="acme/other-model")
        self.assertEqual(config.worker_specs()[0].model, "acme/other-model")


if __name__ == "__main__":
    unittest.main()
