from conan import ConanFile
from conan.tools.files import copy
from conan.tools.scm import Git


class KtmConan(ConanFile):
    name = "ktm"
    version = "0.2.14"
    package_type = "header-library"
    license = "MIT"
    homepage = "https://github.com/YGXXD/ktm"
    description = "Header-only SIMD-friendly math library."
    no_copy_source = True

    def source(self):
        git = Git(self)
        git.clone(url=self.homepage, target=".")
        git.checkout(f"v{self.version}")

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=self.package_folder)
        copy(self, "*.h", src=self.source_folder, dst=self.package_folder, keep_path=True)
        copy(self, "*.inl", src=self.source_folder, dst=self.package_folder, keep_path=True)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ktm")
        component = self.cpp_info.components["ktm"]
        component.bindirs = []
        component.libdirs = []
        component.includedirs = ["."]
        component.set_property("cmake_target_name", "ktm::ktm")
