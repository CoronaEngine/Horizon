#pragma once

#include "dsl/diagnostics/env.h"
#include "rhi/context.h"
#include "rhi/device.h"

inline const char *test_backend_name() noexcept {
    return "vulkan-compute";
}

inline ocarina::Device create_test_device() {
    return ocarina::RHIContext::instance().create_device();
}
