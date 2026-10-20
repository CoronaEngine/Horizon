#pragma once

#include <cstdlib>
#include <cstring>
#include "dsl/diagnostics/env.h"
#include "rhi/context.h"
#include "rhi/device.h"

inline const char *test_backend_name() noexcept {
    const char *path = std::getenv("OCARINA_TEST_CODEGEN");
    ocarina::Env::set_shader_codegen_path(path && std::strcmp(path, "ir") == 0
        ? ocarina::ShaderCodegenPath::EAstToIR : ocarina::ShaderCodegenPath::EAstToSource);
    return "vulkan-compute";
}

inline ocarina::Device create_test_device() {
    test_backend_name();
    return ocarina::RHIContext::instance().create_device();
}
