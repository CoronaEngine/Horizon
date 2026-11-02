"""Configure and build when Ninja is initially absent from PATH or cached badly."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools import workflow
from build_tools.ninja_tool import probe_ninja
from build_tools.paths import REPO_ROOT


class CmakeNinjaTests(unittest.TestCase):
    def test_configure_and_build_with_exported_ninja(self):
        cmake = shutil.which("cmake")
        ninja = shutil.which("ninja") or workflow.cached_ninja(REPO_ROOT, "RelWithDebInfo", "core")
        if not cmake or not ninja or not probe_ninja(ninja):
            self.skipTest("CMake and an existing Ninja required; tests never install tools")
        for cached in ("absent", "working", "missing", "wrong-program"):
            with self.subTest(cached=cached), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                tools_dir = root / "tools with spaces"
                tools_dir.mkdir()
                fallback = tools_dir / Path(ninja).name
                shutil.copy2(ninja, fallback)
                source = root / "source"
                source.mkdir()
                module = (REPO_ROOT / "build_tools/cmake/horizon_ninja.cmake").as_posix()
                (source / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 4.0)\n'
                    f'set(ENV{{PATH}} "{tools_dir.as_posix()}{os.pathsep}$ENV{{PATH}}")\n'
                    f'include("{module}")\nhorizon_configure_ninja()\n'
                    'project(NinjaBootstrapTest LANGUAGES NONE)\n'
                    'add_custom_target(smoke ALL COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/built")\n',
                    encoding="utf-8")
                env = dict(os.environ, PATH=str(root))
                self.assertIsNone(shutil.which("ninja", path=env["PATH"]))
                build = root / "build"
                command = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja Multi-Config"]
                value = {"working": ninja, "missing": str(root / "removed-ninja"), "wrong-program": cmake}.get(cached)
                if value:
                    command.append(f"-DCMAKE_MAKE_PROGRAM:FILEPATH={value}")
                result = subprocess.run(command, env=env, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                selected = workflow._cache_value(build / "CMakeCache.txt", "CMAKE_MAKE_PROGRAM")
                self.assertEqual(Path(selected).resolve(), Path(ninja if cached == "working" else fallback).resolve())
                result = subprocess.run([cmake, "--build", str(build), "--config", "Release"],
                                        env=env, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertTrue((build / "built").is_file())


if __name__ == "__main__":
    unittest.main()
