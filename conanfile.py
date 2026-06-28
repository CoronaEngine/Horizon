import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy


class HorizonConan(ConanFile):
    name = "horizon"
    package_type = "library"
    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "modules/corona/*",
        "src/*",
        "tools/*",
    )

    settings = "os", "arch", "compiler", "build_type"

    options = {
        "shared": [True, False],
        "with_ocarina": [True, False],
        "with_vision_hotfix": [True, False],
        "with_cuda": [True, False],
        "with_tools": [True, False],
        "with_examples": [True, False],
        "with_tests": [True, False],
    }

    default_options = {
        "shared": False,
        "with_ocarina": False,
        "with_vision_hotfix": False,
        "with_cuda": False,
        "with_tools": False,
        "with_examples": False,
        "with_tests": False,
    }

    def layout(self):
        cmake_layout(self, build_folder="build/conan")

    def set_version(self):
        self.version = os.environ.get("HORIZON_CONAN_VERSION", "0.5.0")

    @staticmethod
    def _copy_headers(conanfile, src, dst):
        for pattern in ("*.h", "*.hpp", "*.inl"):
            copy(conanfile, pattern, src=src, dst=dst, keep_path=True)

    def _build_deps_root(self):
        candidates = (
            os.path.join(self.build_folder, "_deps"),
            os.path.join(os.path.dirname(self.build_folder), "_deps"),
            os.path.join(os.path.dirname(os.path.dirname(self.build_folder)), "_deps"),
        )
        for candidate in candidates:
            if os.path.isdir(os.path.join(candidate, "ktm-src")):
                return candidate
        return None

    def _slang_root(self):
        return os.path.normpath(
            os.environ.get(
                "HORIZON_SLANG_ROOT",
                os.path.join(self.source_folder, "third-party", "slang", "src"),
            )
        )

    def validate(self):
        if bool(self.options.with_ocarina) and not bool(self.options.with_cuda):
            raise ConanInvalidConfiguration("with_ocarina=True requires with_cuda=True")
        if bool(self.options.with_vision_hotfix) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_vision_hotfix=True requires with_ocarina=True")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = bool(self.options.shared)
        toolchain.variables["HORIZON_BUILD_OCARINA"] = bool(self.options.with_ocarina and self.options.with_cuda)
        toolchain.variables["HORIZON_BUILD_VISION_HOTFIX"] = bool(self.options.with_vision_hotfix)
        toolchain.variables["HORIZON_BUILD_TOOLS"] = bool(self.options.with_tools)
        toolchain.variables["HORIZON_BUILD_EXAMPLES"] = bool(self.options.with_examples)
        toolchain.variables["HORIZON_BUILD_TESTS"] = bool(self.options.with_tests)
        toolchain.variables["HORIZON_BUILD_BENCHMARKS"] = False
        toolchain.variables["HORIZON_ENABLE_DEPENDENCY_INSTALL"] = False
        if os.path.isdir(self._slang_root()):
            toolchain.variables["HORIZON_SLANG_ROOT"] = self._slang_root().replace("\\", "/")
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="Horizon")
        if bool(self.options.with_tools):
            cmake.build(target="ShaderCompileScripts")

    def package(self):
        package_include = os.path.join(self.package_folder, "include")
        self._copy_headers(self, os.path.join(self.source_folder, "include"), package_include)
        self._copy_headers(self, os.path.join(self.source_folder, "src", "Helicon"), package_include)
        self._copy_headers(self, os.path.join(self.source_folder, "modules", "corona", "include"), package_include)

        deps_root = self._build_deps_root()
        if deps_root:
            for dep_include in (
                os.path.join(deps_root, "ktm-src"),
                os.path.join(deps_root, "pfr-src", "include"),
                os.path.join(deps_root, "quill-src", "include"),
                os.path.join(deps_root, "spirv-headers-src", "include"),
                os.path.join(deps_root, "spirv-tools-src", "include"),
                os.path.join(deps_root, "vulkan-headers-src", "include"),
                os.path.join(deps_root, "vulkanmemoryallocator-src", "include"),
            ):
                if os.path.isdir(dep_include):
                    self._copy_headers(self, dep_include, package_include)

        slang_root = self._slang_root()
        self._copy_headers(self, os.path.join(slang_root, "include"), package_include)

        package_cmake = os.path.join(self.package_folder, "cmake")
        copy(self, "HeliconShaderCompile.cmake", src=os.path.join(self.source_folder, "cmake"), dst=package_cmake)
        copy(self, "HorizonPackageAliases.cmake", src=os.path.join(self.source_folder, "cmake"), dst=package_cmake)
        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.lib", src=os.path.join(slang_root, "lib"), dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=os.path.join(slang_root, "lib"), dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.dll", src=os.path.join(slang_root, "bin"), dst=os.path.join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.dylib", src=os.path.join(slang_root, "bin"), dst=os.path.join(self.package_folder, "bin"), keep_path=False)
        copy(self, "*.so*", src=os.path.join(slang_root, "bin"), dst=os.path.join(self.package_folder, "bin"), keep_path=False)
        if bool(self.options.with_tools):
            tool_output_dir = os.path.join(self.build_folder, "tools", str(self.settings.build_type))
            copy(self, "ShaderCompileScripts*", src=tool_output_dir, dst=os.path.join(self.package_folder, "bin"), keep_path=False)

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "Horizon")
        self.cpp_info.set_property("cmake_target_name", "Horizon::Horizon")
        self.cpp_info.set_property(
            "cmake_build_modules",
            [
                os.path.join("cmake", "HeliconShaderCompile.cmake"),
                os.path.join("cmake", "HorizonPackageAliases.cmake"),
            ],
        )
        if bool(self.options.with_tools):
            shader_tool_name = "ShaderCompileScripts.exe" if self.settings.os == "Windows" else "ShaderCompileScripts"
            shader_tool_path = os.path.join(self.package_folder, "bin", shader_tool_name).replace("\\", "/")
            self.cpp_info.set_property(
                "cmake_extra_variables",
                {"HORIZON_SHADER_COMPILE_SCRIPTS_EXECUTABLE": shader_tool_path},
            )

        self.cpp_info.components["horizon"].set_property("cmake_target_name", "Horizon")
        self.cpp_info.components["horizon"].libs = ["Horizon"]
        self.cpp_info.components["horizon"].requires = ["helicon", "corona_kernel", "volk"]

        self.cpp_info.components["helicon"].set_property("cmake_target_name", "Helicon")
        self.cpp_info.components["helicon"].libs = ["Helicon", "gfx", "slang", "slang-rt"]
        self.cpp_info.components["helicon"].requires = [
            "spirv_cross_c",
            "spirv_cross_cpp",
            "spirv_cross_core",
            "spirv_cross_util",
            "spirv_tools_link",
        ]

        self.cpp_info.components["corona_kernel"].set_property("cmake_target_name", "corona_kernel")
        self.cpp_info.components["corona_kernel"].libs = ["corona_kernel"]
        self.cpp_info.components["corona_kernel"].requires = ["quill"]

        self.cpp_info.components["corona_pal"].set_property("cmake_target_name", "corona_pal")

        self.cpp_info.components["quill"].set_property("cmake_target_name", "quill::quill")

        self.cpp_info.components["volk"].set_property("cmake_target_name", "volk")
        self.cpp_info.components["volk"].libs = ["volk"]

        self.cpp_info.components["spirv_cross_c"].set_property("cmake_target_name", "spirv-cross-c")
        self.cpp_info.components["spirv_cross_c"].libs = ["spirv-cross-c"]
        self.cpp_info.components["spirv_cross_c"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_cross_cpp"].set_property("cmake_target_name", "spirv-cross-cpp")
        self.cpp_info.components["spirv_cross_cpp"].libs = ["spirv-cross-cpp"]
        self.cpp_info.components["spirv_cross_cpp"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_cross_core"].set_property("cmake_target_name", "spirv-cross-core")
        self.cpp_info.components["spirv_cross_core"].libs = ["spirv-cross-core"]

        self.cpp_info.components["spirv_cross_util"].set_property("cmake_target_name", "spirv-cross-util")
        self.cpp_info.components["spirv_cross_util"].libs = ["spirv-cross-util"]
        self.cpp_info.components["spirv_cross_util"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_tools"].set_property("cmake_target_name", "SPIRV-Tools")
        self.cpp_info.components["spirv_tools"].libs = ["SPIRV-Tools"]

        self.cpp_info.components["spirv_tools_opt"].set_property("cmake_target_name", "SPIRV-Tools-opt")
        self.cpp_info.components["spirv_tools_opt"].libs = ["SPIRV-Tools-opt"]
        self.cpp_info.components["spirv_tools_opt"].requires = ["spirv_tools"]

        self.cpp_info.components["spirv_tools_link"].set_property("cmake_target_name", "SPIRV-Tools-link")
        self.cpp_info.components["spirv_tools_link"].libs = ["SPIRV-Tools-link"]
        self.cpp_info.components["spirv_tools_link"].requires = ["spirv_tools_opt", "spirv_tools"]

        for component in self.cpp_info.components.values():
            component.includedirs = ["include"]

        if self.settings.compiler == "msvc":
            for component_name in ("horizon", "helicon", "corona_kernel"):
                self.cpp_info.components[component_name].cxxflags = ["/utf-8"]
