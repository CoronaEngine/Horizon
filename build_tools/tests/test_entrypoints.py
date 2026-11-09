"""Public entry points, profile selection and command boundaries."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools import conan_cache, workflow
from build_tools.paths import REPO_ROOT, conanfile_path


class EntryPointTests(unittest.TestCase):
    def test_scripts_work_outside_the_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            for script in ("dev.py", "conan_cache.py"):
                with self.subTest(script=script):
                    result = subprocess.run(
                        [sys.executable, "-B", str(REPO_ROOT / "build_tools" / script), "--help"],
                        cwd=temporary, capture_output=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(b"usage:", result.stdout)

    def test_root_presets_are_loadable_and_have_no_old_aliases(self):
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake required")
        result = subprocess.run([cmake, "--list-presets", "-S", str(REPO_ROOT)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for family in workflow.TARGET_FAMILIES:
            for config in workflow.CONFIGURATIONS:
                self.assertIn(f'"{family}-{config.lower()}"', result.stdout)
        for config in workflow.CONFIGURATIONS:
            self.assertNotIn(f'"{config.lower()}"', result.stdout)

    def test_multi_target_build_keeps_prepared_environment_during_regeneration(self):
        with patch.object(workflow, "assert_cache_matches_repo"), \
                patch.object(workflow, "repair_ninja_cache"), \
                patch.object(workflow, "load_conan_build_environment", return_value={}), \
                patch.object(workflow, "run_command") as run:
            workflow.cmake_build(REPO_ROOT, "Debug", ["Horizon", "Helicon"], "engine")
        self.assertEqual(run.call_args.args[0][-3:], ("--target", "Horizon", "Helicon"))
        self.assertEqual(run.call_args.kwargs["env"][workflow.BOOTSTRAP_ENV], "1")

    def test_ide_bootstrap_keeps_enabled_features_for_each_python_provider(self):
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake required")
        for provider in ("venv", "conda"):
            for features in ((), ("ocarina",), ("ocarina_tests",), ("tests",),
                             ("ocarina", "ocarina_tests", "tests")):
                with self.subTest(provider=provider, features=features), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    generators = root / "build/conan/examples/release/generators"
                    generators.mkdir(parents=True)
                    for name in ("conan_toolchain.cmake", "dev_build_environment.cmake"):
                        (generators / name).touch()
                    (root / "horizon_ninja.cmake").write_text(
                        "function(horizon_configure_ninja)\nendfunction()\n", encoding="utf-8")
                    if provider == "venv":
                        python = root / (".venv/Scripts/python.exe" if os.name == "nt" else ".venv/bin/python")
                        python.parent.mkdir(parents=True)
                        python.touch()
                    # Capture the external process boundary, leaving all bootstrap
                    # argument selection and provider branching in real CMake.
                    script = root / "bootstrap.cmake"
                    script.write_text(
                        'cmake_minimum_required(VERSION 4.0)\n'
                        f'set(HORIZON_BUILD_TOOLS_DIR "{(REPO_ROOT / "build_tools").as_posix()}")\n'
                        f'set(CMAKE_MODULE_PATH "{root.as_posix()}")\n'
                        'set(HORIZON_DEV_CONFIGURATION Release CACHE STRING "")\n'
                        'set(HORIZON_DEV_TARGET_FAMILY examples CACHE STRING "")\n'
                        'set(HORIZON_BUILD_BASELINE_WINDOW_TEST ON)\n'
                        + ''.join(f'set(HORIZON_BUILD_{feature.upper()} {"ON" if feature in features else "OFF"})\n'
                                  for feature in ("ocarina", "ocarina_tests", "tests"))
                        + f'set(_horizon_conda "{(root / "conda").as_posix()}")\n'
                        'macro(execute_process)\n'
                        '  cmake_parse_arguments(PROCESS "" "RESULT_VARIABLE;OUTPUT_VARIABLE;WORKING_DIRECTORY;COMMAND_ECHO" "COMMAND" ${ARGV})\n'
                        '  if(PROCESS_RESULT_VARIABLE)\n'
                        '    set(${PROCESS_RESULT_VARIABLE} 0)\n'
                        '    set_property(GLOBAL PROPERTY captured_bootstrap "${PROCESS_COMMAND}")\n'
                        '  endif()\n'
                        '  if(PROCESS_OUTPUT_VARIABLE)\n'
                        f'    set(${{PROCESS_OUTPUT_VARIABLE}} [=[{{"envs":["{(root / "horizon-dev").as_posix()}"]}}]=])\n'
                        '  endif()\n'
                        'endmacro()\n'
                        'include("${HORIZON_BUILD_TOOLS_DIR}/cmake/horizon_dev_bootstrap.cmake")\n'
                        'horizon_dev_bootstrap()\n'
                        'get_property(command GLOBAL PROPERTY captured_bootstrap)\n'
                        'file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/command.txt" "${command}")\n',
                        encoding="utf-8")
                    env = dict(os.environ)
                    env.pop(workflow.BOOTSTRAP_ENV, None)
                    result = subprocess.run([cmake, "-P", str(script)], cwd=root,
                                            env=env, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    command = (root / "command.txt").read_text(encoding="utf-8").split(";")
                    self.assertEqual(command[command.index("--target-family") + 1], "examples")
                    self.assertIn("horizon-test-baseline-window", command)
                    self.assertEqual("--modify" in command, provider == "venv", command)
                    for feature, flag in (("ocarina", "--with-ocarina"),
                                          ("ocarina_tests", "--with-ocarina-tests"),
                                          ("tests", "--with-tests")):
                        self.assertEqual(flag in command, feature in features, command)

    def test_cache_update_uses_the_shared_install_transaction(self):
        for package in (None, "fmt/12.1.0"):
            with self.subTest(package=package), patch.object(conan_cache, "conan_install") as install, \
                    patch.object(conan_cache, "ensure_git"), patch.object(conan_cache, "ensure_ninja"):
                args = conan_cache.create_parser().parse_args(
                    ["update", *([package] if package else []), "--target-family", "core"])
                conan_cache.update_cache(args)
                kwargs = install.call_args.kwargs
                self.assertEqual(kwargs["target_family"], "core")
                self.assertEqual(kwargs["requires"], package)
                self.assertEqual(kwargs["options"], () if package else ["&:with_engine=False", "&:with_tests=True"])

    def test_install_platform_and_output_paths(self):
        for system in ("Windows", "Linux", "Darwin"):
            for package in (None, "fmt/12.1.0"):
                with self.subTest(system=system, package=package), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    (root / "build_tools/conan/profiles").mkdir(parents=True)
                    (root / "build_tools/conan/profiles/windows-msvc-debug").touch()
                    with patch.object(workflow.platform, "system", return_value=system), \
                            patch.object(workflow, "conan_cache_lock"), \
                            patch.object(workflow, "run_command") as run, \
                            patch.object(workflow, "write_cmake_build_environment") as save:
                        workflow.conan_install(root, "Debug", target_family="core", options=(), recipes=(),
                                               recipe_toggle_env="HORIZON_TEST_RECIPES", requires=package,
                                               update="fmt")
                    command = run.call_args.args[0]
                    self.assertEqual(command[:2], ["conan", "install"])
                    self.assertIn("--update=fmt", command)
                    if system == "Windows":
                        self.assertIn(str(root / "build_tools/conan/profiles/windows-msvc-debug"), command)
                    else:
                        self.assertIn(f"horizon-{system.lower()}-debug", command)
                        self.assertIn("tools.cmake.cmaketoolchain:generator=Ninja Multi-Config", command)
                    if package:
                        self.assertEqual(command[2:4], ["--requires", package])
                        self.assertEqual(command[command.index("--output-folder") + 1], root / "build/conan/cache/debug")
                        save.assert_not_called()
                    else:
                        self.assertEqual(command[2], conanfile_path(root))
                        save.assert_called_once()

    def test_environment_snapshot_preserves_escaped_values(self):
        values = {"PATH": 'C:/tool path;C:/quote"and\\slash', "LIB": "left;right"}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workflow.generators_dir(root, "Debug", "core").mkdir(parents=True)
            with patch.object(workflow, "load_conan_build_environment", return_value=values):
                cmake = workflow.write_cmake_build_environment(root, "Debug", "core")
            self.assertEqual(json.loads(cmake.with_suffix(".json").read_text(encoding="utf-8")), values)


if __name__ == "__main__":
    unittest.main()
