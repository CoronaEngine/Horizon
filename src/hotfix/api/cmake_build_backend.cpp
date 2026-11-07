#include "horizon/hotfix/cmake_build_backend.h"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace horizon::hotfix {
namespace {

std::string utf8_path(const std::filesystem::path &path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

bool valid_component(std::string_view value) {
    constexpr std::string_view characters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_+.-";
    return !value.empty() && value.front() != '-' && value.front() != '.' && value.back() != '.' &&
           std::ranges::all_of(value, [&](char character) { return characters.find(character) != characters.npos; });
}

void append_diagnostic(std::string &diagnostics, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!diagnostics.empty() && diagnostics.back() != '\n') {
        diagnostics.push_back('\n');
    }
    diagnostics.append(text);
}

bool registered_configuration(const std::filesystem::path &configurations, std::string_view requested) {
    std::ifstream stream{configurations};
    for (std::string name; std::getline(stream, name);) {
        if (name == requested) {
            return true;
        }
    }
    return false;
}

std::filesystem::path read_module_path(const std::filesystem::path &manifest) {
    std::ifstream stream{manifest, std::ios::binary};
    std::string path;
    if (!std::getline(stream, path) || stream.peek() != std::char_traits<char>::eof() || stream.bad()) {
        return {};
    }
    if (path.ends_with('\r')) {
        path.pop_back();
    }
    if (path.empty() || path.find('\0') != path.npos) {
        return {};
    }
    return std::filesystem::path{std::u8string(path.begin(), path.end())};
}

}// namespace

CMakeBuildBackend::CMakeBuildBackend(IProcessRunner &runner, std::filesystem::path cmake_executable,
                                     std::map<std::string, std::string> environment_overrides)
    : runner_(runner),
      cmake_executable_(std::move(cmake_executable)),
      environment_overrides_(std::move(environment_overrides)) {}

BuildResult CMakeBuildBackend::build(const BuildRequest &request) {
    BuildResult result;
    result.status = BuildStatus::failed;

    const auto &directory = request.build_directory.native();
    if (directory.empty() || directory.find(std::filesystem::path::value_type{}) != directory.npos ||
        !valid_component(request.target) ||
        (!request.configuration.empty() && !valid_component(request.configuration))) {
        result.diagnostics = "CMake hotfix build requires a build directory and valid target/configuration names";
        return result;
    }

    try {
        const auto build_directory = std::filesystem::absolute(request.build_directory);
        const auto manifests = build_directory / "hotfix-manifests" / request.target;
        const auto configurations = manifests / "configurations.txt";
        const auto manifest = manifests / request.configuration / "module.txt";
        if (!registered_configuration(configurations, request.configuration)) {
            result.diagnostics = "CMake hotfix configuration '" + request.configuration +
                                 "' is not registered for '" + request.target + "'";
            return result;
        }
        if (!std::filesystem::is_regular_file(manifest)) {
            result.diagnostics = "CMake hotfix module manifest is missing: " + utf8_path(manifest);
            return result;
        }

        ProcessRequest process;
        process.executable = cmake_executable_;
        process.arguments = {"--build", utf8_path(build_directory), "--target", request.target};
        if (!request.configuration.empty()) {
            process.arguments.insert(process.arguments.end(), {"--config", request.configuration});
        }
        process.working_directory = build_directory;
        process.environment_overrides = environment_overrides_;

        auto built = runner_.run(process);
        result.diagnostics = std::move(built.stdout_text);
        append_diagnostic(result.diagnostics, built.stderr_text);
        if (!built.succeeded()) {
            append_diagnostic(result.diagnostics, built.error_message);
            append_diagnostic(result.diagnostics, built.exit_code
                ? "CMake build failed (exit code " + std::to_string(*built.exit_code) + ")"
                : "CMake build failed to complete");
            return result;
        }

        // Regeneration during the build may have changed the module location.
        if (!registered_configuration(configurations, request.configuration)) {
            append_diagnostic(result.diagnostics, "CMake hotfix configuration changed during the build");
            return result;
        }
        const auto module = read_module_path(manifest);
        if (!module.is_absolute() || !std::filesystem::is_regular_file(module) ||
            std::filesystem::file_size(module) == 0) {
            append_diagnostic(result.diagnostics, "CMake hotfix manifest does not identify a module file: " +
                              utf8_path(manifest));
            return result;
        }
        result.artifacts.module_path = module;
        result.status = BuildStatus::succeeded;
    } catch (const std::system_error &error) {
        append_diagnostic(result.diagnostics, error.what());
    }
    return result;
}

}// namespace horizon::hotfix
