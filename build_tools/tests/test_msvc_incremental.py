"""Verify real Ninja header tracking and IDE builds without an activated MSVC shell."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools import workflow
from build_tools.paths import REPO_ROOT


@unittest.skipUnless(os.name == "nt", "MSVC is Windows-specific")
class MsvcIncrementalTests(unittest.TestCase):
    def test_header_edit_rebuilds_with_no_console_and_no_build_shell_environment(self):
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja")
        if not cmake or not ninja:
            self.skipTest("CMake and Ninja required")
        env = dict(os.environ)
        if not shutil.which("cl"):
            if not (workflow.generators_dir(REPO_ROOT, "RelWithDebInfo", "core") / "conanbuild.bat").is_file():
                self.skipTest("MSVC environment or installed core profile required")
            env = workflow.load_conan_build_environment(REPO_ROOT, "RelWithDebInfo", "core")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            snapshot = {name: workflow._environment_value(env, name)
                        for name in ("PATH", "INCLUDE", "LIB", "LIBPATH")
                        if workflow._environment_value(env, name)}
            (root / "dev_build_environment.json").write_text(json.dumps(snapshot), encoding="utf-8")
            (root / "dev_build_environment.cmake").touch()
            header = root / "header.h"
            header.write_text("#define RESULT 0\n")
            (root / "main.c").write_text('#include "header.h"\nint main(void) { return RESULT; }\n')
            module = (REPO_ROOT / "build_tools/cmake/horizon_msvc_build_environment.cmake").as_posix()
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 4.0)\nproject(HeaderTracking LANGUAGES C)\n'
                f'set(HORIZON_DEV_PYTHON "{Path(sys.executable).as_posix()}")\n'
                'set(HORIZON_DEV_BUILD_ENVIRONMENT "${CMAKE_CURRENT_SOURCE_DIR}/dev_build_environment.cmake")\n'
                f'include("{module}")\nhorizon_msvc_build_environment()\n'
                'add_executable(probe main.c)\n', encoding="utf-8")
            build = root / "build"
            configure = subprocess.run(
                [cmake, "-S", str(root), "-B", str(build), "-G", "Ninja",
                 f"-DCMAKE_MAKE_PROGRAM={ninja}", "-DCMAKE_C_COMPILER=cl", "-DCMAKE_BUILD_TYPE=Release"],
                env=env, capture_output=True, timeout=60)
            self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
            build_env = {k: v for k, v in env.items() if k.upper() not in {"INCLUDE", "LIB", "LIBPATH"}}
            for expected in (0, 7):
                header.write_text(f"#define RESULT {expected}\n")
                result = subprocess.run([cmake, "--build", str(build)], env=build_env,
                                        creationflags=subprocess.CREATE_NO_WINDOW,
                                        capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(subprocess.run([str(build / "probe.exe")], timeout=30).returncode, expected)
            result = subprocess.run([cmake, "--build", str(build)], env=build_env,
                                    creationflags=subprocess.CREATE_NO_WINDOW,
                                    capture_output=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(b"no work to do", result.stdout)


if __name__ == "__main__":
    unittest.main()
