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
        candidates = []
        fetchcontent_source_root = self._fetchcontent_source_root()
        if fetchcontent_source_root:
            candidates.append(fetchcontent_source_root)
        candidates.extend((
            os.path.join(self.build_folder, "_deps"),
            os.path.join(os.path.dirname(self.build_folder), "_deps"),
            os.path.join(os.path.dirname(os.path.dirname(self.build_folder)), "_deps"),
        ))
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

    def _fetchcontent_source_root(self):
        source_root = os.environ.get("HORIZON_FETCHCONTENT_SOURCE_ROOT")
        if source_root and os.path.isdir(source_root):
            return os.path.normpath(source_root)
        return None

    @staticmethod
    def _env_bool(name, default=False):
        value = os.environ.get(name, "")
        if not value:
            return default
        return value.lower() in ("1", "true", "yes", "on")

    def _editable_build_root(self):
        return os.path.normpath(
            os.environ.get(
                "HORIZON_EDITABLE_BUILD_ROOT",
                os.path.join(self.package_folder, "build"),
            )
        )

    def _is_editable(self):
        return os.path.isfile(os.path.join(self.package_folder, "conanfile.py"))

    def _editable_slang_root(self):
        return os.path.normpath(
            os.environ.get(
                "HORIZON_SLANG_ROOT",
                os.path.join(self.package_folder, "third-party", "slang", "src"),
            )
        )

    def _editable_libdirs(self):
        build_root = self._editable_build_root()
        config = str(self.settings.build_type)
        candidates = [
            os.path.join(build_root, "src", config),
            os.path.join(build_root, "src", "Helicon", config),
            os.path.join(build_root, "modules", "corona", "src", "kernel", config),
            os.path.join(build_root, "modules", "corona", "src", "pal", config),
            os.path.join(build_root, "_deps", "volk-build", config),
            os.path.join(build_root, "_deps", "spirv-cross-build", config),
            os.path.join(build_root, "_deps", "spirv-tools-build", "source", config),
            os.path.join(build_root, "_deps", "spirv-tools-build", "source", "opt", config),
            os.path.join(build_root, "_deps", "spirv-tools-build", "source", "link", config),
            os.path.join(self._editable_slang_root(), "lib"),
        ]
        return [path for path in candidates if os.path.isdir(path)]

    def _shader_tool_path(self):
        shader_tool_name = "ShaderCompileScripts.exe" if self.settings.os == "Windows" else "ShaderCompileScripts"
        if self._is_editable():
            return os.path.join(
                self._editable_build_root(),
                "tools",
                str(self.settings.build_type),
                shader_tool_name,
            )
        return os.path.join(self.package_folder, "bin", shader_tool_name)

    def _spirv_cross_debug_suffix(self):
        return "d" if str(self.settings.build_type) == "Debug" else ""

    def _fetchcontent_source_overrides(self):
        source_root = self._fetchcontent_source_root()
        if not source_root:
            return {}

        dependency_sources = {
            "pfr": "pfr-src",
            "ktm": "ktm-src",
            "preprocessor": "preprocessor-src",
            "SPIRV-Cross": "spirv-cross-src",
            "SPIRV-Headers": "spirv-headers-src",
            "SPIRV-Tools": "spirv-tools-src",
            "volk": "volk-src",
            "Vulkan-Headers": "vulkan-headers-src",
            "VulkanMemoryAllocator": "vulkanmemoryallocator-src",
            "quill": "quill-src",
        }

        overrides = {}
        for cmake_name, source_dir_name in dependency_sources.items():
            source_dir = os.path.join(source_root, source_dir_name)
            if os.path.isdir(source_dir):
                overrides[f"FETCHCONTENT_SOURCE_DIR_{cmake_name.upper()}"] = os.path.normpath(source_dir)
        return overrides

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
        fetchcontent_source_root = self._fetchcontent_source_root()
        fetchcontent_require_source_cache = self._env_bool(
            "HORIZON_FETCHCONTENT_REQUIRE_SOURCE_CACHE",
            default=True,
        )
        if fetchcontent_require_source_cache and not fetchcontent_source_root:
            raise ConanInvalidConfiguration(
                "HORIZON_FETCHCONTENT_REQUIRE_SOURCE_CACHE requires HORIZON_FETCHCONTENT_SOURCE_ROOT"
            )
        toolchain.variables["HORIZON_FETCHCONTENT_REQUIRE_SOURCE_CACHE"] = fetchcontent_require_source_cache
        if fetchcontent_source_root:
            toolchain.variables["HORIZON_FETCHCONTENT_SOURCE_ROOT"] = fetchcontent_source_root.replace("\\", "/")
            for variable_name, source_dir in self._fetchcontent_source_overrides().items():
                toolchain.variables[variable_name] = source_dir.replace("\\", "/")
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
            shader_tool_path = self._shader_tool_path().replace("\\", "/")
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
            "spirv_cross_glsl",
            "spirv_cross_hlsl",
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
        self.cpp_info.components["spirv_cross_c"].libs = [f"spirv-cross-c{self._spirv_cross_debug_suffix()}"]
        self.cpp_info.components["spirv_cross_c"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_cross_cpp"].set_property("cmake_target_name", "spirv-cross-cpp")
        self.cpp_info.components["spirv_cross_cpp"].libs = [f"spirv-cross-cpp{self._spirv_cross_debug_suffix()}"]
        self.cpp_info.components["spirv_cross_cpp"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_cross_core"].set_property("cmake_target_name", "spirv-cross-core")
        self.cpp_info.components["spirv_cross_core"].libs = [f"spirv-cross-core{self._spirv_cross_debug_suffix()}"]

        self.cpp_info.components["spirv_cross_glsl"].set_property("cmake_target_name", "spirv-cross-glsl")
        self.cpp_info.components["spirv_cross_glsl"].libs = [f"spirv-cross-glsl{self._spirv_cross_debug_suffix()}"]
        self.cpp_info.components["spirv_cross_glsl"].requires = ["spirv_cross_core"]

        self.cpp_info.components["spirv_cross_hlsl"].set_property("cmake_target_name", "spirv-cross-hlsl")
        self.cpp_info.components["spirv_cross_hlsl"].libs = [f"spirv-cross-hlsl{self._spirv_cross_debug_suffix()}"]
        self.cpp_info.components["spirv_cross_hlsl"].requires = ["spirv_cross_glsl", "spirv_cross_core"]

        self.cpp_info.components["spirv_cross_util"].set_property("cmake_target_name", "spirv-cross-util")
        self.cpp_info.components["spirv_cross_util"].libs = [f"spirv-cross-util{self._spirv_cross_debug_suffix()}"]
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
            if self._is_editable():
                component.libdirs = self._editable_libdirs()

        if self.settings.compiler == "msvc":
            for component_name in ("horizon", "helicon", "corona_kernel"):
                self.cpp_info.components[component_name].cxxflags = ["/utf-8"]
