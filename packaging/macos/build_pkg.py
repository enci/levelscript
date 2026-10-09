"""Build the macOS LevelScript installer (levelscript + levelscript-debugger).

Configures and builds an Apple Silicon (arm64) Release tree, stages a payload
that installs to /usr/local/levelscript (bin/levelscript, bin/levelscript-debugger,
bin/resources/, examples/, uninstall.sh) plus /etc/paths.d/levelscript, which puts
bin/ on every user's PATH, and wraps it in a .pkg with pkgbuild + productbuild.

The package is unsigned: the first time, users open it via System Settings >
Privacy & Security > Open Anyway. Installed files are not quarantined.

Requires CMake and Xcode (command line tools).

    python packaging/macos/build_pkg.py [--version 0.8.1] [--skip-build]

Output: dist/LevelScript-<version>-macos-arm64.pkg
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
APP_NAME = "LevelScript"
PKG_ID = "io.github.enci.levelscript"   # never change: pkgutil tracks upgrades by it
INSTALL_DIR = "usr/local/levelscript"
MIN_MACOS = "11.0"                      # first release on Apple Silicon
TARGETS = ("levelscript", "levelscript-debugger")


def default_version() -> str:
    text = (ROOT / "src" / "version.hpp").read_text("utf-8")
    parts = [re.search(rf"#define\s+LS_VERSION_{k}\s+(\d+)", text).group(1)
             for k in ("MAJOR", "MINOR", "PATCH")]
    return ".".join(parts)


def run(*cmd) -> None:
    subprocess.run([str(c) for c in cmd], check=True)


def build_release(build_dir: Path) -> None:
    run("cmake", "-S", ROOT, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_OSX_ARCHITECTURES=arm64",
        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MIN_MACOS}")
    run("cmake", "--build", build_dir, "--config", "Release", "--parallel",
        "--target", *TARGETS)


def find_exe(build_dir: Path, name: str) -> Path:
    # Multi-config generators emit app/Release/, single-config emit app/.
    for sub in ("app/Release", "app"):
        p = build_dir / sub / name
        if p.is_file():
            return p
    raise FileNotFoundError(f"{name} not found under {build_dir} (build it first)")


def stage(build_dir: Path, payload: Path) -> None:
    if payload.exists():
        shutil.rmtree(payload)
    app = payload / INSTALL_DIR
    (app / "bin").mkdir(parents=True)
    for name in TARGETS:
        shutil.copy2(find_exe(build_dir, name), app / "bin")
    # The debugger resolves fonts as <exe dir>/resources/...
    shutil.copytree(ROOT / "resources", app / "bin" / "resources")
    shutil.copytree(ROOT / "examples", app / "examples",
                    ignore=shutil.ignore_patterns("errors", "*.expected"))
    shutil.copy2(ROOT / "readme.md", app)
    uninstall = app / "uninstall.sh"
    uninstall.write_text((HERE / "uninstall.sh").read_text("utf-8")
                         .replace("@PKG_ID@", PKG_ID), encoding="utf-8")
    uninstall.chmod(0o755)
    # /etc is a symlink to /private/etc; write through the real path so the
    # payload doesn't try to replace the symlink with a directory.
    paths_d = payload / "private" / "etc" / "paths.d"
    paths_d.mkdir(parents=True)
    (paths_d / "levelscript").write_text(f"/{INSTALL_DIR}/bin\n", encoding="utf-8")


def render_distribution(version: str, component: str, work: Path) -> Path:
    tokens = {
        "APP_NAME": APP_NAME,
        "APP_VERSION": version,
        "PKG_ID": PKG_ID,
        "MIN_MACOS": MIN_MACOS,
        "COMPONENT_PKG": component,
    }
    text = (HERE / "distribution.xml.in").read_text("utf-8")
    for k, v in tokens.items():
        text = text.replace(f"@{k}@", v)
    dist = work / "distribution.xml"
    dist.write_text(text, encoding="utf-8")
    return dist


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Build the macOS LevelScript installer.")
    p.add_argument("--version", default=default_version())
    p.add_argument("--build-dir", type=Path, default=ROOT / "build-release")
    p.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    p.add_argument("--skip-build", action="store_true",
                   help="reuse the existing Release build in --build-dir")
    args = p.parse_args(argv)
    args.version = args.version.lstrip("v")

    if not args.skip_build:
        build_release(args.build_dir)

    work = args.output_dir / "_pkg"
    if work.exists():
        shutil.rmtree(work)
    payload = work / "payload"
    stage(args.build_dir, payload)

    scripts = work / "scripts"
    scripts.mkdir()
    shutil.copy2(HERE / "preinstall", scripts / "preinstall")
    (scripts / "preinstall").chmod(0o755)

    component = f"{PKG_ID}.pkg"
    run("pkgbuild", "--root", payload, "--identifier", PKG_ID,
        "--version", args.version, "--install-location", "/",
        "--scripts", scripts, work / component)

    resources = work / "resources"
    resources.mkdir()
    shutil.copy2(HERE / "conclusion.html", resources)

    installer = args.output_dir / f"{APP_NAME}-{args.version}-macos-arm64.pkg"
    run("productbuild", "--distribution", render_distribution(args.version, component, work),
        "--package-path", work, "--resources", resources, installer)
    print(f"Built {installer}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
