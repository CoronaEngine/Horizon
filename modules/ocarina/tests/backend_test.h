#pragma once

#include <cstdlib>
#include <cstring>
#include "dsl/diagnostics/env.h"

inline const char *test_backend_name() noexcept {
    const char *path = std::getenv("OCARINA_TEST_CODEGEN");
    ocarina::Env::set_shader_codegen_path(path && std::strcmp(path, "ir") == 0
        ? ocarina::ShaderCodegenPath::EAstToIR : ocarina::ShaderCodegenPath::EAstToSource);
    const char *name = std::getenv("OCARINA_TEST_BACKEND");
    return name && *name ? name : "cuda";
}
