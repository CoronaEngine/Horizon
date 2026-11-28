import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get


class SlangConan(ConanFile):
    name = "slang"
    version = "2026.11"
    package_type = "shared-library"
    license = "Apache-2.0 WITH LLVM-exception"
    homepage = "https://github.com/shader-slang/slang"
    description = "Slang shading language compiler and runtime binary SDK."
    settings = "os", "arch"

    _assets = {
        ("Windows", "x86_64"): (
            "windows-x86_64",
            "e070e239290108dd7a83094cf72fb94ae753b2d5331adba76874bf17dd5c8b6a",
        ),
        ("Windows", "armv8"): (
            "windows-aarch64",
            "8380746b38cff5c727f7ff6a73a9d9608eee7230a886540244b977af3596d8ec",
        ),
        ("Macos", "x86_64"): (
            "macos-x86_64",
            "764ae0e20714930db2e319f7e6339b233d4a7d668dc9bfa2f37772838d6ecaaa",
        ),
        ("Macos", "armv8"): (
            "macos-aarch64",
            "595f06cb9c1c306609cd61d9ced20dc75d56c0153d7c1bf1e509834edf133c75",
        ),
        ("Linux", "x86_64"): (
            "linux-x86_64",
            "d83ef21fc08d2c035ed15b5ce7541f27de8ec8d1db42b27c72bdc1567e5c4b37",
        ),
        ("Linux", "armv8"): (
            "linux-aarch64",
            "d59c474d0f63c670b2e4a8134d7a95c12a35ea62cbe74e2d00afa71f8134c7d0",
        ),
    }

    def _asset(self):
        return self._assets.get((str(self.settings.os), str(self.settings.arch)))

    def validate(self):
        if not self._asset():
            raise ConanInvalidConfiguration(
                f"slang/{self.version} binary SDK is not configured for "
                f"{self.settings.os}/{self.settings.arch}"
            )

    def _repair_package(self):
        # zipfile 解包不还原 symlink 与执行位：给库添加可执行权限，
        for sub in ("lib", "bin"):
            for dirpath, _, filenames in os.walk(os.path.join(self.package_folder, sub)):
                for filename in filenames:
                    path = os.path.join(dirpath, filename)
                    if sub == "bin":
                        os.chmod(path, 0o755)
                    try:
                        with open(path, "rb") as f:
                            target = f.read(4096).decode("utf-8").strip()
                        if target and os.path.exists(os.path.normpath(os.path.join(dirpath, target))):
                            os.remove(path)
                            os.symlink(target, path)
                    except (OSError, UnicodeDecodeError, ValueError):
                        continue

    def package(self):
        asset_suffix, sha256 = self._asset()
        asset_name = f"slang-{self.version}-{asset_suffix}.zip"
        sdk_dir = os.path.join(self.build_folder, "sdk")
        get(
            self,
            url=(os.environ.get("SLANG_DOWNLOAD_BASE") or self.homepage)
                + f"/releases/download/v{self.version}/{asset_name}",
            sha256=sha256,
            destination=sdk_dir,
            strip_root=False,
        )

        copy(self, "LICENSE", src=sdk_dir, dst=os.path.join(self.package_folder, "licenses"), keep_path=False)
        copy(self, "*", src=os.path.join(sdk_dir, "include"), dst=os.path.join(self.package_folder, "include"), keep_path=True)
        copy(self, "*", src=os.path.join(sdk_dir, "lib"), dst=os.path.join(self.package_folder, "lib"), keep_path=True)
        copy(self, "*", src=os.path.join(sdk_dir, "bin"), dst=os.path.join(self.package_folder, "bin"), keep_path=True)

        self._repair_package()

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "slang")
        self.cpp_info.set_property("cmake_target_name", "slang::slang_package")
        self.cpp_info.bindirs = ["bin"]
        self.cpp_info.libdirs = ["lib"]
        self.cpp_info.includedirs = ["include"]

        self.cpp_info.components["slang"].set_property("cmake_target_name", "slang::slang")
        self.cpp_info.components["slang"].libs = ["slang"]
        self.cpp_info.components["slang"].defines = ["SLANG_DYNAMIC"]

        self.cpp_info.components["slang-rt"].set_property("cmake_target_name", "slang::slang-rt")
        self.cpp_info.components["slang-rt"].libs = ["slang-rt"]

        self.cpp_info.components["gfx"].set_property("cmake_target_name", "slang::gfx")
        self.cpp_info.components["gfx"].libs = ["gfx"]
        self.cpp_info.components["gfx"].defines = ["SLANG_GFX_DYNAMIC"]
        self.cpp_info.components["gfx"].requires = ["slang"]

        self.cpp_info.components["slang-compiler"].set_property("cmake_target_name", "slang::slang-compiler")
        self.cpp_info.components["slang-compiler"].libs = ["slang-compiler"]
        self.cpp_info.components["slang-compiler"].requires = ["slang"]

        self.runenv_info.prepend_path("PATH", os.path.join(self.package_folder, "bin"))
