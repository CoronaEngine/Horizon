//
// Created by Z on 22/04/2026.
//

#include "gemm_registry.h"

namespace ocarina {

namespace {
[[nodiscard]] auto &gemm_desc_registry() noexcept {
    static vector<GemmDesc> registry;
    return registry;
}
}// namespace

uint register_gemm_desc(GemmDesc desc) noexcept {
    auto &registry = gemm_desc_registry();
    registry.emplace_back(std::move(desc));
    return static_cast<uint>(registry.size() - 1u);
}

const GemmDesc *find_gemm_desc(uint id) noexcept {
    auto &registry = gemm_desc_registry();
    return id < registry.size() ? &registry[id] : nullptr;
}

}// namespace ocarina