import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get


class SlangConan(ConanFile):
    name = "slang"
    version = "2026.10"
    package_type = "shared-library"
    license = "Apache-2.0 WITH LLVM-exception"
    homepage = "https://github.com/shader-slang/slang"
    description = "Slang shading language compiler and runtime binary SDK."
    settings = "os", "arch"

    _assets = {
        ("Windows", "x86_64"): (
            "windows-x86_64",
            "4d681fd6c40a028939d4907d714fb633a16895bd7ae8b8ef288401b805c17aa4",
        ),
        ("Windows", "armv8"): (
            "windows-aarch64",
            "54b88155e5d94ddf63ef5013a59a8a49f35a7b415048fc57078d9eecdf2ccc7d",
        ),
        ("Macos", "x86_64"): (
            "macos-x86_64",
            "3195d5811f64ab0b95b9c27da36ac3f3f4f49429225ce5c4ffa14ab5962882ce",
        ),
        ("Macos", "armv8"): (
            "macos-aarch64",
            "70c63b66448d1d4d3ae770f3cdacdb00674e0f81ee44b52bd0dde6f2f49273db",
        ),
    }

    def _asset(self):
        return self._assets.get((str(self.settings.os), str(self.settings.arch)))

    def validate(self):
        if not self._asset():
            raise ConanInvalidConfiguration(
                f"slang/{self.version} binary SDK is only configured for Windows/Macos x86_64 and armv8"
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
            url=f"{self.homepage}/releases/download/v{self.version}/{asset_name}",
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
