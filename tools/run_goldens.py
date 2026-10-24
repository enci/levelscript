#!/usr/bin/env python3
"""Golden-output example runner — the determinism gate.

Runs every .lvs under tests/golden/ through the levelscript CLI with
its pinned seed (a `// @seed=N` directive, default 42) and its `// @param name=N`
values, and compares stdout against the .expected file next to it. Any change to
shuffle order or draw sequence fails loudly here. Every tests/errors/*.lvs must
fail to compile. Every file's `// @expect ...` directives must hold
(`levelscript --check`).

    python tools/run_goldens.py             # verify all
    python tools/run_goldens.py --update    # regenerate goldens (intended changes)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent   # the repository root


def find_cli() -> Path:
    candidates = [
        ROOT / "build" / sub / "app" / cfg / exe
        for sub in (".", "clion")
        for cfg in ("Debug", "Release", ".")
        for exe in ("levelscript.exe", "levelscript")
    ]
    for c in candidates:
        if c.is_file():
            return c
    sys.exit("levelscript not found — build first (cmake --build build)")


def check(cli: Path, ex: Path, name: str) -> bool:
    """The file's @expect directives hold (levelscript --check)."""
    run = subprocess.run([str(cli), "--check", str(ex)], capture_output=True, text=True)
    if run.returncode != 0:
        print(f"FAIL {name}: @expect\n{run.stdout}{run.stderr}", end="")
    return run.returncode == 0


def main() -> None:
    update = "--update" in sys.argv
    cli = find_cli()
    # tests/golden/programs/modules/ holds modules a sample uses, not programs of their own
    modules = ROOT / "tests" / "golden" / "programs" / "modules"
    goldens = sorted(p for p in (ROOT / "tests" / "golden").rglob("*.lvs")
                     if modules not in p.parents)
    errors = sorted((ROOT / "tests" / "errors").glob("*.lvs"))
    if not goldens and not errors:
        sys.exit("no goldens found")

    failed = 0

    # tests/errors/*.lvs must FAIL to compile (exit != 0, diagnostics on stderr)
    for ex in errors:
        run = subprocess.run([str(cli), "--seed", "1", str(ex)],
                             capture_output=True, text=True)
        if run.returncode == 0:
            print(f"FAIL errors/{ex.name}: compiled but should not")
            failed += 1
        elif "error" not in run.stderr:
            print(f"FAIL errors/{ex.name}: failed without a diagnostic")
            failed += 1
        elif not check(cli, ex, f"errors/{ex.name}"):
            failed += 1
        else:
            print(f"ok   errors/{ex.name}")

    for ex in goldens:
        name = ex.relative_to(ROOT).as_posix()
        src = ex.read_text(encoding="utf-8")
        m = re.search(r"//\s*@seed=(\d+)", src)
        seed = m.group(1) if m else "42"
        params = [a for p in re.findall(r"//\s*@param\s+(\w+=-?\d+)", src) for a in ("--param", p)]

        run = subprocess.run(
            [str(cli), "--seed", seed, *params, str(ex)],
            capture_output=True, text=True,
        )
        if run.returncode != 0:
            print(f"FAIL {name}: exit {run.returncode}\n{run.stderr}")
            failed += 1
            continue

        expected = ex.with_suffix(".expected")
        if update:
            expected.write_text(run.stdout, encoding="utf-8", newline="\n")
            print(f"UPDATED {name}")
        elif not expected.is_file():
            print(f"FAIL {name}: no .expected file (run with --update)")
            failed += 1
        elif run.stdout.replace("\r\n", "\n") != expected.read_text(encoding="utf-8"):
            print(f"FAIL {name}: output differs from {expected.name}")
            failed += 1
        elif not check(cli, ex, name):
            failed += 1
        else:
            print(f"ok   {name}")

    total = len(goldens) + len(errors)
    if failed:
        sys.exit(f"{failed}/{total} goldens failed")
    print(f"all {total} goldens pass")


if __name__ == "__main__":
    main()
