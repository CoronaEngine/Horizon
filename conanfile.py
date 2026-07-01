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
        "benchmarks/*",
        "cmake/*",
        "examples/*",
        "include/*",
        "modules/corona/*",
        "modules/ocarina/*",
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
        "with_benchmarks": [True, False],
        "with_ocarina_tests": [True, False],
        "with_ocarina_vulkan": [True, False],
        "with_hardcode_shaders": [True, False],
        "enable_debug_validation": [True, False],
        "enable_relwithdebinfo_validation": [True, False],
        "enable_release_validation": [True, False],
    }

    default_options = {
        "shared": False,
        "with_ocarina": True,
        "with_vision_hotfix": True,
        "with_cuda": True,
        "with_tools": False,
        "with_examples": False,
        "with_tests": False,
        "with_benchmarks": False,
        "with_ocarina_tests": False,
        "with_ocarina_vulkan": False,
        "with_hardcode_shaders": False,
        "enable_debug_validation": True,
        "enable_relwithdebinfo_validation": False,
        "enable_release_validation": False,
        "spirv-cross/*:shared": False,
        "spirv-cross/*:build_executable": False,
        "spirv-tools/*:shared": False,
        "spirv-tools/*:build_executables": False,
        "glfw/*:shared": False,
    }

    def layout(self):
        cmake_layout(self, build_folder="build/conan")

    def set_version(self):
        self.version = os.environ.get("HORIZON_CONAN_VERSION", "0.5.0")

    def requirements(self):
        self.requires("ktm/0.2.14", transitive_headers=True)
        self.requires("pfr/1.91.0", transitive_headers=True)
        self.requires("spirv-cross/1.4.350.0", transitive_headers=True, transitive_libs=True)
        self.requires("spirv-tools/1.4.350.0", transitive_headers=True, transitive_libs=True)
        self.requires("volk/1.4.350.0", transitive_headers=True, transitive_libs=True)
        self.requires("vulkan-headers/1.4.350.0", transitive_headers=True)
        self.requires("vulkan-memory-allocator/3.4.0", transitive_headers=True)
        self.requires("quill/11.0.2", transitive_headers=True, transitive_libs=True)
        self.requires("slang/2026.10", transitive_headers=True, transitive_libs=True)

        if bool(self.options.with_examples):
            self.requires("stb/cci.20240531")
            self.requires("glfw/3.4")
            self.requires("tinyobjloader/1.0.7")
            self.requires("glm/1.0.1")

        if bool(self.options.with_ocarina) and bool(self.options.with_cuda):
            self.requires("fmt/12.1.0")
            self.requires("spdlog/1.17.0")
            self.requires("xxhash/0.8.3")

    @staticmethod
    def _copy_headers(conanfile, src, dst):
        for pattern in ("*.h", "*.hpp", "*.inl"):
            copy(conanfile, pattern, src=src, dst=dst, keep_path=True)

    def _editable_build_root(self):
        return os.path.normpath(
            os.environ.get(
                "HORIZON_EDITABLE_BUILD_ROOT",
                os.path.join(self.package_folder, "build"),
            )
        )

    def _is_editable(self):
        return os.path.isfile(os.path.join(self.package_folder, "conanfile.py"))

    def _editable_includedirs(self):
        source_root = self.package_folder
        candidates = [
            os.path.join(source_root, "include"),
            os.path.join(source_root, "src", "Helicon"),
            os.path.join(source_root, "modules", "corona", "include"),
        ]

        return [path for path in candidates if os.path.isdir(path)]

    def _editable_libdirs(self):
        build_root = self._editable_build_root()
        config = str(self.settings.build_type)
        candidates = [
            os.path.join(build_root, "src", config),
            os.path.join(build_root, "src", "Helicon", config),
            os.path.join(build_root, "modules", "corona", "src", "kernel", config),
            os.path.join(build_root, "modules", "corona", "src", "pal", config),
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

    def validate(self):
        if bool(self.options.with_ocarina) and not bool(self.options.with_cuda):
            raise ConanInvalidConfiguration("with_ocarina=True requires with_cuda=True")
        if bool(self.options.with_ocarina) and not os.environ.get("CUDA_PATH"):
            raise ConanInvalidConfiguration("with_ocarina=True requires CUDA_PATH to be set")
        if bool(self.options.with_vision_hotfix) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_vision_hotfix=True requires with_ocarina=True")
        if bool(self.options.with_ocarina_tests) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_ocarina_tests=True requires with_ocarina=True")
        if bool(self.options.with_ocarina_vulkan) and not bool(self.options.with_ocarina):
            raise ConanInvalidConfiguration("with_ocarina_vulkan=True requires with_ocarina=True")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        cache_variables = toolchain.cache_variables
        cache_variables["BUILD_SHARED_LIBS"] = bool(self.options.shared)
        cache_variables["HORIZON_BUILD_OCARINA"] = bool(self.options.with_ocarina and self.options.with_cuda)
        cache_variables["HORIZON_BUILD_VISION_HOTFIX"] = bool(self.options.with_vision_hotfix)
        cache_variables["HORIZON_BUILD_TOOLS"] = bool(self.options.with_tools)
        cache_variables["HORIZON_BUILD_EXAMPLES"] = bool(self.options.with_examples)
        cache_variables["HORIZON_BUILD_TESTS"] = bool(self.options.with_tests)
        cache_variables["HORIZON_BUILD_BENCHMARKS"] = bool(self.options.with_benchmarks)
        cache_variables["HORIZON_BUILD_OCARINA_TESTS"] = bool(self.options.with_ocarina_tests)
        cache_variables["HORIZON_ENABLE_HARDCODE_SHADERS"] = bool(self.options.with_hardcode_shaders)
        cache_variables["HORIZON_ENABLE_DEBUG_VALIDATION"] = bool(self.options.enable_debug_validation)
        cache_variables["HORIZON_ENABLE_RELWITHDEBINFO_VALIDATION"] = bool(
            self.options.enable_relwithdebinfo_validation
        )
        cache_variables["HORIZON_ENABLE_RELEASE_VALIDATION"] = bool(self.options.enable_release_validation)
        if bool(self.options.with_ocarina_vulkan):
            cache_variables["VISION_BUILD_VULKAN"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="Horizon")
        if bool(self.options.with_ocarina) and bool(self.options.with_cuda):
            cmake.build(target="ocarina")
        if bool(self.options.with_vision_hotfix):
            cmake.build(target="vision-hotfix-all")
        if bool(self.options.with_tools):
            cmake.build(target="ShaderCompileScripts")

    def package(self):
        package_include = os.path.join(self.package_folder, "include")
        self._copy_headers(self, os.path.join(self.source_folder, "include"), package_include)
        self._copy_headers(self, os.path.join(self.source_folder, "src", "Helicon"), package_include)
        self._copy_headers(self, os.path.join(self.source_folder, "modules", "corona", "include"), package_include)

        package_cmake = os.path.join(self.package_folder, "cmake")
        copy(self, "HeliconShaderCompile.cmake", src=os.path.join(self.source_folder, "cmake"), dst=package_cmake)
        copy(self, "HorizonPackageAliases.cmake", src=os.path.join(self.source_folder, "cmake"), dst=package_cmake)
        copy(self, "*.lib", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        copy(self, "*.a", src=self.build_folder, dst=os.path.join(self.package_folder, "lib"), keep_path=False)
        if bool(self.options.with_tools):
            tool_output_dir = os.path.join(self.build_folder, "tools", str(self.settings.build_type))
            copy(self, "ShaderCompileScripts*", src=tool_output_dir, dst=os.path.join(self.package_folder, "bin"), keep_path=False)
            copy(self, "*.dll", src=tool_output_dir, dst=os.path.join(self.package_folder, "bin"), keep_path=False)

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
        self.cpp_info.components["horizon"].requires = [
            "helicon",
            "corona_kernel",
            "ktm::ktm",
            "volk::libvolk",
            "vulkan-headers::vulkan-headers",
            "vulkan-memory-allocator::vulkan-memory-allocator",
        ]

        self.cpp_info.components["helicon"].set_property("cmake_target_name", "Helicon")
        self.cpp_info.components["helicon"].libs = ["Helicon"]
        self.cpp_info.components["helicon"].requires = [
            "ktm::ktm",
            "pfr::pfr",
            "spirv-cross::spirv-cross-c",
            "spirv-cross::spirv-cross-cpp",
            "spirv-cross::spirv-cross-core",
            "spirv-cross::spirv-cross-glsl",
            "spirv-cross::spirv-cross-hlsl",
            "spirv-cross::spirv-cross-util",
            "spirv-tools::spirv-tools-link",
            "slang::slang",
            "slang::slang-rt",
            "slang::gfx",
        ]

        self.cpp_info.components["corona_kernel"].set_property("cmake_target_name", "corona_kernel")
        self.cpp_info.components["corona_kernel"].libs = ["corona_kernel"]
        self.cpp_info.components["corona_kernel"].requires = ["quill::quill"]

        self.cpp_info.components["corona_pal"].set_property("cmake_target_name", "corona_pal")

        editable = self._is_editable()
        editable_includedirs = self._editable_includedirs() if editable else None
        editable_libdirs = self._editable_libdirs() if editable else None
        for component in self.cpp_info.components.values():
            component.includedirs = editable_includedirs if editable else ["include"]
            if editable:
                component.libdirs = editable_libdirs

        if self.settings.compiler == "msvc":
            for component_name in ("horizon", "helicon", "corona_kernel"):
                self.cpp_info.components[component_name].cxxflags = ["/utf-8"]
