#pragma once

#include "tests/backend_test.h"
#include <chrono>

namespace ocarina::test {

// Each diagnostic test compiles one kernel. An isolated directory selects the
// exact source produced by this compilation, including its capability profile.
class SlangDiagnosticSource {
    fs::path directory_;
public:
    explicit SlangDiagnosticSource(string_view test_name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = RHIContextTestAccess::set_cache_directory(ocarina::format(".cache-{}-{}", test_name, stamp));
    }
    ~SlangDiagnosticSource() { fs::remove_all(directory_); }

    [[nodiscard]] string compute_main() const {
        string result;
        for (const auto &file : fs::directory_iterator(directory_ / "slang_cache")) {
            const auto name = file.path().filename().string();
            if (name.find("_compute_") != string::npos && name.ends_with("_compute_main.slang")) {
                if (!result.empty()) return {};
                result = RHIContext::read_file(file.path());
            }
        }
        return result;
    }
};

}// namespace ocarina::test
