#pragma once

#include "rhi/context.h"
#include <chrono>

namespace ocarina::test {

// Each diagnostic test compiles one kernel. An isolated directory selects the
// exact source produced by this compilation, including its capability profile.
class SlangDiagnosticSource {
    fs::path directory_;
public:
    explicit SlangDiagnosticSource(string_view test_name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto &context = RHIContext::instance();
        const auto runtime = context.runtime_directory();
        context.init(runtime, ocarina::format(".cache-{}-{}", test_name, stamp));
        directory_ = context.cache_directory();
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
