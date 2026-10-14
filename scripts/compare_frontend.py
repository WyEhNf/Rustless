#!/usr/bin/env python3
"""Compare frontend behavior, diagnostics and dumps before and after refactoring."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess

from test_frontend import discover


def observe(compiler, case, timeout):
    command = [str(compiler), "--stage", case["stage"]]
    if case["stage"] == "parse":
        command.extend(["--parse-entry", case["entry"]])
        if case["entry"] == "crate":
            command.append("--dump-ast")
    elif case["stage"] == "semantic":
        command.extend(["--dump-ast", "--dump-typed"])
    command.append(case["source"])
    process = subprocess.run(command, capture_output=True, timeout=timeout)
    return process.returncode, process.stdout, process.stderr


def main():
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--test-root", type=Path,
                        default=project.parent / "rx-compiler-testcases")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=30)
    args = parser.parse_args()
    baseline, candidate = args.baseline.resolve(), args.candidate.resolve()
    root = args.test_root.resolve()
    if not baseline.is_file() or not candidate.is_file() or not root.is_dir():
        parser.error("both compilers and the testcase directory must exist")
    if args.jobs < 1 or args.timeout <= 0:
        parser.error("jobs and timeout must be positive")
    cases = discover(root)

    def compare(case):
        try:
            before = observe(baseline, case, args.timeout)
            after = observe(candidate, case, args.timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            return case, str(error)
        names = ("exit status", "stdout/dumps", "stderr/diagnostics")
        changed = [name for name, left, right in zip(names, before, after)
                   if left != right]
        return case, ", ".join(changed)

    failed = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for number, (case, difference) in enumerate(pool.map(compare, cases), 1):
            if difference:
                failed += 1
                print(f"DIFF {case['stage']} {case['source']}: {difference}",
                      flush=True)
            if number % 100 == 0:
                print(f"compared {number}/{len(cases)}", flush=True)
    print(f"identical: {len(cases) - failed}/{len(cases)}; different: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
