from conan import ConanFile
from conan.tools.files import copy
from conan.tools.scm import Git


class PfrConan(ConanFile):
    name = "pfr"
    version = "1.91.0"
    package_type = "header-library"
    license = "BSL-1.0"
    homepage = "https://github.com/boostorg/pfr"
    description = "Boost.PFR header-only reflection helpers."
    no_copy_source = True

    def source(self):
        git = Git(self)
        git.clone(url=self.homepage, target=".")
        git.checkout(f"boost-{self.version}")

    def package(self):
        copy(self, "LICENSE_1_0.txt", src=self.source_folder, dst=self.package_folder)
        copy(self, "*", src=f"{self.source_folder}/include", dst=f"{self.package_folder}/include", keep_path=True)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "pfr")
        component = self.cpp_info.components["pfr"]
        component.bindirs = []
        component.libdirs = []
        component.set_property("cmake_target_name", "pfr::pfr")
