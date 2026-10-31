"""Provision tools in the running Python's environment and fail before work."""

from contextlib import contextmanager, ExitStack
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from build_tools import conan_cache, conda_environment, dev, git_tool, ninja_tool, workflow
from build_tools.file_lock import file_lock
from build_tools.paths import REPO_ROOT

TOOLS = ((git_tool, "git", "git version 2.50.0\n"), (ninja_tool, "ninja", "1.13.2\n"))


def make_tool(prefix, name):
    path = prefix / (f"Library/bin/{name}.exe" if os.name == "nt" else f"bin/{name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
    return path


class ProvisioningTests(unittest.TestCase):
    @contextmanager
    def environment(self, module):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            prefix = Path(temporary) / "Conda environment"
            (prefix / "conda-meta").mkdir(parents=True)
            stack.enter_context(patch.object(module.sys, "prefix", str(prefix)))
            stack.enter_context(patch.object(module.shutil, "which", return_value=None))
            stack.enter_context(patch.object(module, "find_conda", return_value="project-conda"))
            stack.enter_context(patch.dict(os.environ, {"PATH": str(prefix / "empty"),
                                                       "CONDA_PREFIX": str(prefix / "unrelated")}))
            yield prefix

    def test_reuses_tools_without_conda(self):
        for module, name, version in TOOLS:
            for source in ("path", "environment", *(["cache"] if name == "ninja" else [])):
                with self.subTest(tool=name, source=source), self.environment(module) as prefix:
                    executable = make_tool(prefix if source == "environment" else prefix.parent / "external", name)
                    if source != "environment":
                        module.sys.prefix = str(prefix / "not-conda")
                    if source == "path":
                        module.shutil.which.return_value = str(executable)
                    with patch.object(module.subprocess, "run", return_value=
                                      subprocess.CompletedProcess([], 0, version)) as run:
                        args = [str(executable)] if source == "cache" else []
                        self.assertEqual(getattr(module, f"ensure_{name}")(*args), executable)
                    run.assert_called_once()
                    self.assertEqual(run.call_args.args[0], [str(executable), "--version"])
                    if source != "path":
                        self.assertEqual(os.environ["PATH"].split(os.pathsep)[0], str(executable.parent))

    def test_installs_missing_tools_and_repairs_broken_packages(self):
        for module, name, version in TOOLS:
            for repair in (False, True):
                with self.subTest(tool=name, repair=repair), self.environment(module) as prefix:
                    (prefix / f"conda-meta/{name}-extra-1.0-0.json").write_text("{}")
                    if repair:
                        make_tool(prefix, name)
                        (prefix / f"conda-meta/{name}-1.0-0.json").write_text("{}")
                    installs = []

                    def run(command, **kwargs):
                        if command[0] == "project-conda":
                            installs.append(command)
                            make_tool(prefix, name)
                            return subprocess.CompletedProcess(command, 0)
                        if not installs:
                            raise subprocess.CalledProcessError(1, command)
                        return subprocess.CompletedProcess(command, 0, version)

                    with patch.object(module.subprocess, "run", side_effect=run):
                        executable = getattr(module, f"ensure_{name}")()
                    self.assertEqual(len(installs), 1)
                    command = installs[0]
                    self.assertEqual(command[command.index("--prefix") + 1], str(prefix))
                    self.assertEqual(command[command.index("--channel") + 1], "conda-forge")
                    self.assertIn("--freeze-installed", command)
                    self.assertIn(name, command)
                    self.assertEqual("--force-reinstall" in command, repair)
                    self.assertEqual(os.environ["PATH"].split(os.pathsep)[0], str(executable.parent))

    def test_failed_or_incomplete_installation_is_not_accepted(self):
        for module, name, _ in TOOLS:
            for fails in (False, True):
                with self.subTest(tool=name, fails=fails), self.environment(module):
                    effect = subprocess.CalledProcessError(9, ["conda"]) if fails else None
                    with patch.object(module.subprocess, "run", side_effect=effect,
                                      return_value=subprocess.CompletedProcess([], 0)):
                        with self.assertRaisesRegex(RuntimeError, "installation failed.*9" if fails else "not usable"):
                            getattr(module, f"ensure_{name}")()

    def test_missing_tools_outside_conda_do_not_trigger_installation(self):
        for module, name, _ in TOOLS:
            with self.subTest(tool=name), self.environment(module) as prefix:
                module.sys.prefix = str(prefix / "system-python")
                with patch.object(module.subprocess, "run") as run:
                    with self.assertRaisesRegex(RuntimeError, "not in a Conda environment"):
                        getattr(module, f"ensure_{name}")()
                run.assert_not_called()

    def test_waiter_rechecks_tool_before_installing(self):
        for module, name, version in TOOLS:
            with self.subTest(tool=name), self.environment(module):
                @contextmanager
                def installed_while_waiting(prefix):
                    make_tool(prefix, name)
                    yield

                with patch.object(module, "conda_environment_lock", installed_while_waiting), \
                        patch.object(module.subprocess, "run", return_value=
                                     subprocess.CompletedProcess([], 0, version)) as run:
                    getattr(module, f"ensure_{name}")()
                run.assert_called_once()
                self.assertEqual(run.call_args.args[0][-1], "--version")

    def test_unrunnable_tools_are_rejected(self):
        for module, name, _ in TOOLS:
            for error in (OSError("broken"), subprocess.TimeoutExpired(name, 30)):
                with self.subTest(tool=name, error=error), self.environment(module) as prefix:
                    make_tool(prefix, name)
                    with patch.object(module.subprocess, "run", side_effect=error):
                        self.assertIsNone(getattr(module, f"find_{name}")(prefix))

    def test_ninja_rejects_wrong_version_output(self):
        for value, accepted in (("1.13.2\n", True), ("1.13.2.git\n", True),
                                ("cmake version 4.0.3\n", False), ("", False), ("1.2\nnoise", False)):
            with self.subTest(value=value), patch.object(ninja_tool.subprocess, "run", return_value=
                                                        subprocess.CompletedProcess([], 0, value)):
                self.assertEqual(ninja_tool.probe_ninja("ninja"), accepted)

    def test_conda_found_without_activated_shell(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            conda = base / ("Scripts/conda.exe" if os.name == "nt" else "bin/conda")
            conda.parent.mkdir()
            conda.touch()
            with patch.dict(os.environ, {"CONDA_EXE": ""}), \
                    patch.object(conda_environment.shutil, "which", return_value=None):
                self.assertEqual(conda_environment.find_conda(base / "envs/project"), str(conda))

    def test_environment_lock_excludes_another_process_and_releases(self):
        script = (
            "import sys\nfrom pathlib import Path\n"
            "from build_tools.conda_environment import conda_environment_lock\n"
            "try:\n with conda_environment_lock(Path(sys.argv[1]), timeout=0.1): pass\n"
            "except RuntimeError:\n sys.exit(7)\n"
        )
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            command = [sys.executable, "-B", "-c", script, str(prefix)]
            with file_lock(prefix / "conda-meta/corona-tools.lock"):
                result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 7, result.stderr)
            result = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)


class CommandBoundaryTests(unittest.TestCase):
    def test_build_commands_prepare_tools_before_work(self):
        for command in ("install", "_bootstrap", "configure", "build", "build-fast", "rebuild", "update"):
            for tool in ("git", "ninja"):
                with self.subTest(command=command, tool=tool), ExitStack() as stack:
                    stack.enter_context(patch.object(dev, "ensure_git"))
                    stack.enter_context(patch.object(dev, "ensure_ninja"))
                    stack.enter_context(patch.object(dev, f"ensure_{tool}", side_effect=RuntimeError("tool failed")))
                    operations = [stack.enter_context(patch.object(dev, name)) for name in
                                  ("install", "cmake_configure", "cmake_build", "safe_remove")]
                    with self.assertRaisesRegex(RuntimeError, "tool failed"):
                        dev.execute(dev.create_parser().parse_args([command]))
                    for operation in operations:
                        operation.assert_not_called()

    def test_status_only_prepares_git_and_clean_needs_neither(self):
        for command in ("status", "clean"):
            with self.subTest(command=command), patch.object(dev, "ensure_git") as git, \
                    patch.object(dev, "ensure_ninja") as ninja, patch.object(dev, "run_command"), \
                    patch.object(dev, "clean_repo"):
                dev.execute(dev.create_parser().parse_args([command]))
            self.assertEqual(git.call_count, int(command == "status"))
            ninja.assert_not_called()

    def test_cache_update_prepares_both_tools_before_install(self):
        calls = Mock()
        with patch.object(conan_cache, "ensure_git", calls.git), \
                patch.object(conan_cache, "ensure_ninja", calls.ninja), \
                patch.object(conan_cache, "conan_install", calls.install):
            conan_cache.update_cache(conan_cache.create_parser().parse_args(["update", "fmt/12.1.0"]))
        self.assertEqual([call[0] for call in calls.mock_calls], ["git", "ninja", "install"])

    def test_build_repairs_stale_ninja_before_starting(self):
        for working in (False, True):
            with self.subTest(working=working), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                directory = workflow.build_dir(root, "Debug", "core")
                directory.mkdir(parents=True)
                (directory / "CMakeCache.txt").write_text(
                    "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\nCMAKE_MAKE_PROGRAM:FILEPATH=cached-ninja\n")
                calls = Mock()
                with patch.object(workflow, "probe_ninja", return_value=working), \
                        patch.object(workflow, "write_cmake_build_environment", calls.save), \
                        patch.object(workflow, "cmake_configure", calls.configure), \
                        patch.object(workflow, "load_conan_build_environment", return_value={}), \
                        patch.object(workflow, "run_command", calls.build):
                    workflow.cmake_build(root, "Debug", ["horizon-tests"], "core")
                self.assertEqual([call[0] for call in calls.mock_calls],
                                 ["build"] if working else ["save", "configure", "build"])
                self.assertEqual(workflow.cached_ninja(root, "Debug", "core"), "cached-ninja")


if __name__ == "__main__":
    unittest.main()
