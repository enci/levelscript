#!/usr/bin/env python3
"""Golden-output example runner — the determinism gate.

Runs every examples/*.ls through lsc with its pinned seed (a `// @seed N`
comment, default 42) and compares stdout against the .expected file next to
it. Any change to shuffle order or draw sequence fails loudly here.

    python run_examples.py             # verify all
    python run_examples.py --update    # regenerate goldens (intended changes)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent


def find_lsc() -> Path:
    candidates = [
        ROOT / "build" / "app" / cfg / exe
        for cfg in ("Debug", "Release", ".")
        for exe in ("lsc.exe", "lsc")
    ]
    for c in candidates:
        if c.is_file():
            return c
    sys.exit("lsc not found — build first (cmake --build build)")


def main() -> None:
    update = "--update" in sys.argv
    lsc = find_lsc()
    examples = sorted((ROOT / "examples").glob("*.ls"))
    error_examples = sorted((ROOT / "examples" / "errors").glob("*.ls"))
    if not examples and not error_examples:
        sys.exit("no examples found")

    failed = 0

    # examples/errors/*.ls must FAIL to compile (exit != 0, diagnostics on stderr)
    for ex in error_examples:
        run = subprocess.run([str(lsc), "--seed", "1", str(ex)],
                             capture_output=True, text=True)
        if run.returncode == 0:
            print(f"FAIL errors/{ex.name}: compiled but should not")
            failed += 1
        elif "error" not in run.stderr:
            print(f"FAIL errors/{ex.name}: failed without a diagnostic")
            failed += 1
        else:
            print(f"ok   errors/{ex.name}")

    for ex in examples:
        src = ex.read_text(encoding="utf-8")
        m = re.search(r"@seed\s+(\d+)", src)
        seed = m.group(1) if m else "42"

        run = subprocess.run(
            [str(lsc), "--seed", seed, str(ex)],
            capture_output=True, text=True,
        )
        if run.returncode != 0:
            print(f"FAIL {ex.name}: exit {run.returncode}\n{run.stderr}")
            failed += 1
            continue

        expected = ex.with_suffix(".expected")
        if update:
            expected.write_text(run.stdout, encoding="utf-8", newline="\n")
            print(f"UPDATED {ex.name}")
        elif not expected.is_file():
            print(f"FAIL {ex.name}: no .expected file (run with --update)")
            failed += 1
        elif run.stdout.replace("\r\n", "\n") != expected.read_text(encoding="utf-8"):
            print(f"FAIL {ex.name}: output differs from {expected.name}")
            failed += 1
        else:
            print(f"ok   {ex.name}")

    total = len(examples) + len(error_examples)
    if failed:
        sys.exit(f"{failed}/{total} examples failed")
    print(f"all {total} examples pass")


if __name__ == "__main__":
    main()
