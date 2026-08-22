#include "core/runtime/platform.h"

#include <array>
#include <filesystem>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}// namespace

int main(int argc, char **argv) {
    expect(argc == 2, "the test module path is provided");
    if (argc != 2) {
        return 1;
    }

    void *module = horizon::core::dynamic_module_load(std::filesystem::path{argv[1]});
    expect(module != nullptr, "the test module loads through the Core API");

    using ValueFunction = int (*)() noexcept;
    void *symbol = module == nullptr
                       ? nullptr
                       : horizon::core::dynamic_module_find_symbol(module, "horizon_test_value");
    auto value = reinterpret_cast<ValueFunction>(symbol);
    expect(value != nullptr && value() == 42, "the exported symbol resolves and executes");

    horizon::core::dynamic_module_destroy(module);

#if defined(_WIN32)
    expect(horizon::core::dynamic_module_name("sample") == "sample.dll",
           "Windows module suffix is .dll");
#elif defined(__APPLE__)
    expect(horizon::core::dynamic_module_name("sample") == "libsample.dylib",
           "macOS module suffix is .dylib");
#else
    expect(horizon::core::dynamic_module_name("sample") == "libsample.so",
           "Linux module suffix is .so");
#endif

    expect(!horizon::core::traceback().empty(), "traceback returns at least one frame");
    expect(horizon::core::wstring_to_string(L"Horizon") == "Horizon",
           "wide strings convert through the portable Core API");

    const std::array<unsigned char, 4> source{1u, 2u, 3u, 4u};
    std::array<unsigned char, 4> destination{};
    horizon::core::oc_memcpy(destination.data(), source.data(), source.size());
    expect(destination == source, "oc_memcpy copies an exact byte count");
    return failures == 0 ? 0 : 1;
}
