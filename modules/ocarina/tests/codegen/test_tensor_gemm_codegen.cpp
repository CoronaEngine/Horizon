#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

#include "dsl/dsl.h"
#include "dsl/tensor/gemm.h"
#include "rhi/context.h"

/**
 * GEMM registry is a temporary host-side bridge, not a CUDA backend dependency.
 * Keep it local to this test TU so the target still compiles exactly one source file.
 */
#include "rhi/tensor/gemm_registry.cpp"

using namespace ocarina;

namespace {

[[nodiscard]] bool check_impl(bool condition, const char *expr) {
    if (!condition) {
        std::cerr << "FAILED: " << expr << std::endl;
    }
    return condition;
}

#define CHECK(...)                            \
    do {                                      \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                     \
        }                                     \
    } while (false)

[[nodiscard]] bool contains(std::string_view source, std::string_view needle) {
    return source.find(needle) != std::string_view::npos;
}

struct GemmKernelBuildResult {
    Kernel<void()> kernel;
    uint id{std::numeric_limits<uint>::max()};
};

struct MultiGemmKernelBuildResult {
    Kernel<void()> kernel;
    uint first_id{std::numeric_limits<uint>::max()};
    uint second_id{std::numeric_limits<uint>::max()};
};

[[nodiscard]] std::optional<fs::path> find_cached_cuda_source(const RHIContext &context,
                                                              std::string_view shader_desc) {
    if (!fs::exists(context.cache_directory())) {
        return std::nullopt;
    }
    for (const auto &entry: fs::directory_iterator(context.cache_directory())) {
        if (!entry.is_regular_file()) {
            continue;
        }
        fs::path path = entry.path();
        if (path.extension() == ".cu" && contains(path.filename().string(), shader_desc)) {
            return path;
        }
    }
    return std::nullopt;
}

template<typename T>
[[nodiscard]] std::string compile_and_read_cuda_source(Device &device,
                                                       RHIContext &context,
                                                       const Kernel<T> &kernel,
                                                       std::string_view shader_desc) {
    context.clear_cache();
    RHIContext::create_directory_if_necessary(context.cache_directory());
    auto shader = device.compile(kernel, string(shader_desc));
    (void)shader;
    auto path = find_cached_cuda_source(context, shader_desc);
    if (!path.has_value()) {
        std::cerr << "FAILED: generated cuda source not found for " << shader_desc << std::endl;
        return {};
    }
    return RHIContext::read_file(*path);
}

[[nodiscard]] GemmDesc make_test_gemm_desc(handle_ty a_handle,
                                           handle_ty b_handle,
                                           handle_ty c_handle,
                                           uint m,
                                           uint n,
                                           uint k,
                                           bool trans_a,
                                           bool trans_b) {
    GemmDesc desc{};
    desc.a = TensorView{a_handle,
                        0u,
                        Type::of<float>(),
                        {trans_a ? k : m, trans_a ? m : k},
                        {trans_a ? m : k, 1u},
                        TensorLayout::row_major};
    desc.b = TensorView{b_handle,
                        0u,
                        Type::of<float>(),
                        {trans_b ? n : k, trans_b ? k : n},
                        {trans_b ? k : n, 1u},
                        TensorLayout::row_major};
    desc.c = TensorView{c_handle,
                        0u,
                        Type::of<float>(),
                        {m, n},
                        {n, 1u},
                        TensorLayout::row_major};
    desc.m = m;
    desc.n = n;
    desc.k = k;
    desc.trans_a = trans_a;
    desc.trans_b = trans_b;
    desc.alpha = 0.5f;
    desc.beta = 2.0f;
    desc.compute_type = GemmComputeType::fp32;
    desc.allow_tensor_core = false;
    return desc;
}

[[nodiscard]] GemmKernelBuildResult make_single_gemm_kernel() {
    GemmKernelBuildResult result{};
    GemmDesc desc = make_test_gemm_desc(11u, 22u, 33u, 2u, 4u, 3u, false, false);
    result.kernel = [&] {
        GemmOp op = gemm(desc);
        result.id = op.id;
    };
    return result;
}

[[nodiscard]] MultiGemmKernelBuildResult make_multi_gemm_kernel() {
    MultiGemmKernelBuildResult result{};
    GemmDesc first_desc = make_test_gemm_desc(101u, 202u, 303u, 2u, 4u, 3u, false, false);
    GemmDesc second_desc = make_test_gemm_desc(404u, 505u, 606u, 3u, 2u, 4u, true, true);
    result.kernel = [&] {
        GemmOp first = gemm(first_desc);
        GemmOp second = gemm(second_desc);
        result.first_id = first.id;
        result.second_id = second.id;
    };
    return result;
}

[[nodiscard]] bool test_gemm_codegen_emits_placeholder_call(Device &device, RHIContext &context) {
    auto result = make_single_gemm_kernel();
    CHECK(result.id != std::numeric_limits<uint>::max());

    std::string cuda_source = compile_and_read_cuda_source(device,
                                                           context,
                                                           result.kernel,
                                                           "test_tensor_gemm_codegen_single");
    std::string expected = format("oc_tensor_gemm<{}u>()", result.id);

    CHECK(contains(cuda_source, "extern \"C\" __global__"));
    CHECK(contains(cuda_source, "test_tensor_gemm_codegen_single"));
    if (!contains(cuda_source, expected)) {
        std::cerr << "FAILED: missing GEMM placeholder call for id " << result.id << std::endl;
        std::cerr << "expected substring: " << expected << std::endl;
        std::cerr << cuda_source << std::endl;
        return false;
    }
    return true;
}

[[nodiscard]] bool test_gemm_registry_allocates_distinct_ids() {
    auto first = make_single_gemm_kernel();
    auto second = make_single_gemm_kernel();
    CHECK(first.id != std::numeric_limits<uint>::max());
    CHECK(second.id != std::numeric_limits<uint>::max());
    CHECK(first.id != second.id);
    return true;
}

[[nodiscard]] bool test_multiple_gemm_codegen_emits_all_placeholder_calls(Device &device, RHIContext &context) {
    auto result = make_multi_gemm_kernel();
    CHECK(result.first_id != std::numeric_limits<uint>::max());
    CHECK(result.second_id != std::numeric_limits<uint>::max());
    CHECK(result.first_id != result.second_id);

    std::string cuda_source = compile_and_read_cuda_source(device,
                                                           context,
                                                           result.kernel,
                                                           "test_tensor_gemm_codegen_multi");
    CHECK(contains(cuda_source, "test_tensor_gemm_codegen_multi"));
    CHECK(contains(cuda_source, format("oc_tensor_gemm<{}u>()", result.first_id)));
    CHECK(contains(cuda_source, format("oc_tensor_gemm<{}u>()", result.second_id)));
    return true;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");

    bool passed = true;
    passed = test_gemm_codegen_emits_placeholder_call(device, context) && passed;
    passed = test_gemm_registry_allocates_distinct_ids() && passed;
    passed = test_multiple_gemm_codegen_emits_all_placeholder_calls(device, context) && passed;
    if (!passed) {
        return 1;
    }
    std::cout << "tensor gemm codegen test passed" << std::endl;
    return 0;
}