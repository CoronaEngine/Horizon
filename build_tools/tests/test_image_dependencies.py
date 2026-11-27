"""Exercise the CMake dependency graph without a compiler or installed packages."""

from pathlib import Path
from itertools import product
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools.paths import REPO_ROOT


class ImageDependencyTests(unittest.TestCase):
    def test_image_dependencies_follow_effective_ocarina_option(self):
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        if not cmake or not ninja:
            self.skipTest("CMake and Ninja required; tests never install tools")
        for examples, ocarina in product((False, True), repeat=2):
            with self.subTest(examples=examples, ocarina=ocarina), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                package = root / "packages"
                headers = package / "include"
                headers.mkdir(parents=True)
                (package / "stbConfig.cmake").write_text(
                    'add_library(stb::stb INTERFACE IMPORTED)\n'
                    'set_target_properties(stb::stb PROPERTIES\n'
                    f'    INTERFACE_INCLUDE_DIRECTORIES "{headers.as_posix()}")\n',
                    encoding="utf-8")
                (package / "tinyexrConfig.cmake").write_text(
                    'add_library(tinyexr::tinyexr INTERFACE IMPORTED)\n', encoding="utf-8")
                consumer = root / "consumer"
                consumer.mkdir()
                (consumer / "CMakeLists.txt").write_text(
                    'if(NOT TARGET horizon::stb OR NOT TARGET horizon::tinyexr)\n'
                    '    message(FATAL_ERROR "Ocarina requires stb and TinyEXR")\n'
                    'endif()\n'
                    'add_library(window-consumer INTERFACE)\n'
                    'target_link_libraries(window-consumer INTERFACE horizon::stb)\n'
                    'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/includes.txt"\n'
                    '    CONTENT "$<TARGET_PROPERTY:window-consumer,INTERFACE_INCLUDE_DIRECTORIES>")\n',
                    encoding="utf-8")
                modules = (REPO_ROOT / "build_tools/cmake").as_posix()
                (root / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 4.0)\n'
                    'project(ImageDependencies LANGUAGES NONE)\n'
                    f'list(APPEND CMAKE_MODULE_PATH "{modules}")\n'
                    f'list(APPEND CMAKE_PREFIX_PATH "{package.as_posix()}")\n'
                    'include(horizon_options)\n'
                    'include(horizon_dependencies)\n'
                    'include(horizon_image_dependencies)\n'
                    'if(HORIZON_BUILD_OCARINA)\n'
                    '    add_subdirectory(consumer)\n'
                    'elseif(TARGET horizon::stb OR TARGET stb::stb OR TARGET horizon::tinyexr OR TARGET tinyexr::tinyexr)\n'
                    '    message(FATAL_ERROR "Image dependencies loaded without an Ocarina consumer")\n'
                    'endif()\n',
                    encoding="utf-8")
                build = root / "build"
                result = subprocess.run(
                    [cmake, "-S", str(root), "-B", str(build), "-G", "Ninja",
                     f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}",
                     f"-DHORIZON_BUILD_EXAMPLES={'ON' if examples else 'OFF'}",
                     f"-DHORIZON_BUILD_OCARINA={'ON' if ocarina else 'OFF'}"],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                includes = build / "includes.txt"
                if examples or ocarina:
                    self.assertEqual(includes.read_text(encoding="utf-8"), headers.as_posix())
                else:
                    self.assertFalse(includes.exists())


if __name__ == "__main__":
    unittest.main()
