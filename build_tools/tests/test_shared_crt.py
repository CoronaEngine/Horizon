"""Check runtime selection and reject Conan profiles that split DLL CRT state."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools.paths import REPO_ROOT


class ConanSharedCrtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            from conan.errors import ConanInvalidConfiguration
            from conan.internal.model.settings import Settings
        except ImportError:
            raise unittest.SkipTest("Conan required")
        spec = importlib.util.spec_from_file_location("horizon_recipe", REPO_ROOT / "build_tools/conanfile.py")
        recipe = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(recipe)
        cls.recipe_class = recipe.HorizonConan
        cls.settings_class = Settings
        cls.invalid_configuration = ConanInvalidConfiguration

    def recipe(self, configuration, runtime="dynamic", runtime_type=None, system="Windows"):
        recipe = self.recipe_class()
        recipe.settings = self.settings_class({
            "os": ["Windows", "Macos", "Linux"],
            "compiler": {"msvc": {"runtime": ["dynamic", "static"],
                                     "runtime_type": ["Debug", "Release"]}},
            "build_type": ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
        })
        recipe.settings.os = system
        recipe.settings.compiler = "msvc"
        recipe.settings.compiler.runtime = runtime
        recipe.settings.compiler.runtime_type = runtime_type or ("Debug" if configuration == "Debug" else "Release")
        recipe.settings.build_type = configuration
        return recipe

    def test_matching_dynamic_crt_is_accepted_in_every_configuration(self):
        for configuration in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
            with self.subTest(configuration=configuration):
                self.recipe(configuration).validate()

    def test_static_windows_crt_is_rejected(self):
        for configuration in ("Debug", "Release"):
            with self.subTest(configuration=configuration):
                with self.assertRaises(self.invalid_configuration):
                    self.recipe(configuration, runtime="static").validate()

    def test_mixed_debug_and_release_crt_is_rejected(self):
        for configuration, runtime_type in (("Debug", "Release"), ("Release", "Debug")):
            with self.subTest(configuration=configuration, runtime_type=runtime_type):
                with self.assertRaises(self.invalid_configuration):
                    self.recipe(configuration, runtime_type=runtime_type).validate()


class CmakeSharedCrtTests(unittest.TestCase):
    def configure(self, root, *, raw_flags="", directory_option=""):
        cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
        if not cmake or not ninja:
            self.skipTest("CMake and Ninja required")
        (root / "probe.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        module = (REPO_ROOT / "build_tools/cmake/horizon_compile_config.cmake").as_posix()
        (root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 4.0)\nproject(SharedCrtProbe LANGUAGES CXX)\n'
            # Exercise the MSVC policy on every host; generation evaluates the
            # same target runtime property that Windows uses for its flags.
            'set(MSVC TRUE)\nset(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)\n'
            f'set(CMAKE_CXX_FLAGS "{raw_flags}")\n'
            f'add_compile_options("{directory_option}")\ninclude("{module}")\n'
            'add_library(fixture SHARED probe.cpp)\nadd_executable(consumer probe.cpp)\n'
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/runtime-$<CONFIG>.txt" CONTENT '
            '"$<TARGET_GENEX_EVAL:fixture,$<TARGET_PROPERTY:fixture,MSVC_RUNTIME_LIBRARY>>\\n'
            '$<TARGET_GENEX_EVAL:consumer,$<TARGET_PROPERTY:consumer,MSVC_RUNTIME_LIBRARY>>\\n")\n',
            encoding="utf-8")
        return subprocess.run(
            [cmake, "-S", str(root), "-B", str(root / "build"), "-G", "Ninja Multi-Config",
             f"-DCMAKE_MAKE_PROGRAM={ninja}",
             "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo;MinSizeRel"],
            env=os.environ.copy(), capture_output=True, text=True, timeout=60)

    def test_dll_and_executable_use_matching_dynamic_crt_in_every_configuration(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = self.configure(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for configuration in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
                with self.subTest(configuration=configuration):
                    expected = "MultiThreadedDebugDLL" if configuration == "Debug" else "MultiThreadedDLL"
                    self.assertEqual((root / f"build/runtime-{configuration}.txt").read_text().splitlines(),
                                     [expected, expected])

    def test_manual_runtime_flags_cannot_override_the_shared_crt(self):
        for raw_flags in ("/MT", "/MDd"):
            with self.subTest(raw_flags=raw_flags), tempfile.TemporaryDirectory() as temporary:
                result = self.configure(Path(temporary), raw_flags=raw_flags)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("CRT", result.stdout + result.stderr)

    def test_inherited_runtime_options_cannot_override_the_shared_crt(self):
        for option in ("$<$<CONFIG:Debug>:/MTd>", "SHELL:/MT", "$<IF:$<CONFIG:Debug>,/MDd,/MT>"):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as temporary:
                result = self.configure(Path(temporary), directory_option=option)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("CRT", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
