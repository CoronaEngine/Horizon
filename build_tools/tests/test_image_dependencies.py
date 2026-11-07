"""Exercise the CMake dependency graph without a compiler or installed packages."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools.paths import REPO_ROOT


class ImageDependencyTests(unittest.TestCase):
    def test_core_window_consumer_receives_stb_headers(self):
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        if not cmake or not ninja:
            self.skipTest("CMake and Ninja required; tests never install tools")
        for baseline in (False, True):
            with self.subTest(baseline=baseline), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                package = root / "packages"
                headers = package / "include"
                headers.mkdir(parents=True)
                (package / "stbConfig.cmake").write_text(
                    'add_library(stb::stb INTERFACE IMPORTED)\n'
                    'set_target_properties(stb::stb PROPERTIES\n'
                    f'    INTERFACE_INCLUDE_DIRECTORIES "{headers.as_posix()}")\n',
                    encoding="utf-8")
                consumer = root / "consumer"
                consumer.mkdir()
                (consumer / "CMakeLists.txt").write_text(
                    'if(NOT TARGET horizon::stb)\n'
                    '    message(FATAL_ERROR "Baseline window requires horizon::stb")\n'
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
                    'set(HORIZON_BUILD_ENGINE OFF)\n'
                    'set(HORIZON_BUILD_OCARINA OFF)\n'
                    'set(HORIZON_BUILD_EXAMPLES OFF)\n'
                    f'set(HORIZON_BUILD_BASELINE_WINDOW_TEST {"ON" if baseline else "OFF"})\n'
                    'include(horizon_dependencies)\n'
                    'include(horizon_image_dependencies)\n'
                    'if(HORIZON_BUILD_BASELINE_WINDOW_TEST)\n'
                    '    add_subdirectory(consumer)\n'
                    'elseif(TARGET horizon::stb OR TARGET stb::stb)\n'
                    '    message(FATAL_ERROR "Plain core must not load stb")\n'
                    'endif()\n',
                    encoding="utf-8")
                build = root / "build"
                result = subprocess.run(
                    [cmake, "-S", str(root), "-B", str(build), "-G", "Ninja",
                     f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}"],
                    capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if baseline:
                    self.assertEqual((build / "includes.txt").read_text(encoding="utf-8"),
                                     headers.as_posix())


if __name__ == "__main__":
    unittest.main()
