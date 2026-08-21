from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from workflow import (
    CONFIGURATIONS,
    DEFAULT_CONFIGURATION,
    DEFAULT_TARGET_FAMILY,
    CommandError,
    TARGET_FAMILIES,
    build_dir,
    clean_repo,
    cmake_build,
    cmake_configure,
    conan_install,
    run_command,
    safe_remove,
)


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGET = "HorizonExamples"
LOCAL_RECIPES = (
    "conan/recipes/ktm",
    "conan/recipes/pfr",
    "conan/recipes/slang",
    "conan/recipes/vulkan-memory-allocator",
)
RECIPE_TOGGLE_ENV = "HORIZON_CONAN_EXPORT_LOCAL_RECIPES"


TARGET_FAMILY_OPTIONS: dict[str, tuple[str, ...]] = {
    "core": ("&:with_tests=True",),
    "tools": ("&:with_tools=True",),
    "examples": ("&:with_examples=True",),
    "ocarina": ("&:with_ocarina=True", "&:with_cuda=True"),
    "ocarina-tests": (
        "&:with_ocarina=True",
        "&:with_cuda=True",
        "&:with_ocarina_tests=True",
    ),
    "vision-hotfix": (
        "&:with_ocarina=True",
        "&:with_cuda=True",
        "&:with_vision_hotfix=True",
    ),
}


def target_family_for_target(target: str) -> str:
    if target == "ShaderCompileScripts":
        return "tools"
    if target == "HorizonExamples":
        return "examples"
    if target.startswith("test-"):
        return "ocarina-tests"
    if target.startswith("horizon-hotfix"):
        return "vision-hotfix"
    if target in {"EABase", "EASTL", "mimalloc", "mimalloc-obj", "mimalloc-static"}:
        return "ocarina"
    if target == "copy_cuda_headers" or target.startswith("ocarina"):
        return "ocarina"
    return "core"


def target_family_for_targets(targets: list[str]) -> str:
    if not targets:
        return DEFAULT_TARGET_FAMILY

    families = {target_family_for_target(target) for target in targets}
    if len(families) != 1:
        choices = ", ".join(sorted(families))
        raise ValueError(
            f"Targets require different dependency families ({choices}). "
            "Configure and build one target family at a time."
        )
    return families.pop()


def conan_options(target_family: str) -> list[str]:
    try:
        return list(TARGET_FAMILY_OPTIONS[target_family])
    except KeyError as error:
        raise ValueError(f"Unsupported target family: {target_family}") from error


def install(configuration: str, target_family: str, *, update: bool = False) -> None:
    conan_install(
        REPO_ROOT,
        configuration,
        target_family=target_family,
        options=conan_options(target_family),
        recipes=LOCAL_RECIPES,
        recipe_toggle_env=RECIPE_TOGGLE_ENV,
        update=update,
    )


def execute(args: argparse.Namespace) -> None:
    targets = args.targets or [DEFAULT_TARGET]
    target = targets[0]
    configuration = args.configuration
    target_family = args.target_family or target_family_for_targets(targets)

    if args.command == "status":
        run_command(("git", "status", "--short", "--branch"), cwd=REPO_ROOT)
        run_command(("conan", "--version"), cwd=REPO_ROOT)
        run_command(("cmake", "--list-presets"), cwd=REPO_ROOT)
    elif args.command in {"install", "_bootstrap"}:
        install(configuration, target_family)
    elif args.command == "configure":
        install(configuration, target_family)
        cmake_configure(REPO_ROOT, configuration, target_family)
    elif args.command == "build":
        install(configuration, target_family)
        cmake_configure(REPO_ROOT, configuration, target_family)
        cmake_build(REPO_ROOT, configuration, target, target_family)
    elif args.command == "build-fast":
        cmake_build(REPO_ROOT, configuration, target, target_family)
    elif args.command == "rebuild":
        safe_remove(REPO_ROOT, build_dir(REPO_ROOT, configuration, target_family))
        install(configuration, target_family)
        cmake_configure(REPO_ROOT, configuration, target_family)
        cmake_build(REPO_ROOT, configuration, target, target_family)
    elif args.command == "update":
        install(configuration, target_family, update=True)
        cmake_configure(REPO_ROOT, configuration, target_family)
    elif args.command == "clean":
        clean_repo(REPO_ROOT)
    else:
        raise RuntimeError(f"Unsupported command: {args.command}")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Horizon developer workflow")
    parser.add_argument(
        "command",
        nargs="?",
        default="status",
        choices=("status", "install", "configure", "build", "build-fast", "rebuild", "update", "clean", "_bootstrap"),
    )
    parser.add_argument("targets", nargs="*")
    parser.add_argument(
        "--configuration",
        choices=CONFIGURATIONS,
        default=DEFAULT_CONFIGURATION,
    )
    parser.add_argument(
        "--target-family",
        choices=TARGET_FAMILIES,
        help="Configure a named target family instead of inferring it from the build target.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        execute(create_parser().parse_args(argv))
        return 0
    except CommandError as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return error.returncode
    except (OSError, RuntimeError, ValueError) as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
