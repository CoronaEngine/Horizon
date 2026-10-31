from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from build_tools.paths import REPO_ROOT, LOCAL_RECIPES, RECIPE_TOGGLE_ENV
from build_tools.dev import conan_options
from build_tools.git_tool import ensure_git
from build_tools.ninja_tool import ensure_ninja
from build_tools.workflow import (
    CONFIGURATIONS,
    DEFAULT_CONFIGURATION,
    CommandError,
    DEFAULT_TARGET_FAMILY,
    TARGET_FAMILIES,
    conan_install,
    conan_cache_lock,
    run_command,
)


def reference_like(value: str) -> bool:
    return any(marker in value for marker in "/@*#:")


def validate_reference_args(args: argparse.Namespace) -> None:
    if args.reference and args.package:
        raise RuntimeError("Use either --reference or package, not both.")
    if args.reference and any((args.version, args.user, args.channel)):
        raise RuntimeError("Do not combine --reference with --version, --user, or --channel.")
    if bool(args.user) != bool(args.channel):
        raise RuntimeError("--user and --channel must be provided together.")
    if not args.package and not args.reference and any((args.version, args.user, args.channel)):
        raise RuntimeError("Package is required when using --version, --user, or --channel.")
    if args.package and reference_like(args.package) and any((args.version, args.user, args.channel)):
        raise RuntimeError("Do not combine a reference-like package with version/user/channel.")
    if args.package_id and not args.package and not args.reference:
        raise RuntimeError("--package-id requires package or --reference.")


def cache_pattern(args: argparse.Namespace) -> str:
    validate_reference_args(args)
    if args.reference:
        pattern = args.reference
    elif not args.package:
        pattern = "*"
    elif reference_like(args.package):
        pattern = args.package
    else:
        pattern = f"{args.package}/{args.version or '*'}"
        if args.user and args.channel:
            pattern += f"@{args.user}/{args.channel}"
    if args.package_id:
        if ":" in pattern:
            raise RuntimeError("Do not pass --package-id when the reference already has a package section.")
        pattern += f":{args.package_id}"
    return pattern


def reference_name(value: str) -> str:
    name = value.split("/", 1)[0].split("@", 1)[0]
    if not name:
        raise RuntimeError(f"Could not derive package name from reference: {value}")
    return name


def concrete_reference(pattern: str) -> str | None:
    if any(marker in pattern for marker in "*#:"):
        return None
    head = pattern.split("@", 1)[0]
    return pattern if head.count("/") == 1 and all(head.split("/")) else None


def list_cache(args: argparse.Namespace) -> None:
    pattern = cache_pattern(args)
    command = ["conan", "list", pattern, "--cache"]
    if args.package_query:
        command.extend(("--package-query", args.package_query))
    run_command(command, cwd=REPO_ROOT)


def update_cache(args: argparse.Namespace) -> None:
    if args.package_id or args.package_query:
        raise RuntimeError("--package-id and --package-query are only supported by list/remove.")
    pattern = cache_pattern(args)
    concrete = concrete_reference(pattern)
    update_name = None
    if args.reference:
        update_name = reference_name(args.reference)
    elif args.package:
        update_name = reference_name(args.package)
    ensure_git()
    ensure_ninja()
    conan_install(
        REPO_ROOT, args.configuration, target_family=args.target_family,
        options=() if concrete else conan_options(args.target_family),
        recipes=LOCAL_RECIPES, recipe_toggle_env=RECIPE_TOGGLE_ENV,
        update=update_name or True, requires=concrete,
    )


def remove_cache(args: argparse.Namespace) -> None:
    if not args.package and not args.reference:
        raise RuntimeError("Refusing to remove without a package name or --reference.")
    pattern = cache_pattern(args)
    list_cache(args)
    if not args.force and not args.dry_run:
        answer = input(f"Type YES to remove global Conan cache entries matching '{pattern}': ")
        if answer != "YES":
            print("[SKIP] Remove cancelled.")
            return
    command = ["conan", "remove", pattern, "--confirm"]
    if args.package_query:
        command.extend(("--package-query", args.package_query))
    if args.dry_run:
        command.append("--dry-run")
    with conan_cache_lock(REPO_ROOT):
        run_command(command, cwd=REPO_ROOT)


def clear_cache(args: argparse.Namespace) -> None:
    if any((args.package, args.version, args.user, args.channel, args.reference, args.package_id, args.package_query)):
        raise RuntimeError("clear does not accept package/reference filters. Use remove instead.")
    command = ["conan", "remove", "*", "--confirm"]
    if args.dry_run:
        command.append("--dry-run")
    with conan_cache_lock(REPO_ROOT):
        run_command(command, cwd=REPO_ROOT)


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Horizon Conan cache maintenance")
    parser.add_argument("command", nargs="?", default="list", choices=("list", "update", "remove", "clear"))
    parser.add_argument("package", nargs="?")
    parser.add_argument("--version")
    parser.add_argument("--user")
    parser.add_argument("--channel")
    parser.add_argument("--reference")
    parser.add_argument("--package-id")
    parser.add_argument("--package-query")
    parser.add_argument("--configuration", choices=CONFIGURATIONS, default=DEFAULT_CONFIGURATION)
    parser.add_argument("--target-family", choices=TARGET_FAMILIES, default=DEFAULT_TARGET_FAMILY)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        args = create_parser().parse_args(argv)
        {"list": list_cache, "update": update_cache, "remove": remove_cache, "clear": clear_cache}[args.command](args)
        return 0
    except CommandError as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return error.returncode
    except (OSError, RuntimeError, ValueError) as error:
        print(f"[ERROR] {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
