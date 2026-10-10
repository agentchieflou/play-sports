"""Runs the content pipeline's steps inside the editor (Epic 146, Specs/ADR_Content_Pipeline.md).

The Content workflow (.github/workflows/content.yml) starts it headlessly:

  UnrealEditor-Cmd.exe play-sports.uproject -run=pythonscript -script=<repo>/tools/content_pipeline/run_in_editor.py
      -unattended -nullrhi -nosplash -nop4

That is the engine's Python commandlet, which runs the file through the Python Editor Script
Plugin's ExecPythonCommandEx: the same path Autonomix's run_python takes (Core 25.2).

Settings come from the environment, so nothing has to survive the editor's command-line quoting:

  PS_CONTENT_MODE    build (the default): run every step and save what changed, then record the
                     steps' sources and outputs in content.lock.json.
                     check: the drift check. Every step runs without changing anything and
                     reports what it would change; any difference fails the run.
  PS_CONTENT_STEPS   Comma-separated step ids to run (default: every step in pipeline.json).
  PS_CONTENT_REPORT  Where to write the JSON report (default Saved/ContentPipeline/report.json).

The report is written before the run ends. A failed step, or drift in check mode, then raises,
so the commandlet (and the editor process) exits non-zero.
"""

import importlib.util
import json
import os
import sys
import traceback

import unreal


def _pipeline_dir():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.join(project, "tools", "content_pipeline")


PIPELINE_DIR = _pipeline_dir()


def _load_module(name, path):
    """Loads a file as module name and registers it, so a step's "import ue_content" finds ours
    whatever else the editor's Python has on its path."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


pipeline = _load_module("pipeline", os.path.join(PIPELINE_DIR, "pipeline.py"))
ue_content = _load_module("ue_content", os.path.join(PIPELINE_DIR, "ue_content.py"))


def _load_step_module(repo, step):
    module = _load_module(f"ps_content_step_{step['Id']}", os.path.join(repo, step["Script"]))
    if not hasattr(module, "build"):
        raise RuntimeError(f"{step['Script']} has no build(ctx) function.")
    return module


def run():
    repo = os.path.dirname(os.path.dirname(PIPELINE_DIR))
    mode = os.environ.get("PS_CONTENT_MODE", "build").strip().lower() or "build"
    if mode not in ("build", "check"):
        raise RuntimeError(f"PS_CONTENT_MODE is '{mode}': it must be build or check.")
    report_path = os.environ.get("PS_CONTENT_REPORT") or os.path.join(repo, "Saved", "ContentPipeline", "report.json")
    selected = [s.strip() for s in os.environ.get("PS_CONTENT_STEPS", "").split(",") if s.strip()]

    config = pipeline.load_pipeline(repo)
    steps = config.get("Steps", [])
    unknown = [step_id for step_id in selected if pipeline.find_step(config, step_id) is None]
    if unknown:
        raise RuntimeError(f"Unknown step(s): {', '.join(unknown)}")
    if selected:
        steps = [step for step in steps if step["Id"] in selected]

    unreal.log(f"[ContentPipeline] {mode}: {', '.join(step['Id'] for step in steps)}")
    lock = pipeline.load_lock(repo)
    report = {"Mode": mode, "Succeeded": True, "Steps": []}
    for step in steps:
        ctx = ue_content.StepContext(step["Id"], dry_run=(mode == "check"), repo=repo,
                                     drift=pipeline.step_problems(repo, config, lock, step),
                                     recorded_outputs=sorted(lock["Steps"].get(step["Id"], {}).get("Outputs", {})))
        entry = {"Id": step["Id"], "Changes": ctx.changes, "Saved": ctx.saved, "Error": None}
        try:
            _load_step_module(repo, step).build(ctx)
            if mode == "build":
                lock["Steps"][step["Id"]] = pipeline.lock_entry(repo, config, step)
        except Exception:
            entry["Error"] = traceback.format_exc()
            report["Succeeded"] = False
            unreal.log_error(f"[ContentPipeline] {step['Id']} failed:\n{entry['Error']}")
        report["Steps"].append(entry)

    if mode == "build" and report["Succeeded"]:
        if pipeline.write_lock(repo, lock):
            unreal.log(f"[ContentPipeline] updated {pipeline.LOCK_FILE}")
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=4)
    unreal.log(f"[ContentPipeline] report: {report_path}")

    if not report["Succeeded"]:
        failed = [entry["Id"] for entry in report["Steps"] if entry["Error"]]
        raise RuntimeError(f"Content pipeline step(s) failed: {', '.join(failed)}")
    if mode == "check":
        drifted = [entry["Id"] for entry in report["Steps"] if entry["Changes"]]
        if drifted:
            raise RuntimeError(f"Content drift: {', '.join(drifted)} no longer match their sources.")


run()
