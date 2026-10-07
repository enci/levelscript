"""Build the Windows LevelScript installer (levelscript + levelscript-debugger).

Configures and builds a Release tree, stages a bundle
(bin\\levelscript.exe, bin\\levelscript-debugger.exe, bin\\resources\\, examples\\)
and wraps it in a per-user Inno Setup installer that puts bin\\ on the user PATH.

Requires CMake, a C++ toolchain, and Inno Setup 6.3+ (ISCC.exe).

    python packaging/build_installer.py [--version 0.7.0] [--skip-build]

Output: dist/LevelScript-setup-<version>-x64.exe
"""

from __future__ import annotations

import argparse
import re
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TEMPLATE = HERE / "levelscript_installer.iss.in"
APP_NAME = "LevelScript"
APP_ID = "293a058d-338e-42bb-8c1f-5580d855f9ef"   # never change: keeps upgrades in place
TARGETS = ("levelscript", "levelscript-debugger")


def default_version() -> str:
    text = (ROOT / "src" / "version.hpp").read_text("utf-8")
    parts = [re.search(rf"#define\s+LS_VERSION_{k}\s+(\d+)", text).group(1)
             for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def find_iscc() -> Path:
    env = os.environ.get("INNO_ISCC")
    if env and Path(env).is_file():
        return Path(env)
    found = shutil.which("iscc") or shutil.which("ISCC")
    if found:
        return Path(found)
    for base in (
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        os.environ.get("ProgramFiles", r"C:\Program Files"),
        os.path.expandvars(r"%LOCALAPPDATA%\Programs"),
    ):
        candidate = Path(base) / "Inno Setup 6" / "ISCC.exe"
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "ISCC.exe not found. Install Inno Setup 6.3+ "
        "(`winget install JRSoftware.InnoSetup`) or set INNO_ISCC."
    )


def run(*cmd) -> None:
    subprocess.run([str(c) for c in cmd], check=True)


def build_release(build_dir: Path) -> None:
    run("cmake", "-S", ROOT, "-B", build_dir)
    run("cmake", "--build", build_dir, "--config", "Release", "--parallel",
        "--target", *TARGETS)


def find_exe(build_dir: Path, name: str) -> Path:
    # Multi-config generators emit app/Release/, single-config emit app/.
    for sub in ("app/Release", "app"):
        p = build_dir / sub / f"{name}.exe"
        if p.is_file():
            return p
    raise FileNotFoundError(f"{name}.exe not found under {build_dir} (build it first)")


def stage(build_dir: Path, bundle: Path) -> None:
    if bundle.exists():
        shutil.rmtree(bundle)
    (bundle / "bin").mkdir(parents=True)
    for name in TARGETS:
        shutil.copy2(find_exe(build_dir, name), bundle / "bin")
    # The debugger resolves fonts as <exe dir>/resources/...
    shutil.copytree(ROOT / "resources", bundle / "bin" / "resources")
    shutil.copytree(ROOT / "examples", bundle / "examples",
                    ignore=shutil.ignore_patterns("errors", "*.expected"))
    shutil.copy2(ROOT / "readme.md", bundle)


def render_script(args, bundle: Path, basename: str, work: Path) -> Path:
    tokens = {
        "APP_NAME": APP_NAME,
        "APP_VERSION": args.version,
        "PUBLISHER": args.publisher,
        "APP_ID": APP_ID,
        "BUNDLE_DIR": str(bundle.resolve()),
        "OUTPUT_DIR": str(args.output_dir.resolve()),
        "OUTPUT_BASENAME": basename,
    }
    text = TEMPLATE.read_text("utf-8")
    for k, v in tokens.items():
        text = text.replace(f"@{k}@", v)
    work.mkdir(parents=True, exist_ok=True)
    script = work / "levelscript.iss"
    script.write_text(text, encoding="utf-8")
    return script


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Build the Windows LevelScript installer.")
    p.add_argument("--version", default=default_version())
    p.add_argument("--publisher", default="Bojan Endrovski")
    p.add_argument("--build-dir", type=Path, default=ROOT / "build-release")
    p.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    p.add_argument("--skip-build", action="store_true",
                   help="reuse the existing Release build in --build-dir")
    args = p.parse_args(argv)
    args.version = args.version.lstrip("v")

    iscc = find_iscc()   # fail fast, before a long build
    if not args.skip_build:
        build_release(args.build_dir)

    bundle = args.output_dir / "bundle"
    stage(args.build_dir, bundle)

    basename = f"{APP_NAME}-setup-{args.version}-x64"
    script = render_script(args, bundle, basename, args.output_dir / "_iss")
    run(iscc, "/Qp", script)

    installer = args.output_dir / f"{basename}.exe"
    if not installer.is_file():
        raise RuntimeError(f"ISCC reported success but {installer} is missing")
    print(f"Built {installer}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
