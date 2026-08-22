from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import workflow as workflow_module
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
    def test_posix_conan_install_uses_multi_config_layout(self) -> None:
        with (
            patch.object(workflow_module.platform, "system", return_value="Linux"),
            patch.object(workflow_module, "run_command") as run_command,
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
            str(root / "conan" / "profiles" / "windows-msvc-debug"),
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
        workflow = Path(".github/workflows/core-tests.yml").read_text(encoding="utf-8")

        self.assertIn("matrix:", workflow)
        self.assertIn("windows-latest", workflow)
        self.assertIn("ubuntu-latest", workflow)
        self.assertIn("macos-latest", workflow)
        self.assertIn('HORIZON_CONAN_EXPORT_LOCAL_RECIPES: "false"', workflow)

    def test_core_header_keeps_api_macros_portable(self) -> None:
        header = Path("src/core/header.h").read_text(encoding="utf-8")

        self.assertNotIn('#include "core/runtime/oc_windows.h"', header)
        self.assertIn(
            "#if defined(_MSC_VER) && !defined(OC_STATIC_LINK)\n"
            "#define OC_DLL_EXPORT __declspec(dllexport)",
            header,
        )
        self.assertIn('#define OC_DLL_EXPORT [[gnu::visibility("default")]]', header)
        self.assertIn("#define OC_DLL_EXPORT\n#define OC_DLL_IMPORT\n#endif", header)

    def test_core_stl_does_not_call_msvc_allocation_or_conversion_apis(self) -> None:
        header = Path("src/core/stl.h").read_text(encoding="utf-8")
        stl = header + Path("src/core/stl.cpp").read_text(encoding="utf-8")

        self.assertIn("#include <cstring>", header)
        self.assertNotIn("_aligned_malloc", stl)
        self.assertNotIn("_aligned_free", stl)
        self.assertNotIn("wcstombs_s", stl)
        self.assertNotIn("std::wmemcpy", stl)

    def test_core_conan_graph_excludes_engine_packages(self) -> None:
        recipe = Path("conanfile.py").read_text(encoding="utf-8")
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
        self.assertEqual(
            conan_options("core"),
            ["&:with_engine=False", "&:with_tests=True"],
        )
        self.assertEqual(conan_options("engine"), ["&:with_engine=True"])
        self.assertEqual(target_family_slug("engine"), "engine")
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
