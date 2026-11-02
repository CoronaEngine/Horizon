"""Reuse Git when available, otherwise provision it in the current Conda env."""

import os
from pathlib import Path
import shutil
import subprocess
import sys

from build_tools.conda_environment import conda_environment_lock, find_conda


def find_git(prefix: Path, *, environment_only: bool = False) -> Path | None:
    candidates = []
    if not environment_only:
        executable = shutil.which("git")
        if executable:
            candidates.append(Path(executable))
    relative_paths = ("Library/bin/git.exe", "Library/cmd/git.exe") if os.name == "nt" else ("bin/git",)
    candidates.extend(prefix / relative for relative in relative_paths)
    for candidate in dict.fromkeys(candidates):
        if not candidate.is_file():
            continue
        try:
            result = subprocess.run(
                [str(candidate), "--version"], capture_output=True, text=True,
                check=True, timeout=30,
            )
            if result.stdout.startswith("git version "):
                return candidate.resolve()
        except (OSError, subprocess.SubprocessError):
            continue
    return None


def _use_git(executable: Path) -> Path:
    # Installation does not update a running process's PATH. Expose the chosen
    # executable to every Git subprocess, including Conan and recursive clones.
    current = shutil.which("git")
    if current is None or Path(current).resolve() != executable:
        os.environ["PATH"] = str(executable.parent) + os.pathsep + os.environ.get("PATH", "")
    return executable


def ensure_git() -> Path:
    prefix = Path(sys.prefix).resolve()
    executable = find_git(prefix)
    if executable is not None:
        return _use_git(executable)
    if not (prefix / "conda-meta").is_dir():
        raise RuntimeError(
            "Git is unavailable and this Python is not in a Conda environment. "
            "Run CMake configure or use conda run -n horizon-dev python build_tools/dev.py."
        )

    with conda_environment_lock(prefix):
        # A different preset may have installed Git while this process waited.
        executable = find_git(prefix)
        if executable is None:
            command = [
                find_conda(prefix), "install", "--yes", "--prefix", str(prefix),
                "--override-channels", "--channel", "conda-forge", "--freeze-installed", "git",
            ]
            if any((prefix / "conda-meta").glob("git-[0-9]*.json")):
                command.append("--force-reinstall")
            print(f"[Git] Installing Git into {prefix}", file=sys.stderr, flush=True)
            try:
                subprocess.run(command, stdout=sys.stderr, check=True)
            except subprocess.CalledProcessError as error:
                raise RuntimeError(f"Automatic Git installation failed (exit code {error.returncode})") from error
            executable = find_git(prefix, environment_only=True)
            if executable is None:
                raise RuntimeError(f"Conda finished, but Git is not usable in {prefix}")
        return _use_git(executable)
