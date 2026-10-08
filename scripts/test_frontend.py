#!/usr/bin/env python3
"""Check lex/parse/semantic manifest expectations against the C++ compiler CLI."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import time


STAGES = ("lex", "parse", "semantic")


def discover(root):
    cases = []
    for manifest in sorted(root.rglob("manifest.json")):
        entries = json.loads(manifest.read_text(encoding="utf-8"))
        for entry in entries:
            if entry.get("stage") not in STAGES:
                continue
            if type(entry.get("compilation_success")) is not bool:
                raise ValueError(f"{manifest}: invalid compilation_success")
            source = (manifest.parent / entry["source"]).resolve()
            if not source.is_file() or not source.is_relative_to(root):
                raise ValueError(f"{manifest}: invalid source {source}")
            cases.append({
                "source": str(source), "stage": entry["stage"],
                "expected_exit": 0 if entry["compilation_success"] else 1,
                "entry": entry.get("metadata", {}).get("entry", "crate"),
                "description": entry.get("description", ""),
            })
    for stage in STAGES:
        if not any(case["stage"] == stage for case in cases):
            raise ValueError(f"no {stage} testcases under {root}")
    return cases


def check(compiler, case, timeout):
    command = [str(compiler), "--stage", case["stage"]]
    if case["stage"] == "parse":
        command.extend(["--parse-entry", case["entry"]])
    command.append(case["source"])
    started = time.monotonic()
    result = {**case, "command": command, "passed": False}
    try:
        process = subprocess.run(command, capture_output=True, timeout=timeout)
        result.update(
            returncode=process.returncode,
            stdout=process.stdout.decode("utf-8", errors="replace"),
            stderr=process.stderr.decode("utf-8", errors="replace"),
            passed=process.returncode == case["expected_exit"],
        )
    except subprocess.TimeoutExpired:
        result.update(returncode=None, stdout="", stderr=f"timeout after {timeout}s")
    except OSError as error:
        result.update(returncode=None, stdout="", stderr=str(error))
    result["seconds"] = round(time.monotonic() - started, 4)
    return result


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--test-root", type=Path,
                        default=project.parent / "rx-compiler-testcases")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.test_root.resolve()
    if not compiler.is_file() or not root.is_dir():
        parser.error("compiler and testcase directory must exist")
    if args.jobs < 1 or args.timeout <= 0:
        parser.error("jobs and timeout must be positive")
    cases = discover(root)
    started = time.monotonic()
    results, summary = [], {}
    for stage in STAGES:
        selected = [case for case in cases if case["stage"] == stage]
        print(f"{stage}: running {len(selected)} cases", flush=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            checked = list(pool.map(lambda case: check(compiler, case, args.timeout), selected))
        results.extend(checked)
        failed = [result for result in checked if not result["passed"]]
        summary[stage] = {"total": len(checked), "passed": len(checked) - len(failed),
                          "failed": len(failed)}
        print(f"{stage}: {len(checked) - len(failed)}/{len(checked)} passed", flush=True)
        for result in failed:
            label = Path(result["source"]).relative_to(root)
            print(f"  FAIL {label}: expected {result['expected_exit']}, "
                  f"got {result['returncode']}", flush=True)
    report = {"compiler": str(compiler), "test_root": str(root), "summary": summary,
              "seconds": round(time.monotonic() - started, 3), "cases": results}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"report: {args.report.resolve()}")
    return 1 if any(result["failed"] for result in summary.values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
