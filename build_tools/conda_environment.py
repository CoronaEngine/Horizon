"""Locate Conda and serialize changes to the selected Python environment."""

import os
from pathlib import Path
import shutil

from build_tools.file_lock import file_lock


def conda_environment_lock(prefix: Path, timeout: float = 600):
    # Use CoronaEngine's lock name too: both repos can share one Conda prefix.
    return file_lock(prefix / "conda-meta" / "corona-tools.lock", timeout)


def find_conda(prefix: Path) -> str:
    candidates = [os.environ.get("CONDA_EXE")]
    # Also support invoking an environment's Python by absolute path without
    # activating it. Named environments normally live under <base>/envs/.
    base = prefix.parent.parent if prefix.parent.name == "envs" else prefix
    candidates.extend(str(base / relative) for relative in (
        "Scripts/conda.exe", "condabin/conda.bat", "bin/conda",
    ))
    candidates.append(shutil.which("conda"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate).resolve())
    raise RuntimeError("Conda was not found. Run build_tools/dev.py through conda run or activate its environment.")
