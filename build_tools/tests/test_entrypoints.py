"""Public entry points, profile selection and command boundaries."""

import json
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
