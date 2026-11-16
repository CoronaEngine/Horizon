#!/usr/bin/env python3
"""Install a pinned experimental ray-query MoltenVK for macOS CI.

The official PR artifact is verified at ZIP, tar and dylib layers. After its
expiry, build the same source revision using the upstream documented commands:
https://github.com/dttdrv/MoltenVK/blob/176926b4eb7b6bd70f46a59f277b4417dd235f67/README.md
No stable, non-ray-query runtime is substituted for this driver.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, Request, build_opener
import zipfile


SOURCE_REPOSITORY = "dttdrv/MoltenVK"
SOURCE_SHA = "176926b4eb7b6bd70f46a59f277b4417dd235f67"
SPIRV_CROSS_SHA = "c1af57390a0dd4950af578ec2135120ac218806f"
RUN_ID = 37418935153
ARTIFACT_ID = 11392189908
ZIP_SHA256 = "df7e6699e254d8327bd17ec13137aa53374de2455fcd829492d05688d3868c36"
TAR_SHA256 = "e2457b0a4544e44736a141031523de2e744c808a7c8104fa7357a7599ae91387"
DYLIB_SHA256 = "aa343de20514463baebb0e4698e21a139615335c6d1873ea274b385ca50bf02a"
VERSION = "1.4.3 (experimental PR, unreleased)"
LIBRARY_MEMBER = "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
ARTIFACT_URL = f"https://api.github.com/repos/KhronosGroup/MoltenVK/actions/artifacts/{ARTIFACT_ID}/zip"


def verify_hash(data, expected, label):
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")
    return actual


def record_install(output, library, installation, **details):
    output = output.resolve()
    destination = output / "lib/libMoltenVK.dylib"
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".download")
    temporary.write_bytes(library)
    temporary.replace(destination)
    metadata = {
        "driver": "MoltenVK", "version": VERSION, "experimental": True,
        "source_repository": SOURCE_REPOSITORY, "source_sha": SOURCE_SHA,
        "spirv_cross_revision": SPIRV_CROSS_SHA,
        "source_pr": "https://github.com/KhronosGroup/MoltenVK/pull/2771",
        "installation": installation,
        "dylib_sha256": hashlib.sha256(library).hexdigest(),
        **details,
    }
    (output / "driver-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def install_archive(archive, output):
    archive_data = archive.read_bytes()
    verify_hash(archive_data, ZIP_SHA256, "artifact ZIP")
    with zipfile.ZipFile(io.BytesIO(archive_data)) as package:
        members = [entry for entry in package.infolist() if entry.filename == "MoltenVK-macos.tar"]
        if len(members) != 1:
            raise ValueError("artifact ZIP must contain exactly one MoltenVK-macos.tar")
        tar_data = package.read(members[0])
    verify_hash(tar_data, TAR_SHA256, "MoltenVK tar")
    # Read just the known dylib. Do not extract archive paths or framework links.
    with tarfile.open(fileobj=io.BytesIO(tar_data)) as package:
        members = [entry for entry in package.getmembers() if entry.name == LIBRARY_MEMBER]
        if len(members) != 1:
            raise ValueError(f"missing or duplicate {LIBRARY_MEMBER}")
        if not members[0].isfile():
            raise ValueError("libMoltenVK.dylib must be a regular file")
        library = package.extractfile(members[0]).read()
    verify_hash(library, DYLIB_SHA256, "MoltenVK dylib")
    return record_install(output, library, "github-actions-artifact",
                          artifact_id=ARTIFACT_ID, artifact_name="MoltenVK-macos",
                          run_url=f"https://github.com/KhronosGroup/MoltenVK/actions/runs/{RUN_ID}",
                          artifact_zip_sha256=ZIP_SHA256, tar_sha256=TAR_SHA256)


def validate_cache(output):
    metadata = json.loads((output / "driver-metadata.json").read_text())
    if (metadata.get("source_sha") != SOURCE_SHA or
            metadata.get("source_repository") != SOURCE_REPOSITORY or
            metadata.get("spirv_cross_revision") != SPIRV_CROSS_SHA or
            metadata.get("experimental") is not True):
        raise ValueError("cached MoltenVK has an unexpected source revision or experimental setting")
    installation = metadata.get("installation")
    if installation == "github-actions-artifact":
        if (metadata.get("artifact_id") != ARTIFACT_ID or
                metadata.get("artifact_zip_sha256") != ZIP_SHA256 or
                metadata.get("tar_sha256") != TAR_SHA256 or
                metadata.get("dylib_sha256") != DYLIB_SHA256):
            raise ValueError("cached MoltenVK artifact provenance does not match the pinned artifact")
    elif installation != "source-build":
        raise ValueError("cached MoltenVK has an unknown installation source")
    verify_hash((output / "lib/libMoltenVK.dylib").read_bytes(),
                metadata.get("dylib_sha256"), "cached MoltenVK dylib")
    return metadata


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


def download_artifact(destination, token=None):
    """Authenticate only the initial GitHub API request, never signed blob URLs."""
    opener = build_opener(NoRedirect())
    url = ARTIFACT_URL
    temporary = destination.with_suffix(".download")
    try:
        for redirect in range(6):
            headers = {"Accept": "application/vnd.github+json", "User-Agent": "Horizon-MoltenVK-CI"}
            if redirect == 0 and token:
                headers["Authorization"] = "Bearer " + token
            request = Request(url, headers=headers)
            try:
                with opener.open(request, timeout=120) as response, temporary.open("wb") as stream:
                    shutil.copyfileobj(response, stream)
                temporary.replace(destination)
                return
            except HTTPError as error:
                if error.code not in (301, 302, 303, 307, 308):
                    raise
                url = error.headers.get("Location", "")
                parsed = urlparse(url)
                if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
                    raise ValueError("artifact redirect must be an HTTPS URL without credentials") from None
        raise ValueError("too many artifact download redirects")
    finally:
        temporary.unlink(missing_ok=True)


def build_from_source(output):
    """Fallback to the fixed source and its pinned dependencies, never stable MoltenVK."""
    output.mkdir(parents=True, exist_ok=True)
    log_path = output / "source-build.log"
    environment = os.environ.copy()
    environment.pop("GITHUB_TOKEN", None)
    environment.pop("GH_TOKEN", None)
    with tempfile.TemporaryDirectory(prefix="moltenvk-source-", dir=output) as temporary, \
            log_path.open("w") as log:
        source = Path(temporary)

        def run(command):
            print("MoltenVK source build:", " ".join(command), flush=True)
            subprocess.run(command, cwd=source, env=environment, check=True,
                           stdout=log, stderr=subprocess.STDOUT, timeout=3600)

        try:
            # Upstream CI uses Python 3.11: glslang's dependency updater imports
            # distutils. Check the actual python3 used by fetchDependencies.
            try:
                run(["python3", "-c", "import distutils"])
            except subprocess.CalledProcessError:
                raise RuntimeError("Pinned MoltenVK source build requires python3 with distutils "
                                   f"(upstream uses Python 3.11); see {log_path}.") from None
            run(["git", "init", "--quiet"])
            run(["git", "remote", "add", "origin", f"https://github.com/{SOURCE_REPOSITORY}.git"])
            run(["git", "fetch", "--depth", "1", "origin", SOURCE_SHA])
            run(["git", "checkout", "--detach", "FETCH_HEAD"])
            revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source,
                                               env=environment, text=True).strip()
            if revision != SOURCE_SHA:
                raise ValueError("source checkout revision differs from the pinned MoltenVK revision")
            cross = (source / "ExternalRevisions/SPIRV-Cross_repo_revision").read_text().strip()
            if cross != SPIRV_CROSS_SHA:
                raise ValueError("source checkout has an unexpected SPIRV-Cross revision")
            # README: fetchDependencies --macos, then Makefile's macos package target.
            run(["./fetchDependencies", "--macos"])
            run(["make", "macos"])
            library = (source / "Package/Latest/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib").read_bytes()
            if not library:
                raise ValueError("source build produced an empty MoltenVK dylib")
            xcode = subprocess.check_output(["xcodebuild", "-version"], env=environment, text=True).strip()
            return record_install(output, library, "source-build", xcode_version=xcode,
                                  build_commands=["./fetchDependencies --macos", "make macos"])
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            raise RuntimeError(f"Pinned experimental MoltenVK source build failed ({type(error).__name__}); "
                               f"see {log_path}. No non-RT driver was installed.") from None
        finally:
            # fetchDependencies returns to the checkout root before tee-ing
            # dependenciesbuild.log; Makefile writes xcodebuild.log there too.
            # Preserve only these known logs before TemporaryDirectory cleans up.
            diagnostics = output / "source-diagnostics"
            for name in ("dependenciesbuild.log", "xcodebuild.log"):
                upstream_log = source / name
                if upstream_log.is_symlink() or not upstream_log.is_file():
                    continue
                try:
                    diagnostics.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(upstream_log, diagnostics / name)
                    print(f"Saved source diagnostic: {diagnostics / name}", file=log)
                except OSError as error:
                    # A diagnostic-copy failure must not hide the build failure.
                    print(f"Could not preserve {name}: {type(error).__name__}", file=log)


def configure_environment(output):
    output = output.resolve()
    icd = output / "MoltenVK_icd.json"
    library = output / "lib/libMoltenVK.dylib"
    settings = {
        "VK_DRIVER_FILES": str(icd), "VK_ICD_FILENAMES": str(icd),
        "MVK_CONFIG_ENABLE_EXPERIMENTAL_RAY_TRACING": "1",
        "DYLD_LIBRARY_PATH": str(library.parent) +
                             (":" + os.environ["DYLD_LIBRARY_PATH"] if os.environ.get("DYLD_LIBRARY_PATH") else ""),
        "HORIZON_CI_DRIVER_METADATA": str(output / "driver-metadata.json"),
    }
    if any("\n" in value or "\r" in value for value in settings.values()):
        raise ValueError("driver environment paths cannot contain a newline")
    icd.write_text(json.dumps({"file_format_version": "1.0.0", "ICD": {
        "library_path": str(library), "api_version": "1.4.0", "is_portability_driver": True}}, indent=2) + "\n")
    if path := os.environ.get("GITHUB_ENV"):
        with Path(path).open("a") as stream:
            stream.writelines(f"{key}={value}\n" for key, value in settings.items())
    return settings


def setup(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / "driver-metadata.json").is_file():
        metadata = validate_cache(output)
    else:
        archive = output / "MoltenVK-macos.zip"
        try:
            download_artifact(archive, token=os.environ.get("GITHUB_TOKEN"))
        except (HTTPError, URLError, OSError) as error:
            # HTTP exceptions include signed URLs; never echo exception text or tokens.
            reason = f"HTTP {error.code}" if isinstance(error, HTTPError) else type(error).__name__
            print(f"Pinned MoltenVK artifact unavailable ({reason}); building source {SOURCE_SHA}.", flush=True)
            metadata = build_from_source(output)
        else:
            metadata = install_archive(archive, output)
        finally:
            archive.unlink(missing_ok=True)
    configure_environment(output)
    print(f"MoltenVK {VERSION}: {metadata['installation']}, source={SOURCE_SHA}, "
          f"dylib_sha256={metadata['dylib_sha256']}", flush=True)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        setup(arguments.output)
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile, tarfile.TarError) as error:
        print(f"MoltenVK CI setup failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
