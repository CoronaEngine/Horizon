#include <cmath>
#include <iostream>

#include "core/type_system/precision_policy.h"
#include "dsl/dsl.h"
#include "math/real.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

struct ShaderParamLeaf {
    real roughness;
    Vector<real, 2> uv;
};

OC_STRUCT(, ShaderParamLeaf, roughness, uv) {
};

struct ShaderParamRecord {
    ShaderParamLeaf leaf;
    ocarina::array<real, 2> weights;
    float extra;
};

OC_STRUCT(, ShaderParamRecord, leaf, weights, extra) {
};

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

[[nodiscard]] StoragePrecisionPolicy make_policy(PrecisionPolicy policy) {
    return StoragePrecisionPolicy{.policy = policy, .allow_real_in_storage = false};
}

[[nodiscard]] float precision_epsilon(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? 2e-2f : 1e-5f;
}

[[nodiscard]] const char *precision_suffix(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? "f16" : "f32";
}

[[nodiscard]] ShaderParamRecord make_param_record(float base) {
    return ShaderParamRecord{
        .leaf = ShaderParamLeaf{
            .roughness = real{base + 0.1875f},
            .uv = Vector<real, 2>{real{base + 1.3125f}, real{base + 2.4375f}}},
        .weights = {real{base + 3.5625f}, real{base + 4.6875f}},
        .extra = base + 5.8125f};
}

[[nodiscard]] bool close_float(float lhs, float rhs, float eps) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

[[nodiscard]] float4 expected_output(real bias, const ShaderParamRecord &param) {
    return make_float4(static_cast<float>(bias) + static_cast<float>(param.leaf.roughness),
                       static_cast<float>(param.leaf.uv.x) + static_cast<float>(param.weights[0]),
                       static_cast<float>(param.leaf.uv.y) + static_cast<float>(param.weights[1]),
                       param.extra);
}

template<bool Raytracing, PrecisionPolicy precision>
[[nodiscard]] bool test_shader_parameter_policy_impl(Device &device) {
    StoragePrecisionPolicy policy = make_policy(precision);
    string mode = Raytracing ? "raygen" : "compute";
    string suffix = precision_suffix(precision);
    Buffer<float4> output = device.create_buffer<float4>(1u,
                                                         "test_shader_parameter_policy_output_" + mode + "_" + suffix);
    vector<float4> host_output(1u, make_float4(0.f));
    real bias{3.28125f};
    ShaderParamRecord param = make_param_record(7.125f);

    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(policy);

    Callable callable = [](Var<real> bias_value, Var<ShaderParamRecord> param_value) {
        return make_float4(cast<float>(bias_value + param_value.leaf.roughness),
                           cast<float>(param_value.leaf.uv.x + param_value.weights[0]),
                           cast<float>(param_value.leaf.uv.y + param_value.weights[1]),
                           param_value.extra);
    };

    Kernel kernel = [&](Var<real> bias_value, Var<ShaderParamRecord> param_value, BufferVar<float4> output_buffer) {
        Uint index = dispatch_id();
        $if(index == 0u) {
            Float4 value = callable(bias_value, param_value);
            output_buffer.write(0u, value);
        };
    };
    kernel.function()->set_raytracing(Raytracing);
    auto shader = device.compile(kernel, "test_shader_parameter_policy_" + mode + "_" + suffix);

    set_global_storage_policy(previous_policy);

    Stream stream = device.create_stream();
    stream << shader(bias, param, output).dispatch(1u)
           << output.download(host_output.data())
           << synchronize()
           << commit();

    CHECK(equal_float4(host_output[0], expected_output(bias, param), precision_epsilon(precision)));
    return true;
}

template<bool Raytracing>
[[nodiscard]] bool test_shader_parameter_policy(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_shader_parameter_policy_impl<Raytracing, PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_shader_parameter_policy_impl<Raytracing, PrecisionPolicy::force_f32>(device);
    }
    return false;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    device.init_rtx();

    bool passed = true;
    passed = test_shader_parameter_policy<false>(device, PrecisionPolicy::force_f16) && passed;
    passed = test_shader_parameter_policy<false>(device, PrecisionPolicy::force_f32) && passed;
    passed = test_shader_parameter_policy<true>(device, PrecisionPolicy::force_f16) && passed;
    passed = test_shader_parameter_policy<true>(device, PrecisionPolicy::force_f32) && passed;
    if (!passed) {
        return 1;
    }
    std::cout << "shader parameter policy test passed" << std::endl;
    return 0;
}