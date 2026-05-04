#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    return std::abs(lhs - rhs) <= eps;
}

void log_mismatch(const char *label, uint row, uint col, float actual, float expected) {
    std::cerr << "  FAIL [" << label << "] at (" << row << ", " << col << ")"
              << " expected=" << expected
              << " actual=" << actual
              << std::endl;
}

[[nodiscard]] vector<float> host_gemm(const vector<float> &a,
                                      const vector<float> &b,
                                      const vector<float> &c,
                                      uint m,
                                      uint n,
                                      uint k,
                                      bool trans_a,
                                      bool trans_b,
                                      float alpha,
                                      float beta) {
    vector<float> out(m * n, 0.f);
    for (uint row = 0; row < m; ++row) {
        for (uint col = 0; col < n; ++col) {
            float accum = 0.f;
            for (uint inner = 0; inner < k; ++inner) {
                uint a_index = trans_a ? inner * m + row : row * k + inner;
                uint b_index = trans_b ? col * k + inner : inner * n + col;
                accum += a[a_index] * b[b_index];
            }
            out[row * n + col] = alpha * accum + beta * c[row * n + col];
        }
    }
    return out;
}

int verify_matrix(const char *label,
                  const vector<float> &actual,
                  const vector<float> &expected,
                  uint m,
                  uint n,
                  float eps = 1e-5f) {
    int failures = 0;
    for (uint row = 0; row < m; ++row) {
        for (uint col = 0; col < n; ++col) {
            uint index = row * n + col;
            if (!close_float(actual[index], expected[index], eps)) {
                log_mismatch(label, row, col, actual[index], expected[index]);
                ++failures;
            }
        }
    }
    return failures;
}

struct RuntimeGemmCase {
    const char *name;
    const char *shader_name;
    const char *verify_label;
    vector<float> host_a;
    vector<float> host_b;
    vector<float> host_c;
    uint m;
    uint n;
    uint k;
    bool trans_a{false};
    bool trans_b{false};
    float alpha{1.f};
    float beta{0.f};
};

int run_gemm_case(Device &device, Stream &stream, const RuntimeGemmCase &test_case) {
    std::cout << "=== " << test_case.name << " ===" << std::endl;
    vector<float> host_out(test_case.host_c.size(), -1.f);

    auto a = device.create_buffer<float>(test_case.host_a.size(), format("{}_a", test_case.shader_name));
    auto b = device.create_buffer<float>(test_case.host_b.size(), format("{}_b", test_case.shader_name));
    auto c = device.create_buffer<float>(test_case.host_c.size(), format("{}_c", test_case.shader_name));

    a.upload_immediately(test_case.host_a.data());
    b.upload_immediately(test_case.host_b.data());
    c.upload_immediately(test_case.host_c.data());

    GemmDesc desc{};
    desc.a = make_tensor_view(a,
                              {test_case.trans_a ? test_case.k : test_case.m,
                               test_case.trans_a ? test_case.m : test_case.k});
    desc.b = make_tensor_view(b,
                              {test_case.trans_b ? test_case.n : test_case.k,
                               test_case.trans_b ? test_case.k : test_case.n});
    desc.c = make_tensor_view(c, {test_case.m, test_case.n});
    desc.m = test_case.m;
    desc.n = test_case.n;
    desc.k = test_case.k;
    desc.trans_a = test_case.trans_a;
    desc.trans_b = test_case.trans_b;
    desc.alpha = test_case.alpha;
    desc.beta = test_case.beta;

    stream << gemm_commands(device, desc, test_case.shader_name)
           << c.download(host_out.data())
           << synchronize()
           << commit();

    auto expected = host_gemm(test_case.host_a,
                              test_case.host_b,
                              test_case.host_c,
                              test_case.m,
                              test_case.n,
                              test_case.k,
                              test_case.trans_a,
                              test_case.trans_b,
                              test_case.alpha,
                              test_case.beta);
    int failures = verify_matrix(test_case.verify_label, host_out, expected, test_case.m, test_case.n);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_basic_gemm(Device &device, Stream &stream) {
    return run_gemm_case(device,
                         stream,
                         RuntimeGemmCase{
                             .name = "test_basic_gemm",
                             .shader_name = "test_tensor_gemm_basic",
                             .verify_label = "basic_gemm",
                             .host_a = {1.f, 2.f, 3.f,
                                        4.f, 5.f, 6.f},
                             .host_b = {1.f, 2.f, 3.f, 4.f,
                                        5.f, 6.f, 7.f, 8.f,
                                        9.f, 10.f, 11.f, 12.f},
                             .host_c = vector<float>(2u * 4u, 0.f),
                             .m = 2u,
                             .n = 4u,
                             .k = 3u});
}

int test_transposed_gemm(Device &device, Stream &stream) {
    return run_gemm_case(device,
                         stream,
                         RuntimeGemmCase{
                             .name = "test_transposed_gemm",
                             .shader_name = "test_tensor_gemm_transposed",
                             .verify_label = "transposed_gemm",
                             .host_a = {1.f, 4.f,
                                        2.f, 5.f,
                                        3.f, 6.f},
                             .host_b = {1.f, 3.f, 5.f,
                                        2.f, 4.f, 6.f},
                             .host_c = {0.5f, 1.0f,
                                        1.5f, 2.0f},
                             .m = 2u,
                             .n = 2u,
                             .k = 3u,
                             .trans_a = true,
                             .trans_b = true,
                             .alpha = 0.5f,
                             .beta = 2.0f});
}

int test_transpose_a_only_gemm(Device &device, Stream &stream) {
    return run_gemm_case(device,
                         stream,
                         RuntimeGemmCase{
                             .name = "test_transpose_a_only_gemm",
                             .shader_name = "test_tensor_gemm_transpose_a_only",
                             .verify_label = "transpose_a_only_gemm",
                             .host_a = {1.f, 5.f,
                                        2.f, 6.f,
                                        3.f, 7.f,
                                        4.f, 8.f},
                             .host_b = {1.f, 2.f,
                                        3.f, 4.f,
                                        5.f, 6.f,
                                        7.f, 8.f},
                             .host_c = vector<float>(2u * 2u, 0.f),
                             .m = 2u,
                             .n = 2u,
                             .k = 4u,
                             .trans_a = true});
}

int test_transpose_b_only_gemm(Device &device, Stream &stream) {
    return run_gemm_case(device,
                         stream,
                         RuntimeGemmCase{
                             .name = "test_transpose_b_only_gemm",
                             .shader_name = "test_tensor_gemm_transpose_b_only",
                             .verify_label = "transpose_b_only_gemm",
                             .host_a = {1.f, 2.f, 3.f,
                                        4.f, 5.f, 6.f},
                             .host_b = {1.f, 4.f, 7.f,
                                        2.f, 5.f, 8.f},
                             .host_c = vector<float>(2u * 2u, 0.f),
                             .m = 2u,
                             .n = 2u,
                             .k = 3u,
                             .trans_b = true});
}

int test_beta_accumulation_gemm(Device &device, Stream &stream) {
    return run_gemm_case(device,
                         stream,
                         RuntimeGemmCase{
                             .name = "test_beta_accumulation_gemm",
                             .shader_name = "test_tensor_gemm_beta_accumulation",
                             .verify_label = "beta_accumulation_gemm",
                             .host_a = {2.f, 1.f,
                                        0.f, 3.f},
                             .host_b = {1.f, 4.f,
                                        2.f, 5.f},
                             .host_c = {10.f, 20.f,
                                        30.f, 40.f},
                             .m = 2u,
                             .n = 2u,
                             .k = 2u,
                             .alpha = 1.5f,
                             .beta = 0.25f});
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    device.init_rtx();
    Stream stream = device.create_stream();

    int failures = 0;
    failures += test_basic_gemm(device, stream);
    failures += test_transposed_gemm(device, stream);
    failures += test_transpose_a_only_gemm(device, stream);
    failures += test_transpose_b_only_gemm(device, stream);
    failures += test_beta_accumulation_gemm(device, stream);

    stream << synchronize() << commit();
    if (failures != 0) {
        std::cerr << "test_tensor_gemm failed with " << failures << " mismatches." << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "test_tensor_gemm PASSED" << std::endl;
    return EXIT_SUCCESS;
}