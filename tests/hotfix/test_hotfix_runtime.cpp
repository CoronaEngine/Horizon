#include "runtime_fixture/fixture_object.h"

#include "hotfix/hotfix.h"
#include "hotfix/module_interface.h"
#include "horizon/hotfix/cmake_build_backend.h"

#if defined(_WIN32)
#include "horizon/hotfix/windows_process_runner.h"
#else
#include "horizon/hotfix/posix_process_runner.h"
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace horizon::hotfix;
using namespace std::chrono_literals;
using horizon::hotfix_test::FixtureImplementation;
using horizon::hotfix_test::FixtureObject;
namespace fs = std::filesystem;

constexpr std::string_view target_name = "horizon-hotfix-runtime-fixture";
constexpr std::string_view compile_error = "HORIZON_HOTFIX_RUNTIME_INTENTIONAL_FAILURE";
constexpr int object_state = 730;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class FixtureSource {
public:
    FixtureSource(fs::path scratch, fs::path original)
        : path(std::move(scratch)), original_(std::move(original)) {
        std::ifstream stream(original_, std::ios::binary);
        require(static_cast<bool>(stream), "open the original fixture source");
        contents_.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
        require(!contents_.empty() && !stream.bad(), "read the original fixture source");
        write({});
    }

    ~FixtureSource() {
        std::error_code error;
        fs::copy_file(original_, path, fs::copy_options::overwrite_existing, error);
    }

    void write(std::string_view prefix) const {
        const auto before = fs::last_write_time(path);
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        do {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << prefix << contents_;
            stream.close();
            require(static_cast<bool>(stream), "write the scratch fixture source");
            if (fs::last_write_time(path) != before) {
                return;
            }
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("fixture source timestamp did not change");
    }

    const fs::path path;

private:
    fs::path original_;
    std::string contents_;
};

class RecordingBackend final : public IBuildBackend {
public:
    RecordingBackend(IProcessRunner &process, const fs::path &cmake) : backend_(process, cmake) {}

    BuildResult build(const BuildRequest &request) override {
        ++build_count;
        last_result = backend_.build(request);
        return last_result;
    }

    // Accessed on the test thread only after HotfixSystem consumes the future.
    int build_count{};
    BuildResult last_result;

private:
    CMakeBuildBackend backend_;
};

struct HotfixLifetime {
    ~HotfixLifetime() { vision::HotfixSystem::destroy_instance(); }
};

std::set<fs::path> staged_files(const fs::path &build_directory) {
    const auto stage = build_directory / "hotfix-staged";
    std::set<fs::path> paths;
    if (fs::is_directory(stage)) {
        for (const auto &entry : fs::recursive_directory_iterator(stage)) {
            if (entry.is_regular_file()) {
                paths.insert(entry.path());
            }
        }
    }
    return paths;
}

void run_build(vision::HotfixSystem &hotfix, const RecordingBackend &backend) {
    const int before = backend.build_count;
    require(hotfix.check_and_build(), "a watched edit schedules a build");
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (hotfix.is_working() && std::chrono::steady_clock::now() < deadline) {
        hotfix.execute_callback();
        if (hotfix.is_working()) {
            std::this_thread::sleep_for(5ms);
        }
    }
    require(!hotfix.is_working(), "HotfixSystem finishes the build and runs its callback");
    require(backend.build_count == before + 1, "the edit invokes the real CMake build backend once");
}

using Slot = vision::HotfixSlot<vision::SP<FixtureObject>>;

void require_live(const Slot &slot, int generation, int restores) {
    require(slot->generation() == generation,
            "the live object uses implementation " + std::to_string(generation));
    require(slot->state == object_state && slot->restores == restores,
            "the new object restores the previous object's state");
}

void update(const FixtureSource &source, const fs::path &build_directory,
            vision::HotfixSystem &hotfix, const RecordingBackend &backend, Slot &slot, int generation) {
    const auto before = slot.impl();
    const auto staged_before = staged_files(build_directory);
    source.write("#define HORIZON_HOTFIX_TEST_GENERATION " + std::to_string(generation) + "\n");
    run_build(hotfix, backend);
    const auto &built = backend.last_result;
    require(built.succeeded(), "CMake builds the edited module\n" + built.diagnostics);
    require(slot.get() != before.get(), "normal update replaces the live object");
    require_live(slot, generation, before->restores + 1);

    const auto staged_after = staged_files(build_directory);
    require(staged_after.size() == staged_before.size() + 1, "an update stages exactly one new module");
    for (const auto &path : staged_after) {
        if (!staged_before.contains(path)) {
            require(path.filename() == built.artifacts.module_path.filename() &&
                        !fs::equivalent(path, built.artifacts.module_path),
                    "the staged module is a separate copy of the build artifact");
        }
    }
}

void test_runtime(const fs::path &cmake, const fs::path &build_directory, std::string configuration,
                  const fs::path &scratch, const fs::path &original) {
    const FixtureSource source(scratch, original);
#if defined(_WIN32)
    WindowsProcessRunner process;
#else
    PosixProcessRunner process;
#endif
    const auto backend = std::make_shared<RecordingBackend>(process, cmake);
    const HotfixLifetime lifetime;
    auto &hotfix = vision::HotfixSystem::instance();
    require(hotfix.set_build_backend(backend, build_directory, std::move(configuration)),
            "configure HotfixSystem to use the CMake build tree");
    hotfix.register_target(source.path, target_name);
    Slot slot{vision::ModuleInterface::instance().construct_shared<FixtureObject>(
        vision::type_string<FixtureImplementation>())};
    slot->state = object_state;
    require_live(slot, 1, 0);
    require(!hotfix.check_and_build(), "an unchanged watched file does not start a build");

    update(source, build_directory, hotfix, *backend, slot, 2);
    update(source, build_directory, hotfix, *backend, slot, 3);
    const auto original_artifact = backend->last_result.artifacts.module_path;
    const auto artifact_time = fs::last_write_time(original_artifact);
    require(hotfix.has_previous() && !hotfix.has_next(), "the latest version has rollback history");

    const auto latest = slot.impl();
    hotfix.previous_version();
    require(slot.get() != latest.get(), "rollback replaces the live object");
    require_live(slot, 2, 3);
    require(hotfix.has_previous() && hotfix.has_next(), "rollback preserves both adjacent versions");

    const auto current = slot.impl();
    const auto staged_before = staged_files(build_directory);
    source.write("#error " + std::string(compile_error) + "\n");
    run_build(hotfix, *backend);
    const auto &failed = backend->last_result;
    require(failed.status == BuildStatus::failed && failed.artifacts.module_path.empty() &&
                failed.diagnostics.find(compile_error) != std::string::npos,
            "the compiler reports the intentional source error and publishes no artifact\n" + failed.diagnostics);
    require(slot.get() == current.get(), "compile failure keeps the exact live object");
    require_live(slot, 2, 3);
    require(staged_files(build_directory) == staged_before && fs::last_write_time(original_artifact) == artifact_time,
            "compile failure leaves staged versions and the last build artifact untouched");
    require(hotfix.has_previous() && hotfix.has_next(), "compile failure leaves version history intact");

    hotfix.next_version();
    require_live(slot, 3, 4);
    require(!hotfix.has_next(), "the existing latest version remains selectable after the failure");
}

}// namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
    if (argc != 6) {
        std::cerr << "Expected CMake, build directory, configuration, scratch source and original source paths\n";
        return 1;
    }
    try {
        test_runtime(argv[1], argv[2], fs::path(argv[3]).string(), argv[4], argv[5]);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
