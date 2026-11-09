#include "core/runtime/platform.h"

#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_fixture_value(void *module) {
    if (module == nullptr) {
        return;
    }
    auto *symbol = ocarina::dynamic_module_find_symbol(module, "ocarina_platform_fixture_value");
    expect(symbol != nullptr, "a real exported fixture function is found");
    if (symbol != nullptr) {
        using FixtureFunction = int (*)();
        expect(reinterpret_cast<FixtureFunction>(symbol)() == 52,
               "the exported fixture function can be called");
    }
    expect(ocarina::dynamic_module_find_symbol(module, "ocarina_platform_missing_symbol") == nullptr,
           "a missing symbol returns null");
    // A failed lookup must not poison the next successful lookup, and the name
    // view need not end in a null terminator.
    constexpr std::string_view name = "ocarina_platform_fixture_value.trailing";
    expect(ocarina::dynamic_module_find_symbol(module, name.substr(0, name.find('.'))) != nullptr,
           "symbol lookup accepts a string view after an earlier failure");
}

void test_dynamic_modules() {
    const ocarina::fs::path fixture_path{OC_PLATFORM_FIXTURE_PATH};
    void *module = ocarina::dynamic_module_load(fixture_path);
    expect(module != nullptr, "a module loads by its complete path");
    expect_fixture_value(module);
    ocarina::dynamic_module_destroy(module);

    const auto module_name = ocarina::dynamic_module_name("ocarina-platform-fixture");
    expect(ocarina::fs::path(module_name) == fixture_path.filename(),
           "the generated module filename matches the CMake MODULE artifact");
    module = ocarina::dynamic_module_try_load(fixture_path.parent_path() / module_name);
    expect(module != nullptr, "a module loads by its constructed platform filename");
    expect_fixture_value(module);
    ocarina::dynamic_module_destroy(module);

    expect(ocarina::dynamic_module_try_load(fixture_path.parent_path() /
                                          "ocarina-platform-module-does-not-exist") == nullptr,
           "a missing module returns null from the nonfatal loader");
    expect(ocarina::dynamic_module_find_symbol(nullptr, "ocarina_platform_fixture_value") == nullptr,
           "a null module handle returns null for symbol lookup");
    ocarina::dynamic_module_destroy(nullptr);
}

void test_traceback() {
    const auto trace = ocarina::traceback();
    expect(!trace.empty(), "a traceback contains caller frames");
    for (const auto &frame : trace) {
        expect(frame.address != 0, "traceback frames retain their instruction addresses");
        expect(!frame.module.empty(), "traceback frames identify a module or an unknown placeholder");
        expect(!frame.symbol.empty(), "traceback frames identify a symbol or an unknown placeholder");
    }
    expect(!ocarina::traceback_string().empty(), "a formatted traceback contains caller frames");
    expect(ocarina::traceback(std::numeric_limits<int>::max()).empty(),
           "skipping more frames than exist produces an empty traceback");
    expect(!ocarina::traceback(-1).empty(), "a negative skip count remains safe");
}

void test_backend_module_name() {
    // RHIContext passes this platform-neutral backend basename to the loader.
    const auto module_name = ocarina::dynamic_module_name("ocarina-backend-vulkan-compute");
#if defined(_WIN32)
    constexpr auto expected = "ocarina-backend-vulkan-compute.dll";
#else
    constexpr auto expected = "libocarina-backend-vulkan-compute.so";
#endif
    expect(module_name == expected, "the requested RHI backend filename matches the MODULE artifact");
}

}// namespace

int main() {
    test_dynamic_modules();
    test_traceback();
    test_backend_module_name();
    return failures == 0 ? 0 : 1;
}
