from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from workflow import (
    CONFIGURATIONS,
    DEFAULT_CONFIGURATION,
    CommandError,
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


def conan_options(targets: list[str]) -> list[str]:
    options: list[str] = []
    requires_ocarina = False
    requires_ocarina_tests = False
    requires_ocarina_vulkan = False
    requires_vision_hotfix = False

    if "ShaderCompileScripts" in targets:
        options.append("&:with_tools=True")
    if "HorizonExamples" in targets:
        options.append("&:with_examples=True")
    if "HorizonTests" in targets:
        options.append("&:with_tests=True")
    if "HorizonSmokeBenchmarks" in targets:
        options.append("&:with_benchmarks=True")

    for target in targets:
        if target.startswith("ocarina"):
            requires_ocarina = True
        if target.startswith("ocarina-test-"):
            requires_ocarina_tests = True
        if target == "ocarina-backend-vulkan":
            requires_ocarina_vulkan = True
        if target.startswith("vision-hotfix"):
            requires_ocarina = True
            requires_vision_hotfix = True

    if requires_ocarina:
        options.extend(("&:with_ocarina=True", "&:with_cuda=True"))
    if requires_ocarina_tests:
        options.append("&:with_ocarina_tests=True")
    if requires_ocarina_vulkan:
        options.append("&:with_ocarina_vulkan=True")
    if requires_vision_hotfix:
        options.append("&:with_vision_hotfix=True")
    return options


def install(configuration: str, targets: list[str], *, update: bool = False) -> None:
    conan_install(
        REPO_ROOT,
        configuration,
        options=conan_options(targets),
        recipes=LOCAL_RECIPES,
        recipe_toggle_env=RECIPE_TOGGLE_ENV,
        update=update,
    )


def execute(args: argparse.Namespace) -> None:
    targets = args.targets or [DEFAULT_TARGET]
    target = targets[0]
    configuration = args.configuration

    if args.command == "status":
        run_command(("git", "status", "--short", "--branch"), cwd=REPO_ROOT)
        run_command(("conan", "--version"), cwd=REPO_ROOT)
        run_command(("cmake", "--list-presets"), cwd=REPO_ROOT)
    elif args.command in {"install", "_bootstrap"}:
        install(configuration, targets)
    elif args.command == "configure":
        install(configuration, targets)
        cmake_configure(REPO_ROOT, configuration)
    elif args.command == "build":
        install(configuration, targets)
        cmake_configure(REPO_ROOT, configuration)
        cmake_build(REPO_ROOT, configuration, target)
    elif args.command == "build-fast":
        cmake_build(REPO_ROOT, configuration, target)
    elif args.command == "rebuild":
        safe_remove(REPO_ROOT, build_dir(REPO_ROOT, configuration))
        install(configuration, targets)
        cmake_configure(REPO_ROOT, configuration)
        cmake_build(REPO_ROOT, configuration, target)
    elif args.command == "update":
        install(configuration, targets, update=True)
        cmake_configure(REPO_ROOT, configuration)
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
