#include "horizon/hotfix/cmake_build_backend.h"
#include "horizon/hotfix/windows_process_runner.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace horizon::hotfix;
namespace fs = std::filesystem;

constexpr std::string_view original_source =
    "extern \"C\" __declspec(dllexport) int fixture_value() { return 17; }\n";

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class FixtureDirectory {
public:
    explicit FixtureDirectory(const fs::path &parent)
        : path(fs::canonical(parent) / (L"cmake backend fixture " + std::to_wstring(GetCurrentProcessId()))) {
        require(fs::create_directory(path), "create an isolated fixture directory");
    }

    ~FixtureDirectory() {
        std::error_code error;
        const auto resolved = fs::canonical(path, error);
        if (!error && resolved == path) {
            fs::remove_all(resolved, error);
        }
    }

    const fs::path path;
};

std::string utf8(const fs::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

struct FixtureToolchain {
    fs::path cmake;
    fs::path ninja;
    fs::path compiler;
    fs::path manifest_module;
};

void configure_fixture(IProcessRunner &process, const FixtureToolchain &tools,
                       const fs::path &source_directory, const fs::path &build_directory,
                       const char *generator, std::initializer_list<std::string> options) {
    ProcessRequest request{
        .executable = tools.cmake,
        .arguments = {"-S", utf8(source_directory), "-B", utf8(build_directory), "-G", generator,
                      "-DCMAKE_MAKE_PROGRAM:FILEPATH=" + utf8(tools.ninja),
                      "-DCMAKE_CXX_COMPILER:FILEPATH=" + utf8(tools.compiler),
                      "-DHORIZON_HOTFIX_MANIFEST_MODULE:FILEPATH=" + utf8(tools.manifest_module)}};
    request.arguments.insert(request.arguments.end(), options);
    const auto configured = process.run(request);
    require(configured.succeeded(), "configure the fixture\n" + configured.error_message + '\n' +
                                    configured.stdout_text + configured.stderr_text);
}

void write_source(const fs::path &source, std::string_view contents) {
    std::ofstream stream(source, std::ios::binary);
    stream << contents;
    stream.close();
    require(static_cast<bool>(stream), "write fixture source");
}

void require_module(const BuildResult &result, const fs::path &expected) {
    require(result.succeeded(), "the fixture build succeeds\n" + result.diagnostics);
    require(result.artifacts.module_path.is_absolute(), "the module path is absolute");
    require(fs::is_regular_file(result.artifacts.module_path), "the module exists as a regular file");
    require(fs::equivalent(result.artifacts.module_path, expected),
            "the module uses the manifest's configured output name and location");
}

int module_value(const fs::path &path) {
    const auto module = LoadLibraryW(path.c_str());
    require(module != nullptr, "load the fixture module");
    auto symbol = GetProcAddress(module, "fixture_value");
    if (!symbol) {
        symbol = GetProcAddress(module, "_fixture_value");
    }
    if (!symbol) {
        FreeLibrary(module);
        throw std::runtime_error("locate the fixture_value export");
    }
    const auto value = reinterpret_cast<int (*)()>(symbol)();
    require(FreeLibrary(module) != 0, "release the fixture module before rebuilding");
    return value;
}

void test_reconfigured_tree(IProcessRunner &process, IBuildBackend &backend, const FixtureToolchain &tools,
                            const fs::path &source_directory, const fs::path &build_directory) {
    write_source(source_directory / "fixture.cpp", original_source);
    configure_fixture(process, tools, source_directory, build_directory, "Ninja", {"-DCMAKE_BUILD_TYPE=Debug"});

    const BuildRequest debug{.build_directory = build_directory, .target = "fixture_module",
                             .configuration = "Debug"};
    const auto debug_module = build_directory / "published modules" / "Debug" / "actual module.dll";
    require_module(backend.build(debug), debug_module);

    configure_fixture(process, tools, source_directory, build_directory, "Ninja", {"-DCMAKE_BUILD_TYPE=Release"});
    require(fs::is_regular_file(debug_module), "the old Debug module survives reconfiguration to Release");

    const auto stale_debug = backend.build(debug);
    require(stale_debug.status == BuildStatus::failed && stale_debug.artifacts.module_path.empty() &&
                !stale_debug.diagnostics.empty(),
            "a Debug request cannot publish the old module after reconfiguring to Release");

    auto release = debug;
    release.configuration = "Release";
    require_module(backend.build(release), build_directory / "published modules" / "Release" / "actual module.dll");
}

void test_builds(const FixtureToolchain &tools, const fs::path &fixture_project, const fs::path &test_root) {
    const FixtureDirectory fixture(test_root);
    const auto source_directory = fixture.path / "source tree";
    const auto build_directory = fixture.path / "build tree";
    const auto source = source_directory / "fixture.cpp";
    const auto expected = build_directory / "published modules" / "Debug" / "actual module.dll";
    fs::create_directory(source_directory);
    fs::copy_file(fixture_project, source_directory / "CMakeLists.txt");
    write_source(source, original_source);

    WindowsProcessRunner process;
    configure_fixture(process, tools, source_directory, build_directory, "Ninja Multi-Config",
                      {"-DCMAKE_CONFIGURATION_TYPES:STRING=Release;Debug", "-DCMAKE_DEFAULT_BUILD_TYPE:STRING=Release"});

    CMakeBuildBackend cmake_backend(process, tools.cmake);
    IBuildBackend &backend = cmake_backend;
    const BuildRequest request{.build_directory = build_directory, .target = "fixture_module",
                               .configuration = "Debug"};

    const auto initial = backend.build(request);
    require_module(initial, expected);
    require(module_value(initial.artifacts.module_path) == 17, "the first module has the original export value");

    const auto modified_at = fs::last_write_time(expected);
    require_module(backend.build(request), expected);
    require(fs::last_write_time(expected) == modified_at, "a no-change build preserves the module timestamp");

    const auto manifest = build_directory / "hotfix-manifests" / "fixture_module" / "Debug" / "module.txt";
    auto saved_manifest = manifest;
    saved_manifest.replace_extension(".saved");
    fs::rename(manifest, saved_manifest);
    const auto missing_manifest = backend.build(request);
    fs::rename(saved_manifest, manifest);
    require(missing_manifest.status == BuildStatus::failed && missing_manifest.artifacts.module_path.empty() &&
                !missing_manifest.diagnostics.empty() && fs::is_regular_file(expected),
            "an existing module without its manifest is not published");

    write_source(source, "extern \"C\" __declspec(dllexport) int fixture_value() { return 42; }\n");
    const auto rebuilt = backend.build(request);
    require_module(rebuilt, expected);
    require(module_value(rebuilt.artifacts.module_path) == 42, "editing the source rebuilds the module");

    write_source(source, "#error HORIZON_HOTFIX_FIXTURE_FAILURE\n");
    const auto failed = backend.build(request);
    require(failed.status == BuildStatus::failed && !failed.succeeded(), "a compile failure fails the build");
    require(failed.diagnostics.find("HORIZON_HOTFIX_FIXTURE_FAILURE") != std::string::npos,
            "the failure retains the compiler's diagnostics\n" + failed.diagnostics);
    require(failed.artifacts.module_path.empty() && !failed.artifacts.debug_symbols_path &&
                !failed.artifacts.import_library_path,
            "a failed build publishes no artifacts");
    require(fs::is_regular_file(expected) && module_value(expected) == 42,
            "the older module remains on disk after the compile failure");

    test_reconfigured_tree(process, backend, tools, source_directory, fixture.path / "reconfigured tree");
}

}// namespace

int wmain(int argc, wchar_t **argv) {
    if (argc != 7) {
        std::cerr << "Expected CMake, Ninja, compiler, manifest module, fixture project, and test directory paths\n";
        return 1;
    }
    try {
        test_builds({argv[1], argv[2], argv[3], argv[4]}, argv[5], argv[6]);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
