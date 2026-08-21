import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class HorizonConan(ConanFile):
    name = "horizon"
    version = "0.5.0"
    settings = "os", "arch", "compiler", "build_type"
    _target_families = (
        "core",
        "tools",
        "examples",
        "ocarina",
        "ocarina-tests",
        "vision-hotfix",
    )

    options = {
        "shared": [True, False],
        "with_ocarina": [True, False],
        "with_vision_hotfix": [True, False],
        "with_cuda": [True, False],
        "with_tools": [True, False],
        "with_examples": [True, False],
        "with_tests": [True, False],
        "with_benchmarks": [True, False],
        "with_ocarina_tests": [True, False],
        "with_hardcode_shaders": [True, False],
        "with_tracy": [True, False],
        "enable_imgui_render": [True, False],
    }

    default_options = {
        "shared": False,
        "with_ocarina": False,
        "with_vision_hotfix": False,
        "with_cuda": False,
        "with_tools": False,
        "with_examples": False,
        "with_tests": False,
        "with_benchmarks": False,
        "with_ocarina_tests": False,
        "with_hardcode_shaders": False,
        "with_tracy": True,
        "enable_imgui_render": True,
        "spirv-tools/*:shared": False,
        "spirv-tools/*:build_executables": False,
        "glfw/*:shared": False,
    }

    def layout(self):
        configuration = str(self.settings.build_type).lower()
        target_family = self.conf.get("user.horizon:target_family", default="examples")
        if target_family not in self._target_families:
            raise ConanInvalidConfiguration(
                f"Unsupported user.horizon:target_family='{target_family}'. "
                f"Expected one of: {', '.join(self._target_families)}"
            )
        cmake_layout(self, build_folder=f"build/conan/{target_family}/{configuration}")

    def requirements(self):
        self.requires("ktm/0.2.14", transitive_headers=True)
        self.requires("pfr/1.91.0", transitive_headers=True)
        self.requires("spirv-tools/1.4.350.0", transitive_headers=True, transitive_libs=True)
        self.requires("volk/1.4.350.0", transitive_headers=True, transitive_libs=True)
        self.requires("vulkan-headers/1.4.350.0", transitive_headers=True)
        self.requires("vulkan-memory-allocator/3.4.0", transitive_headers=True)
        self.requires("quill/11.0.2", transitive_headers=True, transitive_libs=True)
        self.requires("slang/2026.10", transitive_headers=True, transitive_libs=True)
        self.requires("fmt/12.1.0")
        self.requires("spdlog/1.17.0")
        self.requires("xxhash/0.8.3")

        if bool(self.options.with_tracy):
            self.requires("tracy/0.13.1", options={"on_demand": True})

        if bool(self.options.with_examples):
            self.requires("stb/cci.20240531")
            self.requires("glfw/3.4")
            self.requires("tinyobjloader/1.0.7")
            self.requires("glm/1.0.1")
            self.requires("imgui/1.92.8")

    def validate(self):
        if bool(self.options.with_ocarina) and not bool(self.options.with_cuda):
            raise ConanInvalidConfiguration("with_ocarina=True requires with_cuda=True")
        if bool(self.options.with_ocarina) and not os.environ.get("CUDA_PATH"):
            raise ConanInvalidConfiguration("with_ocarina=True requires CUDA_PATH to be set")
        if bool(self.options.with_vision_hotfix) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_vision_hotfix=True requires with_ocarina=True")
        if bool(self.options.with_ocarina_tests) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_ocarina_tests=True requires with_ocarina=True")

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = None
        variables = toolchain.variables
        variables["BUILD_SHARED_LIBS"] = bool(self.options.shared)
        variables["HORIZON_BUILD_OCARINA"] = bool(self.options.with_ocarina and self.options.with_cuda)
        variables["HORIZON_BUILD_VISION_HOTFIX"] = bool(self.options.with_vision_hotfix)
        variables["HORIZON_BUILD_TOOLS"] = bool(self.options.with_tools)
        variables["HORIZON_BUILD_EXAMPLES"] = bool(self.options.with_examples)
        variables["HORIZON_BUILD_TESTS"] = bool(self.options.with_tests)
        variables["HORIZON_BUILD_BENCHMARKS"] = bool(self.options.with_benchmarks)
        variables["HORIZON_BUILD_OCARINA_TESTS"] = bool(self.options.with_ocarina_tests)
        variables["HORIZON_ENABLE_HARDCODE_SHADERS"] = bool(self.options.with_hardcode_shaders)
        variables["HORIZON_ENABLE_TRACY"] = bool(self.options.with_tracy)
        variables["HORIZON_ENABLE_IMGUI_RENDER"] = bool(self.options.enable_imgui_render)
        if bool(self.options.with_examples):
            variables["HORIZON_IMGUI_BINDINGS_DIR"] = os.path.join(
                self.dependencies["imgui"].package_folder,
                "res",
                "bindings",
            ).replace("\\", "/")
        toolchain.generate()
