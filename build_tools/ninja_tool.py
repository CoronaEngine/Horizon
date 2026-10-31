"""Reuse a working Ninja or install it into the current Conda environment."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from build_tools.conda_environment import conda_environment_lock, find_conda


def probe_ninja(executable: str | Path) -> bool:
    try:
        result = subprocess.run(
            [str(executable), "--version"], capture_output=True, text=True,
            check=True, timeout=30,
        )
        return re.fullmatch(r"\d+\.\d+(?:\.\d+)?(?:[.+-]\S+)?", result.stdout.strip()) is not None
    except (OSError, subprocess.SubprocessError):
        return False


def find_ninja(prefix: Path, *, cached: str | None = None, environment_only: bool = False) -> Path | None:
    candidates = []
    if not environment_only:
        for value in (cached, shutil.which("ninja")):
            if value:
                candidates.append(Path(shutil.which(value) or value))
    candidates.append(prefix / ("Library/bin/ninja.exe" if os.name == "nt" else "bin/ninja"))
    for candidate in dict.fromkeys(candidates):
        if candidate.is_file() and probe_ninja(candidate):
            return candidate.resolve()
    return None


def _use_ninja(executable: Path) -> Path:
    current = shutil.which("ninja")
    if current is None or Path(current).resolve() != executable:
        os.environ["PATH"] = str(executable.parent) + os.pathsep + os.environ.get("PATH", "")
    return executable


def ensure_ninja(cached: str | None = None) -> Path:
    prefix = Path(sys.prefix).resolve()
    executable = find_ninja(prefix, cached=cached)
    if executable is not None:
        return _use_ninja(executable)
    if not (prefix / "conda-meta").is_dir():
        raise RuntimeError(
            "Ninja is unavailable and this Python is not in a Conda environment. "
            "Run CMake configure or use conda run -n horizon-dev python build_tools/dev.py."
        )
    with conda_environment_lock(prefix):
        executable = find_ninja(prefix, cached=cached)
        if executable is None:
            command = [
                find_conda(prefix), "install", "--yes", "--prefix", str(prefix),
                "--override-channels", "--channel", "conda-forge", "--freeze-installed", "ninja",
            ]
            if any((prefix / "conda-meta").glob("ninja-[0-9]*.json")):
                command.append("--force-reinstall")
            print(f"[Ninja] Installing Ninja into {prefix}", file=sys.stderr, flush=True)
            try:
                subprocess.run(command, stdout=sys.stderr, check=True)
            except subprocess.CalledProcessError as error:
                raise RuntimeError(f"Automatic Ninja installation failed (exit code {error.returncode})") from error
            executable = find_ninja(prefix, environment_only=True)
            if executable is None:
                raise RuntimeError(f"Conda finished, but Ninja is not usable in {prefix}")
        return _use_ninja(executable)
