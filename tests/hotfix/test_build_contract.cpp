#include "horizon/hotfix/build_backend.h"

#include <array>
#include <iostream>
#include <type_traits>

namespace {

using namespace horizon::hotfix;

class ExampleBackend final : public IBuildBackend {
public:
    BuildResult build(const BuildRequest &) override { return {}; }
};

static_assert(std::is_abstract_v<IBuildBackend>);
static_assert(std::has_virtual_destructor_v<IBuildBackend>);
static_assert(!std::is_abstract_v<ExampleBackend>);

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}// namespace

int main() {
    expect(!BuildResult{}.succeeded(), "a build that has not run is not successful");

    BuildResult result;
    result.status = BuildStatus::succeeded;
    expect(!result.succeeded(), "successful status without a module path fails closed");

    // The contract accepts backend-supplied paths without guessing platform
    // suffixes or requiring that these synthetic paths exist on the test host.
    const auto output_directory = std::filesystem::current_path() / "hotfix output";
    const std::array<std::filesystem::path, 4> paths{
        output_directory / "module.dll", output_directory / "libmodule.so",
        output_directory / "libmodule.dylib", output_directory / u8"模块.plugin"};
    for (const auto &path : paths) {
        result.artifacts.module_path = path;
        expect(result.succeeded(), "success accepts the backend's actual artifact naming");
    }

    result.diagnostics = "build succeeded with warnings";
    expect(result.succeeded(), "diagnostic text is not a failure status");

    result.status = BuildStatus::failed;
    expect(!result.succeeded(), "an old module path cannot mask a failed build");

    result.status = BuildStatus::not_started;
    expect(!result.succeeded(), "an old module path cannot mask an unstarted build");

    result.status = BuildStatus::succeeded;
    result.artifacts.module_path.clear();
    result.artifacts.debug_symbols_path = "module.pdb";
    result.artifacts.import_library_path = "module.lib";
    expect(!result.succeeded(), "sidecar artifacts do not replace the loadable module");

    return failures == 0 ? 0 : 1;
}
