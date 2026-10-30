from __future__ import annotations

import json
import locale
import multiprocessing
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools.paths import REPO_ROOT

from build_tools import workflow as workflow_module
from build_tools.workflow import (
    DEFAULT_TARGET_FAMILY,
    _cmake_bracket,
    _environment_value,
    _set_environment_value,
    build_dir,
    configuration_slug,
    preset_name,
    safe_remove,
    target_family_slug,
)
from build_tools import dev
from build_tools.dev import conan_options, target_family_for_target, target_family_for_targets


def _concurrent_install_worker(repo_root, cache, family, hold_stage, entered, release, ready, results):
    """Exercise the real install transaction with a deliberately non-concurrent cache."""
    active = cache / "active-export"
    owns_marker = False

    def hold(stage):
        if stage == hold_stage:
            entered.set()
            if not release.wait(10):
                raise RuntimeError("Timed out waiting for the test to release the install")

    def command(args, **kwargs):
        nonlocal owns_marker
        if tuple(args[:3]) == ("conan", "config", "home"):
            return subprocess.CompletedProcess(args, 0, stdout=str(cache))
        if tuple(args[:2]) == ("conan", "export"):
            active.mkdir()  # A second writer must not enter until the whole install finishes.
            owns_marker = True
            hold("export")
        elif tuple(args[:2]) == ("conan", "install"):
            hold("install")
            if hold_stage == "failure":
                raise RuntimeError("Simulated Conan failure")
        return subprocess.CompletedProcess(args, 0)

    def write_environment(*args):
        nonlocal owns_marker
        hold("environment")
        active.rmdir()
        owns_marker = False

    try:
        with (
            patch.object(workflow_module, "run_command", side_effect=command),
            patch.object(workflow_module, "write_cmake_build_environment", side_effect=write_environment),
        ):
            ready.set()
            workflow_module.conan_install(
                repo_root, "Debug", target_family=family, options=(),
                recipes=("recipe",), recipe_toggle_env="HORIZON_TEST_RECIPES",
            )
        results.put(None)
    except Exception as error:
        results.put(str(error))
    finally:
        if owns_marker:
            active.rmdir()


class WorkflowTests(unittest.TestCase):
    def test_build_tool_uses_configured_environment_and_preserves_arguments(self) -> None:
        launcher = REPO_ROOT / "build_tools/scripts/run_build_tool.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            environment = root / "build environment.json"
            environment.write_text(json.dumps({'LIB': 'configured;libraries'}), encoding="utf-8")
            probe = root / "probe.py"
            probe.write_text(
                "import os, sys\n"
                "assert os.environ['LIB'] == 'configured;libraries'\n"
                "assert sys.argv[1:] == ['space in argument', 'semi;colon', 'quote\\\"value', 'trailing\\\\', 'close]=]bracket']\n"
                "print('configured environment reached build tool')\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(launcher), str(environment), "", sys.executable, str(probe), "space in argument", "semi;colon", 'quote"value',
                 "trailing\\", "close]=]bracket"],
                env={**os.environ, "LIB": "stale-ide-libraries"}, capture_output=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(b"configured environment reached build tool", result.stdout + result.stderr)

    @unittest.skipUnless(sys.platform == "win32", "MSVC output encoding is Windows-specific")
    def test_build_tool_normalizes_includes_without_hiding_compiler_failure(self) -> None:
        launcher = REPO_ROOT / "build_tools/scripts/run_build_tool.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            environment = root / "environment.json"
            environment.write_text("{}", encoding="utf-8")
            prefix_text = "注意: 包含文件:  "
            try:
                prefix_text.encode(locale.getencoding())
            except UnicodeEncodeError:
                prefix_text = "Hinweis: Einlesen der Datei:  "
            prefix = prefix_text.encode("utf-8").hex()
            probe = root / "compiler.py"
            probe.write_text(
                "import ctypes, sys\n"
                "cp = ctypes.windll.kernel32.GetConsoleOutputCP() or ctypes.windll.kernel32.GetACP()\n"
                f"sys.stdout.buffer.write(({prefix_text!r} + ' D:/project/header.h\\n').encode(f'cp{{cp}}'))\n"
                "print('error C2146: intentional failure', file=sys.stderr)\n"
                "sys.exit(2)\n", encoding="utf-8")
            for flags in (0, subprocess.CREATE_NO_WINDOW):
                with self.subTest(no_console=bool(flags)):
                    result = subprocess.run(
                        [sys.executable, str(launcher), str(environment), prefix, sys.executable, str(probe)], capture_output=True, creationflags=flags,
                    )
                    output = result.stdout + result.stderr
                    self.assertEqual(result.returncode, 2)
                    self.assertIn(b"Note: including file: D:/project/header.h", output)
                    self.assertIn(b"error C2146: intentional failure", output)

    def test_concurrent_installs_serialize_the_complete_cache_transaction(self) -> None:
        context = multiprocessing.get_context("spawn")
        for stage in ("export", "install", "environment"):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                cache = root / "cache"
                cache.mkdir()
                processes = []
                results = context.Queue()
                release = context.Event()
                entered = context.Event()
                try:
                    for index, family in enumerate(("examples", "core")):
                        repo = root / f"repo-{index}"
                        profile = repo / "build_tools/conan/profiles/windows-msvc-debug"
                        profile.parent.mkdir(parents=True)
                        profile.touch()
                        (repo / "recipe").mkdir()
                        (repo / "recipe/conanfile.py").touch()
                        ready = context.Event()
                        process = context.Process(
                            target=_concurrent_install_worker,
                            args=(repo, cache, family, stage if index == 0 else "", entered,
                                  release, ready, results),
                        )
                        processes.append(process)
                        process.start()
                        self.assertTrue(ready.wait(10), "Install worker did not start")
                        if index == 0:
                            self.assertTrue(entered.wait(10), "First install did not reach the requested stage")
                    # Give the second process a chance to collide while the first owns the cache.
                    processes[1].join(0.5)
                finally:
                    release.set()
                    for process in processes:
                        process.join(10)
                        if process.is_alive():
                            process.terminate()
                            process.join()
                self.assertEqual([process.exitcode for process in processes], [0, 0])
                self.assertEqual([results.get(timeout=2) for _ in processes], [None, None])
                results.close()

    def test_install_failure_releases_the_cache_for_another_process(self) -> None:
        context = multiprocessing.get_context("spawn")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            cache.mkdir()
            profile = root / "build_tools/conan/profiles/windows-msvc-debug"
            profile.parent.mkdir(parents=True)
            profile.touch()
            (root / "recipe").mkdir()
            (root / "recipe/conanfile.py").touch()
            results = context.Queue()
            for stage, expected in (("failure", "Simulated Conan failure"), ("", None)):
                process = context.Process(
                    target=_concurrent_install_worker,
                    args=(root, cache, "core", stage, context.Event(), context.Event(),
                          context.Event(), results),
                )
                process.start()
                process.join(10)
                if process.is_alive():
                    process.terminate()
                    process.join()
                self.assertEqual(process.exitcode, 0)
                self.assertEqual(results.get(timeout=2), expected)
            results.close()

    def test_posix_conan_install_uses_multi_config_layout(self) -> None:
        with (
            tempfile.TemporaryDirectory() as cache,
            patch.object(workflow_module.platform, "system", return_value="Linux"),
            patch.object(workflow_module, "run_command", return_value=subprocess.CompletedProcess(
                ("conan", "config", "home"), 0, stdout=cache,
            )) as run_command,
            patch.object(workflow_module, "write_cmake_build_environment"),
        ):
            workflow_module.conan_install(
                Path("/repo"),
                "Debug",
                target_family="core",
                options=(),
                recipes=(),
                recipe_toggle_env="HORIZON_TEST_RECIPES",
            )

        install_command = next(
            call.args[0]
            for call in run_command.call_args_list
            if tuple(call.args[0][:2]) == ("conan", "install")
        )
        generator_index = install_command.index("-c:a")
        self.assertEqual(
            install_command[generator_index + 1],
            "tools.cmake.cmaketoolchain:generator=Ninja Multi-Config",
        )

    def test_conan_profiles_are_native_to_the_host(self) -> None:
        self.assertTrue(hasattr(workflow_module, "conan_profile"))
        conan_profile = workflow_module.conan_profile
        root = Path("C:/repo")
        self.assertEqual(
            conan_profile(root, "Debug", "Windows"),
            str(root / "build_tools" / "conan" / "profiles" / "windows-msvc-debug"),
        )
        self.assertEqual(conan_profile(root, "Debug", "Linux"), "horizon-linux-debug")
        self.assertEqual(conan_profile(root, "Release", "Darwin"), "horizon-darwin-release")

    def test_json_environment_is_parsed_without_losing_values(self) -> None:
        self.assertTrue(hasattr(workflow_module, "_parse_json_environment"))
        parse_json_environment = workflow_module._parse_json_environment
        environment = parse_json_environment(
            '{"PATH": "/usr/bin", "TOKEN": "left=right", "MULTILINE": "a\\nb"}'
        )
        self.assertEqual(environment["TOKEN"], "left=right")
        self.assertEqual(environment["MULTILINE"], "a\nb")

    def test_core_test_workflow_covers_all_host_platforms(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/core-tests.yml").read_text(encoding="utf-8")

        self.assertIn("matrix:", workflow)
        self.assertIn("windows-latest", workflow)
        self.assertIn("ubuntu-latest", workflow)
        self.assertIn("macos-latest", workflow)
        self.assertIn('HORIZON_CONAN_EXPORT_LOCAL_RECIPES: "false"', workflow)

    def test_core_conan_graph_excludes_engine_packages(self) -> None:
        recipe = (REPO_ROOT / "build_tools/conanfile.py").read_text(encoding="utf-8")
        requirements = recipe.split("def requirements(self):", 1)[1].split("def validate(self):", 1)[0]

        self.assertIn("if bool(self.options.with_engine):", requirements)
        engine_guard = requirements.index("if bool(self.options.with_engine):")
        for package in ("ktm", "pfr", "spirv-tools", "volk", "vulkan-headers",
                        "vulkan-memory-allocator", "slang", "tracy"):
            self.assertGreater(requirements.index(f'self.requires("{package}/'), engine_guard)
        for package in ("quill", "fmt", "spdlog", "xxhash"):
            self.assertLess(requirements.index(f'self.requires("{package}/'), engine_guard)

    def test_configuration_paths_are_per_configuration_and_target_family(self) -> None:
        root = Path("C:/repo")
        self.assertEqual(configuration_slug("RelWithDebInfo"), "relwithdebinfo")
        self.assertEqual(
            build_dir(root, "MinSizeRel"),
            root / "build" / "conan" / DEFAULT_TARGET_FAMILY / "minsizerel",
        )
        self.assertEqual(
            build_dir(root, "Debug", "ocarina-tests"),
            root / "build" / "conan" / "ocarina-tests" / "debug",
        )
        self.assertEqual(preset_name("vision-hotfix", "RelWithDebInfo"), "vision-hotfix-relwithdebinfo")

    def test_unknown_configuration_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            configuration_slug("Profile")
        with self.assertRaises(ValueError):
            target_family_slug("everything")

    def test_target_families_match_cmake_targets(self) -> None:
        self.assertEqual(target_family_for_target("Horizon"), "engine")
        self.assertEqual(target_family_for_target("Helicon"), "engine")
        self.assertEqual(target_family_for_target("ShaderCompileScripts"), "tools")
        self.assertEqual(target_family_for_target("HorizonExamples"), "examples")
        self.assertEqual(target_family_for_target("ocarina-native"), "ocarina")
        self.assertEqual(target_family_for_target("copy_slang_headers"), "ocarina")
        self.assertEqual(target_family_for_target("EASTL"), "ocarina")
        self.assertEqual(target_family_for_target("mimalloc-static"), "ocarina")
        self.assertEqual(target_family_for_target("test-core-parsetype"), "ocarina-tests")
        self.assertEqual(target_family_for_target("horizon-hotfix-run"), "vision-hotfix")
        self.assertEqual(target_family_for_targets(["test-core-parsetype"]), "ocarina-tests")
        with self.assertRaises(ValueError):
            target_family_for_targets(["HorizonExamples", "ocarina-native"])

    def test_build_commands_keep_all_requested_targets(self):
        for command in ("build", "build-fast", "rebuild"):
            with self.subTest(command=command), patch.object(dev, "install"), \
                    patch.object(dev, "ensure_git"), patch.object(dev, "ensure_ninja"), \
                    patch.object(dev, "cmake_configure"), patch.object(dev, "safe_remove"), \
                    patch.object(dev, "cmake_build") as build:
                dev.execute(dev.create_parser().parse_args([command, "ocarina-core", "ocarina-math"]))
                self.assertEqual(build.call_args.args[2], ["ocarina-core", "ocarina-math"])

    def test_target_families_select_conan_options(self) -> None:
        self.assertEqual(
            conan_options("core"),
            ["&:with_engine=False", "&:with_tests=True"],
        )
        self.assertEqual(conan_options("engine"), ["&:with_engine=True"])
        self.assertEqual(target_family_slug("engine"), "engine")
        self.assertEqual(conan_options("examples"), ["&:with_examples=True"])
        self.assertEqual(conan_options("ocarina"), ["&:with_ocarina=True"])
        self.assertEqual(
            conan_options("ocarina-tests"),
            ["&:with_ocarina=True", "&:with_ocarina_tests=True"],
        )
        with self.assertRaises(ValueError):
            conan_options("everything")

    def test_cmake_bracket_handles_embedded_delimiter(self) -> None:
        self.assertEqual(_cmake_bracket("a]]b"), "[=[a]]b]=]")

    def test_environment_keys_are_merged_case_insensitively(self) -> None:
        environment = {"PATH": "old", "Path": "duplicate", "OTHER": "value"}
        _set_environment_value(environment, "Path", "with-msvc")
        self.assertEqual(_environment_value(environment, "PATH"), "with-msvc")
        self.assertEqual(sum(key.casefold() == "path" for key in environment), 1)

    def test_safe_remove_refuses_outside_repository(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            root.mkdir()
            with self.assertRaises(RuntimeError):
                safe_remove(root, root.parent)


if __name__ == "__main__":
    unittest.main()
