"""Validate the pinned experimental MoltenVK installer without network or Xcode."""

from contextlib import ExitStack
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import zipfile


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/setup_moltenvk_ci.py"


class MoltenVkCiTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), "The experimental MoltenVK CI installer is missing")
        spec = importlib.util.spec_from_file_location("moltenvk_ci", SCRIPT)
        self.installer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.installer)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "driver with spaces"
        self.library = b"fixture experimental MoltenVK dylib"
        self.make_archive()

    def make_archive(self, *, symlink=False, member=None):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w") as archive:
            info = tarfile.TarInfo(member or "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib")
            info.size = len(self.library)
            if symlink:
                info.type = tarfile.SYMTYPE
                info.linkname = "../../../../outside"
            archive.addfile(info, None if symlink else io.BytesIO(self.library))
            unrelated = tarfile.TarInfo("../../must-not-extract")
            unrelated.size = 1
            archive.addfile(unrelated, io.BytesIO(b"x"))
        self.tar_bytes = buffer.getvalue()
        self.archive = self.root / "artifact.zip"
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("MoltenVK-macos.tar", self.tar_bytes)
        self.hashes = {
            "ZIP_SHA256": hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            "TAR_SHA256": hashlib.sha256(self.tar_bytes).hexdigest(),
            "DYLIB_SHA256": hashlib.sha256(self.library).hexdigest(),
        }

    def pins(self):
        stack = ExitStack()
        for key, value in self.hashes.items():
            stack.enter_context(patch.object(self.installer, key, value))
        return stack

    def install(self):
        with self.pins():
            return self.installer.install_archive(self.archive, self.output)

    def test_installs_only_the_verified_library_and_records_its_provenance(self):
        metadata = self.install()
        self.assertEqual((self.output / "lib/libMoltenVK.dylib").read_bytes(), self.library)
        self.assertFalse((self.root.parent / "must-not-extract").exists())
        self.assertEqual(metadata["installation"], "github-actions-artifact")
        self.assertEqual(metadata["source_sha"], "176926b4eb7b6bd70f46a59f277b4417dd235f67")
        self.assertEqual(metadata["artifact_id"], 11392189908)
        self.assertEqual(metadata["dylib_sha256"], self.hashes["DYLIB_SHA256"])
        self.assertTrue(metadata["experimental"])

    def test_rejects_each_corrupted_archive_layer_before_installing(self):
        for pin in self.hashes:
            with self.subTest(pin=pin), self.pins(), patch.object(self.installer, pin, "0" * 64):
                with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                    self.installer.install_archive(self.archive, self.output)
                self.assertFalse((self.output / "lib/libMoltenVK.dylib").exists())

    def test_rejects_a_symlink_in_place_of_the_library(self):
        self.make_archive(symlink=True)
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.install()

    def test_rejects_a_different_library_location(self):
        self.make_archive(member="../../libMoltenVK.dylib")
        with self.assertRaisesRegex(ValueError, "missing.*libMoltenVK"):
            self.install()

    def test_cache_validates_the_binary_and_rejects_a_different_revision(self):
        metadata = self.install()
        with self.pins():
            self.assertEqual(self.installer.validate_cache(self.output)["dylib_sha256"],
                             self.hashes["DYLIB_SHA256"])
            (self.output / "lib/libMoltenVK.dylib").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                self.installer.validate_cache(self.output)
            (self.output / "lib/libMoltenVK.dylib").write_bytes(self.library)
            metadata["source_sha"] = "0" * 40
            (self.output / "driver-metadata.json").write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, "revision"):
                self.installer.validate_cache(self.output)

    def test_env_prepends_the_experimental_library_and_rewrites_absolute_icd(self):
        self.install()
        environment = self.root / "github-env"
        with patch.dict(os.environ, {"GITHUB_ENV": str(environment),
                                     "DYLD_LIBRARY_PATH": "/existing/loader"}, clear=True):
            self.installer.configure_environment(self.output)
        settings = dict(line.split("=", 1) for line in environment.read_text().splitlines())
        icd = self.output / "MoltenVK_icd.json"
        self.assertEqual(settings["VK_DRIVER_FILES"], str(icd.resolve()))
        self.assertEqual(settings["VK_ICD_FILENAMES"], str(icd.resolve()))
        self.assertEqual(settings["MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING"], "1")
        self.assertEqual(settings["DYLD_LIBRARY_PATH"],
                         str((self.output / "lib").resolve()) + ":/existing/loader")
        self.assertEqual(settings["HORIZON_CI_DRIVER_METADATA"],
                         str((self.output / "driver-metadata.json").resolve()))
        self.assertEqual(json.loads(icd.read_text())["ICD"]["library_path"],
                         str((self.output / "lib/libMoltenVK.dylib").resolve()))

    def test_env_rejects_newline_in_paths(self):
        self.install()
        with patch.dict(os.environ, {"DYLD_LIBRARY_PATH": "bad\nINJECTED=1"}, clear=True):
            with self.assertRaisesRegex(ValueError, "newline"):
                self.installer.configure_environment(self.output)

    def test_api_redirect_does_not_forward_the_github_token(self):
        requests = []

        class Opener:
            def open(opener_self, request, timeout):
                requests.append(request)
                if len(requests) == 1:
                    raise HTTPError(request.full_url, 302, "Found",
                                    {"Location": "https://signed.example/archive.zip"}, None)
                return io.BytesIO(b"downloaded archive")

        with patch.object(self.installer, "build_opener", return_value=Opener()):
            self.installer.download_artifact(self.root / "download.zip", token="secret-token")
        self.assertEqual((self.root / "download.zip").read_bytes(), b"downloaded archive")
        self.assertEqual(requests[0].get_header("Authorization"), "Bearer secret-token")
        self.assertIsNone(requests[1].get_header("Authorization"))

    def test_existing_cache_is_revalidated_without_network_or_build(self):
        self.install()
        with self.pins(), patch.dict(os.environ, {}, clear=True), \
                patch.object(self.installer, "download_artifact", side_effect=AssertionError("network")), \
                patch.object(self.installer, "build_from_source", side_effect=AssertionError("build")):
            self.installer.setup(self.output)
        self.assertTrue((self.output / "MoltenVK_icd.json").is_file())

    def test_expired_artifact_builds_the_pinned_source_and_reuses_its_binary(self):
        commands = []

        def run(command, *, cwd, **kwargs):
            commands.append(command)
            source = Path(cwd)
            if command == ["git", "checkout", "--detach", "FETCH_HEAD"]:
                revision = source / "ExternalRevisions/SPIRV-Cross_repo_revision"
                revision.parent.mkdir()
                revision.write_text("c1af57390a0dd4950af578ec2135120ac218806f\n")
            if command == ["make", "macos"]:
                library = source / "Package/Latest/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
                library.parent.mkdir(parents=True)
                library.write_bytes(b"built experimental fixture")
            self.assertNotIn("GITHUB_TOKEN", kwargs["env"])

        unavailable = HTTPError("https://secret.example/?token=do-not-print", 410, "Gone", {}, None)
        with patch.dict(os.environ, {"GITHUB_TOKEN": "do-not-print"}, clear=True), \
                patch.object(self.installer, "download_artifact", side_effect=unavailable), \
                patch.object(self.installer.subprocess, "run", side_effect=run), \
                patch.object(self.installer.subprocess, "check_output", side_effect=[
                    "176926b4eb7b6bd70f46a59f277b4417dd235f67\n", "Xcode 26.0\n"]), \
                patch("sys.stdout", new_callable=io.StringIO) as stdout:
            metadata = self.installer.setup(self.output)
        self.assertNotIn("do-not-print", stdout.getvalue())
        self.assertEqual(commands[-2:], [["./fetchDependencies", "--macos"], ["make", "macos"]])
        self.assertIn(["git", "fetch", "--depth", "1", "origin",
                       "176926b4eb7b6bd70f46a59f277b4417dd235f67"], commands)
        self.assertEqual(metadata["installation"], "source-build")
        self.assertEqual(metadata["xcode_version"], "Xcode 26.0")
        self.assertEqual((self.output / "lib/libMoltenVK.dylib").read_bytes(), b"built experimental fixture")
        self.assertEqual(self.installer.validate_cache(self.output)["dylib_sha256"],
                         hashlib.sha256(b"built experimental fixture").hexdigest())

    def test_source_build_failure_is_explicit_and_does_not_install_a_different_driver(self):
        self.output.mkdir()
        with patch.object(self.installer.subprocess, "run",
                          side_effect=[None, subprocess.CalledProcessError(2, ["git"])]), \
                patch("sys.stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(RuntimeError, "source build failed.*source-build.log"):
                self.installer.build_from_source(self.output)
        self.assertFalse((self.output / "driver-metadata.json").exists())
        self.assertFalse((self.output / "lib/libMoltenVK.dylib").exists())
        self.assertTrue((self.output / "source-build.log").is_file())

    def test_missing_distutils_explains_the_upstream_python_requirement(self):
        self.output.mkdir()
        with patch.object(self.installer.subprocess, "run",
                          side_effect=subprocess.CalledProcessError(1, ["python3"])), \
                patch("sys.stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(RuntimeError, "requires python3 with distutils.*Python 3.11"):
                self.installer.build_from_source(self.output)
        self.assertFalse((self.output / "driver-metadata.json").exists())

    def test_source_build_rejects_a_different_checked_out_revision(self):
        self.output.mkdir()
        with patch.object(self.installer.subprocess, "run"), \
                patch.object(self.installer.subprocess, "check_output", return_value="0" * 40), \
                patch("sys.stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(RuntimeError, "source build failed.*ValueError"):
                self.installer.build_from_source(self.output)
        self.assertFalse((self.output / "driver-metadata.json").exists())

    def test_failed_source_build_keeps_upstream_logs_after_temporary_cleanup(self):
        source_paths = []

        def run(command, *, cwd, **kwargs):
            source = Path(cwd)
            source_paths.append(source)
            if command == ["git", "init", "--quiet"]:
                (source / "dependenciesbuild.log").write_text("dependency compiler: missing header\n")
                (source / "xcodebuild.log").write_text("Metal compiler: compilation failed\n")
                (source / "credentials.log").write_text("must not archive unrelated logs")
                (source / "source.cpp").write_text("must not archive source files")
                raise subprocess.CalledProcessError(2, command)

        self.output.mkdir()
        with patch.object(self.installer.subprocess, "run", side_effect=run), \
                patch("sys.stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(RuntimeError, "source build failed"):
                self.installer.build_from_source(self.output)
        self.assertTrue(all(not source.exists() for source in source_paths))
        diagnostics = self.output / "source-diagnostics"
        self.assertEqual((diagnostics / "dependenciesbuild.log").read_text(),
                         "dependency compiler: missing header\n")
        self.assertEqual((diagnostics / "xcodebuild.log").read_text(),
                         "Metal compiler: compilation failed\n")
        self.assertEqual(sorted(path.name for path in diagnostics.iterdir()),
                         ["dependenciesbuild.log", "xcodebuild.log"])

    def test_source_diagnostics_do_not_follow_log_symlinks(self):
        secret = self.root / "credentials"
        secret.write_text("must not archive credentials")

        def run(command, *, cwd, **kwargs):
            if command == ["git", "init", "--quiet"]:
                (Path(cwd) / "dependenciesbuild.log").symlink_to(secret)
                raise subprocess.CalledProcessError(2, command)

        self.output.mkdir()
        with patch.object(self.installer.subprocess, "run", side_effect=run), \
                patch("sys.stdout", new_callable=io.StringIO):
            with self.assertRaisesRegex(RuntimeError, "source build failed"):
                self.installer.build_from_source(self.output)
        self.assertFalse((self.output / "source-diagnostics/dependenciesbuild.log").exists())

    def test_an_integrity_failure_cannot_be_hidden_by_source_fallback(self):
        def download(destination, token):
            shutil.copyfile(self.archive, destination)

        with self.pins(), patch.object(self.installer, "ZIP_SHA256", "0" * 64), \
                patch.object(self.installer, "download_artifact", side_effect=download), \
                patch.object(self.installer, "build_from_source", side_effect=AssertionError("build")):
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                self.installer.setup(self.output)


if __name__ == "__main__":
    unittest.main()
