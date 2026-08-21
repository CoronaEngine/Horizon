from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from workflow import (
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
from dev import conan_options, target_family_for_target, target_family_for_targets


class WorkflowTests(unittest.TestCase):
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
        self.assertEqual(target_family_for_target("Horizon"), "core")
        self.assertEqual(target_family_for_target("ShaderCompileScripts"), "tools")
        self.assertEqual(target_family_for_target("HorizonExamples"), "examples")
        self.assertEqual(target_family_for_target("ocarina-native"), "ocarina")
        self.assertEqual(target_family_for_target("copy_cuda_headers"), "ocarina")
        self.assertEqual(target_family_for_target("EASTL"), "ocarina")
        self.assertEqual(target_family_for_target("mimalloc-static"), "ocarina")
        self.assertEqual(target_family_for_target("test-core-parsetype"), "ocarina-tests")
        self.assertEqual(target_family_for_target("horizon-hotfix-run"), "vision-hotfix")
        self.assertEqual(target_family_for_targets(["test-core-parsetype"]), "ocarina-tests")
        with self.assertRaises(ValueError):
            target_family_for_targets(["HorizonExamples", "ocarina-native"])

    def test_target_families_select_conan_options(self) -> None:
        self.assertEqual(conan_options("core"), ["&:with_tests=True"])
        self.assertEqual(conan_options("examples"), ["&:with_examples=True"])
        self.assertEqual(
            conan_options("ocarina-tests"),
            ["&:with_ocarina=True", "&:with_cuda=True", "&:with_ocarina_tests=True"],
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
