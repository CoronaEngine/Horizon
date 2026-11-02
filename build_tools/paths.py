"""Repository paths shared by Horizon's build entry points."""

from pathlib import Path


BUILD_TOOLS_ROOT = Path(__file__).resolve().parent
REPO_ROOT = BUILD_TOOLS_ROOT.parent
LOCAL_RECIPES = tuple(
    f"build_tools/conan/recipes/{name}"
    for name in ("ktm", "pfr", "slang", "vulkan-memory-allocator")
)
RECIPE_TOGGLE_ENV = "HORIZON_CONAN_EXPORT_LOCAL_RECIPES"


def conanfile_path(repo_root: Path) -> Path:
    return repo_root / "build_tools/conanfile.py"


def profiles_dir(repo_root: Path) -> Path:
    return repo_root / "build_tools/conan/profiles"
