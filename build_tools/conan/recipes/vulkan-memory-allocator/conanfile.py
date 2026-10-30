from conan import ConanFile
from conan.tools.files import copy
from conan.tools.scm import Git


class VulkanMemoryAllocatorConan(ConanFile):
    name = "vulkan-memory-allocator"
    version = "3.4.0"
    package_type = "header-library"
    license = "MIT"
    homepage = "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator"
    description = "Header-only Vulkan memory allocation library."
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    def requirements(self):
        self.requires("vulkan-headers/1.4.350.0")

    def source(self):
        git = Git(self)
        git.clone(url=self.homepage, target=".")
        git.checkout(f"v{self.version}")

    def package(self):
        copy(self, "LICENSE.txt", src=self.source_folder, dst=self.package_folder)
        copy(self, "*", src=f"{self.source_folder}/include", dst=f"{self.package_folder}/include", keep_path=True)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "VulkanMemoryAllocator")
        component = self.cpp_info.components["vulkan-memory-allocator"]
        component.bindirs = []
        component.libdirs = []
        component.requires = ["vulkan-headers::vulkan-headers"]
        component.set_property("cmake_target_name", "GPUOpen::VulkanMemoryAllocator")
