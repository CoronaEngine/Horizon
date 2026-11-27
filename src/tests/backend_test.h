#pragma once

#include "dsl/diagnostics/env.h"
#include "rhi/context.h"
#include "rhi/device.h"
#include <iostream>

namespace ocarina::test {
struct RHIContextTestAccess {
    static fs::path set_cache_directory(const fs::path &directory) {
        auto &context = RHIContext::instance();
        context.set_cache_directory(directory);
        return context.cache_directory();
    }
};
}// namespace ocarina::test

inline const char *test_backend_name() noexcept {
    return "vulkan-compute";
}

inline ocarina::Device create_test_device() {
    return ocarina::RHIContext::instance().create_device();
}

inline bool skip_without_feature(bool supported, const char *feature) {
    if (supported) return false;
    std::cout << "SKIPPED (" << test_backend_name() << "): device does not support "
              << feature << std::endl;
    return true;
}
