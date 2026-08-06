from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path
from typing import Iterable, Mapping, Sequence


CONFIGURATIONS = ("Debug", "Release", "RelWithDebInfo", "MinSizeRel")
DEFAULT_CONFIGURATION = "RelWithDebInfo"
TARGET_FAMILIES = (
    "core",
    "tools",
    "examples",
    "ocarina",
    "ocarina-tests",
    "vision-hotfix",
)
DEFAULT_TARGET_FAMILY = "examples"
BOOTSTRAP_ENV = "CORONA_DEV_BOOTSTRAP_ACTIVE"


class CommandError(RuntimeError):
    def __init__(self, command: Sequence[str], returncode: int):
        super().__init__(f"Command failed with exit code {returncode}: {subprocess.list2cmdline(command)}")
        self.command = tuple(command)
        self.returncode = returncode


def configuration_slug(configuration: str) -> str:
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"Unsupported configuration: {configuration}")
    return configuration.lower()


def target_family_slug(target_family: str) -> str:
    if target_family not in TARGET_FAMILIES:
        raise ValueError(f"Unsupported target family: {target_family}")
    return target_family


def preset_name(target_family: str, configuration: str) -> str:
    return f"{target_family_slug(target_family)}-{configuration_slug(configuration)}"


def build_dir(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> Path:
    return repo_root / "build" / "conan" / target_family_slug(target_family) / configuration_slug(configuration)


def generators_dir(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> Path:
    return build_dir(repo_root, configuration, target_family) / "generators"


def profile_path(repo_root: Path, configuration: str) -> Path:
    return repo_root / "conan" / "profiles" / f"windows-msvc-{configuration_slug(configuration)}"


def run_command(
    command: Sequence[str | os.PathLike[str]],
    *,
    cwd: Path,
    env: Mapping[str, str] | None = None,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    args = [os.fspath(value) for value in command]
    print(f"+ {subprocess.list2cmdline(args)}", flush=True)
    result = subprocess.run(
        args,
        cwd=cwd,
        env=dict(env) if env is not None else None,
        text=True,
        capture_output=capture_output,
        check=False,
    )
    if result.returncode != 0:
        if capture_output:
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=os.sys.stderr)
        raise CommandError(args, result.returncode)
    return result


def export_local_recipes(repo_root: Path, recipes: Iterable[str], *, toggle_env: str) -> None:
    value = os.environ.get(toggle_env, "").strip().lower()
    if value in {"0", "false", "off", "no"}:
        print(f"[INFO] Skipping local Conan recipe exports because {toggle_env} is disabled.")
        return

    for relative_path in recipes:
        recipe_path = repo_root / relative_path
        if not recipe_path.is_file() and not (recipe_path / "conanfile.py").is_file():
            raise RuntimeError(f"Local Conan recipe was not found: {recipe_path}")
        run_command(("conan", "export", recipe_path), cwd=repo_root)


def conan_install(
    repo_root: Path,
    configuration: str,
    *,
    target_family: str,
    options: Iterable[str],
    recipes: Iterable[str],
    recipe_toggle_env: str,
    update: bool = False,
) -> None:
    export_local_recipes(repo_root, recipes, toggle_env=recipe_toggle_env)
    target_family = target_family_slug(target_family)
    profile = profile_path(repo_root, configuration)
    if not profile.is_file():
        raise RuntimeError(f"Conan profile was not found: {profile}")

    command: list[str | os.PathLike[str]] = [
        "conan",
        "install",
        ".",
        "-pr:a",
        profile,
        "-pr:b",
        profile,
        "-c:h",
        f"user.horizon:target_family={target_family}",
    ]
    for option in options:
        command.extend(("-o", option))
    command.append("--build=missing")
    if update:
        command.append("--update")
    run_command(command, cwd=repo_root)
    write_cmake_build_environment(repo_root, configuration, target_family)


def load_conan_build_environment(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> dict[str, str]:
    environment = dict(os.environ)
    batch_file = generators_dir(repo_root, configuration, target_family) / "conanbuild.bat"
    if not batch_file.is_file():
        raise RuntimeError(f"Conan build environment was not found: {batch_file}")

    command = f'call "{batch_file}" >nul && set'
    result = subprocess.run(
        command,
        cwd=repo_root,
        shell=True,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stderr:
            print(result.stderr, end="", file=os.sys.stderr)
        raise CommandError((command,), result.returncode)

    for line in result.stdout.splitlines():
        separator = line.find("=")
        if separator > 0:
            _set_environment_value(environment, line[:separator], line[separator + 1 :])
    return environment


def _set_environment_value(environment: dict[str, str], name: str, value: str) -> None:
    matches = [key for key in environment if key.casefold() == name.casefold()]
    canonical_name = matches[0] if matches else name
    for key in matches:
        del environment[key]
    environment[canonical_name] = value


def _environment_value(environment: Mapping[str, str], name: str) -> str | None:
    for key, value in environment.items():
        if key.casefold() == name.casefold():
            return value
    return None


def _cmake_bracket(value: str) -> str:
    equals = ""
    while f"]{equals}]" in value:
        equals += "="
    return f"[{equals}[{value}]{equals}]"


def write_cmake_build_environment(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> Path:
    environment = load_conan_build_environment(repo_root, configuration, target_family)
    output = generators_dir(repo_root, configuration, target_family) / "dev_build_environment.cmake"
    names = (
        "PATH", "INCLUDE", "LIB", "LIBPATH", "VCINSTALLDIR", "VCToolsInstallDir",
        "VSINSTALLDIR", "WindowsSdkDir", "WindowsSDKVersion", "UniversalCRTSdkDir", "UCRTVersion",
    )
    lines = ["# Generated by tools/workflow.py. Do not edit."]
    for name in names:
        value = _environment_value(environment, name)
        if value:
            lines.append(f"set(ENV{{{name}}} {_cmake_bracket(value)})")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


def cmake_configure(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> None:
    environment = load_conan_build_environment(repo_root, configuration, target_family)
    environment[BOOTSTRAP_ENV] = "1"
    run_command(("cmake", "--preset", preset_name(target_family, configuration)), cwd=repo_root, env=environment)


def _cache_value(cache_file: Path, name: str) -> str | None:
    prefix = f"{name}:"
    for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1]
    return None


def assert_cache_matches_repo(
    repo_root: Path,
    configuration: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> None:
    expected_build = build_dir(repo_root, configuration, target_family).resolve()
    cache_file = expected_build / "CMakeCache.txt"
    if not cache_file.is_file():
        raise RuntimeError(
            f"CMake cache was not found: {cache_file}. Run configure/build before build-fast."
        )

    source_dir = _cache_value(cache_file, "CMAKE_HOME_DIRECTORY")
    if source_dir and Path(source_dir).resolve() != repo_root.resolve():
        raise RuntimeError(
            f"CMake cache belongs to '{source_dir}', not '{repo_root}'. Run rebuild for this checkout."
        )

    cache_dir = _cache_value(cache_file, "CMAKE_CACHEFILE_DIR")
    if cache_dir and Path(cache_dir).resolve() != expected_build:
        raise RuntimeError(
            f"CMake cache directory is '{cache_dir}', not '{expected_build}'. Run rebuild."
        )


def cmake_build(
    repo_root: Path,
    configuration: str,
    target: str,
    target_family: str = DEFAULT_TARGET_FAMILY,
) -> None:
    assert_cache_matches_repo(repo_root, configuration, target_family)
    environment = load_conan_build_environment(repo_root, configuration, target_family)
    run_command(
        ("cmake", "--build", "--preset", preset_name(target_family, configuration), "--target", target),
        cwd=repo_root,
        env=environment,
    )


def safe_remove(repo_root: Path, path: Path) -> None:
    root = repo_root.resolve()
    target = path.resolve()
    if target == root or not target.is_relative_to(root):
        raise RuntimeError(f"Refusing to remove path outside repository root: {target}")
    if not target.exists():
        print(f"[INFO] Not found: {target.relative_to(root)}")
        return
    print(f"[INFO] Removing {target}")
    if target.is_dir() and not target.is_symlink():
        shutil.rmtree(target)
    else:
        target.unlink()


def clean_repo(repo_root: Path) -> None:
    for relative_path in ("build", "install", "out", "dist"):
        safe_remove(repo_root, repo_root / relative_path)
