#include <iostream>
#include <optional>
#include <string_view>

#include "core/type_system/precision_policy.h"
#include "dsl/dsl.h"
#include "generator/ast_to_cpp_source.h"
#include "math/real.h"
#include "rhi/context.h"

using namespace ocarina;

struct CodegenPolicyRecord {
    real weight;
    Vector<real, 2> uv;
};

OC_STRUCT(, CodegenPolicyRecord, weight, uv) {
};

struct CodegenNestedInner {
    real weight;
    Vector<real, 2> uv;
};

OC_STRUCT(, CodegenNestedInner, weight, uv) {
};

struct CodegenNestedRecord {
    CodegenNestedInner inner;
    real gain;
};

OC_STRUCT(, CodegenNestedRecord, inner, gain) {
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

[[nodiscard]] bool contains(std::string_view source, std::string_view needle) {
    return source.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string trim_trailing_space(std::string_view source) {
    size_t end = source.size();
    while (end > 0u) {
        char ch = source[end - 1u];
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            break;
        }
        end -= 1u;
    }
    return std::string(source.substr(0u, end));
}

[[nodiscard]] bool expect_same_source(std::string_view label, std::string_view lhs, std::string_view rhs) {
    std::string lhs_trimmed = trim_trailing_space(lhs);
    std::string rhs_trimmed = trim_trailing_space(rhs);
    if (lhs_trimmed == rhs_trimmed) {
        return true;
    }
    std::cerr << "FAILED: source mismatch for " << label << std::endl;
    std::cerr << "---- ast ----" << std::endl;
    std::cerr << lhs_trimmed << std::endl;
    std::cerr << "---- ir ----" << std::endl;
    std::cerr << rhs_trimmed << std::endl;
    return false;
}

[[nodiscard]] const Type *resolved_type(const Type *type, PrecisionPolicy precision) {
    return Type::resolve(type, make_policy(precision));
}

[[nodiscard]] std::string emit_cpp_source(const Function &function) {
    AstToCppSource emitter(false);
    emitter.emit(function);
    return std::string(emitter.scratch().view());
}

[[nodiscard]] const char *precision_suffix(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? "f16" : "f32";
}

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
                                                       std::string_view shader_desc,
                                                       ShaderCodegenPath path) {
    ShaderCodegenPath previous_path = Env::shader_codegen_path();
    Env::set_shader_codegen_path(path);
    context.clear_cache();
    RHIContext::create_directory_if_necessary(context.cache_directory());
    auto shader = device.compile(kernel, string(shader_desc));
    (void)shader;
    Env::set_shader_codegen_path(previous_path);
    auto cached_source = find_cached_cuda_source(context, shader_desc);
    if (!cached_source.has_value()) {
        std::cerr << "FAILED: generated cuda source not found for " << shader_desc << std::endl;
        return {};
    }
    return RHIContext::read_file(*cached_source);
}

template<PrecisionPolicy precision>
[[nodiscard]] shared_ptr<Function> make_callable_function() {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));
    Callable callable = [](Var<CodegenPolicyRecord> record, Var<real> bias) {
        return record.weight + bias + record.uv.x;
    };
    callable.function()->set_description(precision == PrecisionPolicy::force_f16 ? "callable_f16" : "callable_f32");
    auto function = callable.function();
    set_global_storage_policy(previous_policy);
    return function;
}

template<PrecisionPolicy precision>
[[nodiscard]] shared_ptr<Function> make_kernel_function() {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));
    Kernel kernel = [](Var<real> bias, Var<CodegenPolicyRecord> record) {
        Var<real> sum = record.weight + bias + record.uv.y;
        comment("resolved type kernel body");
        sum = sum + cast<real>(1.0f);
    };
    kernel.function()->set_description(precision == PrecisionPolicy::force_f16 ? "kernel_f16" : "kernel_f32");
    auto function = kernel.function();
    set_global_storage_policy(previous_policy);
    return function;
}

template<PrecisionPolicy precision>
[[nodiscard]] std::string compile_callable_kernel_cuda_source(Device &device,
                                                              RHIContext &context,
                                                              ShaderCodegenPath path) {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));

    Callable callable = [](Var<CodegenPolicyRecord> record, Var<real> bias) {
        return record.weight + bias + record.uv.x;
    };
    callable.function()->set_description("callable_" + string(precision_suffix(precision)));

    Kernel kernel = [&](Var<CodegenPolicyRecord> record, Var<real> bias) {
        Var<real> sum = callable(record, bias);
        comment("resolved type callable kernel body");
        sum = sum + cast<real>(1.0f);
    };

    string shader_desc = "test_ast_to_cuda_source_callable_" + string(precision_suffix(precision));
    std::string source = compile_and_read_cuda_source(device, context, kernel, shader_desc, path);
    set_global_storage_policy(previous_policy);
    return source;
}

template<PrecisionPolicy precision>
[[nodiscard]] std::string compile_kernel_cuda_source(Device &device,
                                                     RHIContext &context,
                                                     ShaderCodegenPath path) {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));

    Kernel kernel = [](Var<real> bias, Var<CodegenPolicyRecord> record) {
        Var<real> sum = record.weight + bias + record.uv.y;
        comment("resolved type kernel body");
        sum = sum + cast<real>(1.0f);
    };

    string shader_desc = "test_ast_to_cuda_source_kernel_" + string(precision_suffix(precision));
    std::string source = compile_and_read_cuda_source(device, context, kernel, shader_desc, path);
    set_global_storage_policy(previous_policy);
    return source;
}

template<PrecisionPolicy precision>
[[nodiscard]] std::string compile_nested_kernel_cuda_source(Device &device,
                                                            RHIContext &context,
                                                            ShaderCodegenPath path) {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));

    Kernel kernel = [](Var<real> bias, Var<CodegenNestedRecord> record) {
        Var<CodegenNestedRecord> local_record = record;
        Var<CodegenNestedInner> local_inner = local_record.inner;
        Var<real> sum = local_inner.weight + local_record.gain + bias + local_inner.uv.x;
        comment("resolved nested kernel body");
        sum = sum + cast<real>(1.0f);
    };

    string shader_desc = "test_ast_to_cuda_source_nested_kernel_" + string(precision_suffix(precision));
    std::string source = compile_and_read_cuda_source(device, context, kernel, shader_desc, path);
    set_global_storage_policy(previous_policy);
    return source;
}

template<PrecisionPolicy precision>
[[nodiscard]] shared_ptr<Function> make_nested_kernel_function() {
    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(make_policy(precision));
    Kernel kernel = [](Var<real> bias, Var<CodegenNestedRecord> record) {
        Var<CodegenNestedRecord> local_record = record;
        Var<CodegenNestedInner> local_inner = local_record.inner;
        Var<real> sum = local_inner.weight + local_record.gain + bias + local_inner.uv.x;
        comment("resolved nested kernel body");
        sum = sum + cast<real>(1.0f);
    };
    kernel.function()->set_description(precision == PrecisionPolicy::force_f16 ? "nested_kernel_f16" : "nested_kernel_f32");
    auto function = kernel.function();
    set_global_storage_policy(previous_policy);
    return function;
}

[[nodiscard]] bool expect_cpp_markers(std::string_view source, PrecisionPolicy precision) {
    if (precision == PrecisionPolicy::force_f16) {
        CHECK(contains(source, "half "));
        CHECK(contains(source, "half2"));
        CHECK(!contains(source, "real "));
    } else {
        CHECK(contains(source, "float "));
        CHECK(contains(source, "float2"));
        CHECK(!contains(source, "real "));
    }
    return true;
}

[[nodiscard]] bool expect_cuda_markers(std::string_view source, PrecisionPolicy precision) {
    if (precision == PrecisionPolicy::force_f16) {
        CHECK(contains(source, "oc_half "));
        CHECK(contains(source, "oc_half2"));
        CHECK(!contains(source, "oc_real"));
    } else {
        CHECK(contains(source, "oc_float "));
        CHECK(contains(source, "oc_float2"));
        CHECK(!contains(source, "oc_real"));
    }
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_callable_cpp_source() {
    auto function = make_callable_function<precision>();
    std::string cpp_source = emit_cpp_source(*function);
    CHECK(contains(cpp_source, precision == PrecisionPolicy::force_f16 ? "callable_f16" : "callable_f32"));
    CHECK(expect_cpp_markers(cpp_source, precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_callable_ast_types() {
    auto function = make_callable_function<precision>();
    CHECK(function->arguments().size() == 2u);
    CHECK(function->arguments()[0].type() == resolved_type(Type::of<CodegenPolicyRecord>(), precision));
    CHECK(function->arguments()[1].type() == resolved_type(Type::of<real>(), precision));
    CHECK(function->return_type() == resolved_type(Type::of<real>(), precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_kernel_cpp_source() {
    auto function = make_kernel_function<precision>();
    std::string cpp_source = emit_cpp_source(*function);
    CHECK(contains(cpp_source, precision == PrecisionPolicy::force_f16 ? "kernel_f16" : "kernel_f32"));
    CHECK(expect_cpp_markers(cpp_source, precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_callable_cuda_source(Device &device, RHIContext &context) {
    std::string cuda_source = compile_callable_kernel_cuda_source<precision>(device,
                                                                             context,
                                                                             ShaderCodegenPath::EAstToSource);
    CHECK(contains(cuda_source, "test_ast_to_cuda_source_callable_" + string(precision_suffix(precision))));
    CHECK(contains(cuda_source, precision == PrecisionPolicy::force_f16 ? "callable_f16" : "callable_f32"));
    CHECK(contains(cuda_source, "__device__"));
    CHECK(contains(cuda_source, "extern \"C\" __global__"));
    CHECK(expect_cuda_markers(cuda_source, precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_kernel_cuda_source(Device &device, RHIContext &context) {
    std::string cuda_source = compile_kernel_cuda_source<precision>(device,
                                                                    context,
                                                                    ShaderCodegenPath::EAstToSource);
    CHECK(contains(cuda_source, "test_ast_to_cuda_source_kernel_" + string(precision_suffix(precision))));
    CHECK(contains(cuda_source, "extern \"C\" __global__"));
    CHECK(expect_cuda_markers(cuda_source, precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_cuda_ir_path_matches_ast_source(Device &device, RHIContext &context) {
    CHECK(expect_same_source("kernel",
                             compile_kernel_cuda_source<precision>(device,
                                                                   context,
                                                                   ShaderCodegenPath::EAstToSource),
                             compile_kernel_cuda_source<precision>(device,
                                                                   context,
                                                                   ShaderCodegenPath::EAstToIR)));
    CHECK(expect_same_source("nested_kernel",
                             compile_nested_kernel_cuda_source<precision>(device,
                                                                          context,
                                                                          ShaderCodegenPath::EAstToSource),
                             compile_nested_kernel_cuda_source<precision>(device,
                                                                          context,
                                                                          ShaderCodegenPath::EAstToIR)));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_nested_kernel_ast_and_cpp_source() {
    auto function = make_nested_kernel_function<precision>();
    const Type *resolved_record = resolved_type(Type::of<CodegenNestedRecord>(), precision);
    const Type *resolved_inner = resolved_type(Type::of<CodegenNestedInner>(), precision);
    const Type *resolved_real = resolved_type(Type::of<real>(), precision);
    CHECK(function->arguments().size() == 2u);
    CHECK(function->arguments()[0].type() == resolved_real);
    CHECK(function->arguments()[1].type() == resolved_record);
    CHECK(function->body()->local_vars().size() >= 3u);
    CHECK(function->body()->local_vars()[0].type() == resolved_record);
    CHECK(function->body()->local_vars()[1].type() == resolved_inner);
    CHECK(function->body()->local_vars()[2].type() == resolved_real);
    bool saw_record = false;
    bool saw_inner = false;
    function->for_each_structure([&](const Type *type) {
        if (type == resolved_record) {
            saw_record = true;
        }
        if (type == resolved_inner) {
            saw_inner = true;
        }
    });
    CHECK(saw_record);
    CHECK(saw_inner);

    std::string cpp_source = emit_cpp_source(*function);
    CHECK(contains(cpp_source, precision == PrecisionPolicy::force_f16 ? "nested_kernel_f16" : "nested_kernel_f32"));
    CHECK(expect_cpp_markers(cpp_source, precision));
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_nested_kernel_cuda_source(Device &device, RHIContext &context) {
    std::string cuda_source = compile_nested_kernel_cuda_source<precision>(device,
                                                                           context,
                                                                           ShaderCodegenPath::EAstToSource);
    CHECK(contains(cuda_source, "test_ast_to_cuda_source_nested_kernel_" + string(precision_suffix(precision))));
    CHECK(contains(cuda_source, "extern \"C\" __global__"));
    CHECK(expect_cuda_markers(cuda_source, precision));
    return true;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");

    bool passed = true;
    passed = check_impl(Env::shader_codegen_path() == ShaderCodegenPath::EAstToSource,
                        "Env::shader_codegen_path() == ShaderCodegenPath::EAstToSource") && passed;
    passed = test_callable_ast_types<PrecisionPolicy::force_f16>() && passed;
    passed = test_callable_ast_types<PrecisionPolicy::force_f32>() && passed;
    passed = test_callable_cpp_source<PrecisionPolicy::force_f16>() && passed;
    passed = test_callable_cpp_source<PrecisionPolicy::force_f32>() && passed;
    passed = test_callable_cuda_source<PrecisionPolicy::force_f16>(device, context) && passed;
    passed = test_callable_cuda_source<PrecisionPolicy::force_f32>(device, context) && passed;
    passed = test_kernel_cpp_source<PrecisionPolicy::force_f16>() && passed;
    passed = test_kernel_cpp_source<PrecisionPolicy::force_f32>() && passed;
    passed = test_kernel_cuda_source<PrecisionPolicy::force_f16>(device, context) && passed;
    passed = test_kernel_cuda_source<PrecisionPolicy::force_f32>(device, context) && passed;
    passed = test_cuda_ir_path_matches_ast_source<PrecisionPolicy::force_f16>(device, context) && passed;
    passed = test_cuda_ir_path_matches_ast_source<PrecisionPolicy::force_f32>(device, context) && passed;
    passed = test_nested_kernel_ast_and_cpp_source<PrecisionPolicy::force_f16>() && passed;
    passed = test_nested_kernel_ast_and_cpp_source<PrecisionPolicy::force_f32>() && passed;
    passed = test_nested_kernel_cuda_source<PrecisionPolicy::force_f16>(device, context) && passed;
    passed = test_nested_kernel_cuda_source<PrecisionPolicy::force_f32>(device, context) && passed;
    if (!passed) {
        return 1;
    }
    std::cout << "ast to cuda source resolved type test passed" << std::endl;
    return 0;
}