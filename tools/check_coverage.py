#!/usr/bin/env python3
"""Check coverage: every compile-time check has a negative test.

Reads the checks from docs/spec.md section 7.3 (retired ones excluded), and
the checks the tests name: a `// check N` or `// checks N, M` comment in a
tests/*.cpp test, and the `check N` in an examples/errors/*.lvs header line.
Lists each check with where it is tested; fails when a check has no test.

    python tools/check_coverage.py           # fail on any uncovered check
    python tools/check_coverage.py --list    # also print the coverage matrix
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def spec_checks() -> dict:
    spec = (ROOT / "docs" / "spec.md").read_text(encoding="utf-8")
    section = spec[spec.index("### 7.3"):spec.index("### 7.4")]
    checks = {}
    for num, title, rest in re.findall(r"^(\d+)\. \*\*(.+?)\*\*(.*)$", section, re.M):
        if not rest.strip().startswith("Retired"):
            checks[int(num)] = title.rstrip(".")
    return checks


def numbers(text: str) -> list:
    return [int(n) for n in re.findall(r"\d+", text)]


def tested() -> dict:
    where = {}
    for f in sorted((ROOT / "tests").glob("*.cpp")):
        src = f.read_text(encoding="utf-8")
        for name, body in re.findall(r'^TEST_CASE\("([^"]*)"\)(.*?)(?=^TEST_CASE|\Z)', src, re.M | re.S):
            for tag in re.findall(r"^\s*// checks? ([0-9][0-9, ]*)$", body, re.M):
                for n in numbers(tag):
                    where.setdefault(n, []).append(f"{f.name}: {name}")
    for f in sorted((ROOT / "examples" / "errors").glob("*.lvs")):
        header = f.read_text(encoding="utf-8").splitlines()[0]
        for tag in re.findall(r"checks? ([0-9][0-9, and]*)", header):
            for n in numbers(tag):
                where.setdefault(n, []).append(f"errors/{f.name}")
    return where


def main() -> None:
    checks, where = spec_checks(), tested()
    missing = [n for n in checks if n not in where]
    unknown = sorted(n for n in where if n not in checks)
    if "--list" in sys.argv:
        for n, title in checks.items():
            print(f"{n:3} {title}")
            for w in where.get(n, ["(no test)"]):
                print(f"      {w}")
    for n in missing:
        print(f"MISSING check {n}: {checks[n]}")
    for n in unknown:
        print(f"UNKNOWN check {n}: named by {', '.join(where[n])}")
    covered = len(checks) - len(missing)
    print(f"{covered} of {len(checks)} checks have a negative test")
    sys.exit(1 if missing or unknown else 0)


if __name__ == "__main__":
    main()
