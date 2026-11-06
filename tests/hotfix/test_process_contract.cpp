#include "horizon/hotfix/process_runner.h"

#include <iostream>
#include <type_traits>

namespace {

using namespace horizon::hotfix;

// Compiling a concrete implementation checks that the seam can be implemented
// without Windows, Ocarina or a runtime singleton. This is not an OS runner.
class ExampleRunner final : public IProcessRunner {
public:
    ProcessResult run(const ProcessRequest &) override { return {}; }
};

static_assert(std::is_abstract_v<IProcessRunner>);
static_assert(std::has_virtual_destructor_v<IProcessRunner>);
static_assert(!std::is_abstract_v<ExampleRunner>);

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}// namespace

int main() {
    expect(!ProcessResult{}.succeeded(), "a process that has not run is not successful");

    ProcessResult result;
    result.exit_code = 0;
    expect(result.succeeded(), "normal exit zero succeeds");

    result.stderr_text = "compiler warning: unused variable";
    expect(result.succeeded(), "stderr warnings do not make exit zero fail");

    result.exit_code = 7;
    result.stderr_text.clear();
    result.stdout_text = "build completed";
    expect(!result.succeeded(), "a nonzero exit fails even without error output");

    result.exit_code = -1;
    expect(!result.succeeded(), "a negative nonzero exit is also a failure");

    result.exit_code.reset();
    expect(!result.succeeded(), "output without a normal exit code is not success");

    result.error_message = "executable could not be launched";
    expect(!result.succeeded(), "a launch error is distinct from normal exit");

    result.exit_code = 0;
    expect(!result.succeeded(), "an execution error cannot be masked by exit zero");

    return failures == 0 ? 0 : 1;
}
