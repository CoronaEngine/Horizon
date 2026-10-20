#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <slang.h>
#include <slang-com-ptr.h>
#include <iostream>
#include <limits>

using namespace ocarina;

namespace {

bool compile_without_implicit_conversions(const Function &function) {
    auto &context = RHIContext::instance();
    const string source = context.read_global_cache(function.func_name() + ".slang");
    if (source.empty()) {
        std::cerr << "generated Slang source is missing" << std::endl;
        return false;
    }
    Slang::ComPtr<slang::IGlobalSession> global;
    if (SLANG_FAILED(slang::createGlobalSession(global.writeRef()))) return false;
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = global->findProfile("spirv_1_5");
    const string includes = (context.runtime_directory() / "slang").string();
    const char *paths[]{includes.c_str()};
    slang::CompilerOptionEntry warning{};
    warning.name = slang::CompilerOptionName::WarningsAsErrors;
    warning.value.kind = slang::CompilerOptionValueKind::String;
    warning.value.stringValue0 = "30081";
    slang::SessionDesc description{};
    description.targets = &target;
    description.targetCount = 1;
    description.searchPaths = paths;
    description.searchPathCount = 1;
    description.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    description.compilerOptionEntries = &warning;
    description.compilerOptionEntryCount = 1;
    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global->createSession(description, session.writeRef()))) return false;
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> module;
    module = session->loadModuleFromSourceString("conversion_regression", nullptr,
                                                source.c_str(), diagnostics.writeRef());
    if (!module && diagnostics) {
        std::cerr << static_cast<const char *>(diagnostics->getBufferPointer()) << std::endl;
    }
    return module != nullptr;
}

}// namespace

int main() {
    auto device = create_test_device();
    const vector<float> inputs{0.f, -0.f, -2.75f, 5.75f, std::numeric_limits<float>::quiet_NaN()};
    auto input = device.create_buffer<float>(inputs.size());
    auto output = device.create_buffer<uint4>(inputs.size() * 3u);
    auto loop_output = device.create_buffer<uint>(inputs.size());
    auto mask_output = device.create_buffer<float4>(inputs.size());
    auto bytes = device.create_byte_buffer(13u);
    Kernel kernel = [](BufferVar<float> in, BufferVar<uint4> out, ByteBufferVar raw,
                       BufferVar<uint> loops, BufferVar<float4> masks) {
        Uint i = dispatch_id();
        Float value = in.read(i);
        Uint mask = i & 4u;
        Bool flag = mask;
        Bool numeric = value;
        Bool conjunction = value && mask;
        Bool disjunction = value || mask;
        Bool negated = !value;
        Uint branch = 0u;
        $if(value) { branch = 1u; };
        out.write(i * 3u, make_uint4(cast<uint>(flag), cast<uint>(numeric), branch, cast<uint>(negated)));
        // Both sign directions must truncate toward zero; only defined,
        // representable float-to-integer inputs are converted here.
        Uint positive = abs(select(value != value, 0.f, value));
        Int negative = -abs(select(value != value, 0.f, value));
        Uint count = in.size<uint>();
        Int byte_count = raw.size<int>();
        out.write(i * 3u + 1u, make_uint4(positive, cast<uint>(negative + 8), count, cast<uint>(byte_count)));
        Float3 lanes = make_float3(0.f, -2.75f, 5.75f);
        Int3 integers = lanes;
        Bool3 truths = lanes;
        out.write(i * 3u + 2u, make_uint4(cast<uint>(conjunction), cast<uint>(disjunction),
                                       cast<uint>(integers.y + 8), cast<uint>(truths.z)));
        Float left_mask = numeric * 3.5f;
        Float right_mask = 3.5f * numeric;
        masks.write(i, make_float4(left_mask, right_mask, lanes.y * numeric, numeric * lanes.z));
        Uint iterations = 0u;
        $loop {
            iterations += 1u;
            $if(iterations == i + 1u) { $break; };
        };
        $if((numeric < 0.5f) != !numeric || (0.5f < numeric) != numeric) {
            iterations += 100u;
        };
        loops.write(i, iterations);
    };
    auto shader = device.compile(kernel, "slang-explicit-conversions");
    if (!compile_without_implicit_conversions(*kernel.function())) return EXIT_FAILURE;
    vector<uint4> actual(inputs.size() * 3u);
    vector<uint> actual_loops(inputs.size());
    vector<float4> actual_masks(inputs.size());
    auto stream = device.create_stream();
    stream << input.upload(inputs.data()) << shader(input, output, bytes, loop_output, mask_output).dispatch(uint(inputs.size()))
           << output.download(actual.data()) << loop_output.download(actual_loops.data())
           << mask_output.download(actual_masks.data()) << synchronize() << commit();
    for (uint i = 0u; i < inputs.size(); ++i) {
        if (actual_loops[i] != i + 1u) {
            std::cerr << "loop iterations=" << actual_loops[i] << " expected=" << i + 1u << std::endl;
            return EXIT_FAILURE;
        }
        const bool truth = bool(inputs[i]);
        const float4 expected_mask = make_float4(truth ? 3.5f : 0.f, truth ? 3.5f : 0.f,
                                                 truth ? -2.75f : 0.f, truth ? 5.75f : 0.f);
        for (uint lane = 0u; lane < 4u; ++lane) {
            if (actual_masks[i][lane] != expected_mask[lane]) {
                std::cerr << "arithmetic mask row=" << i << " lane=" << lane << std::endl;
                return EXIT_FAILURE;
            }
        }
        const bool flag = bool(i & 4u);
        const uint magnitude = i == 2u ? 2u : i == 3u ? 5u : 0u;
        const uint4 expected[]{make_uint4(uint(flag), uint(truth), uint(truth), uint(!truth)),
                               make_uint4(magnitude, 8u - magnitude, uint(inputs.size()), 13u),
                               make_uint4(uint(truth && flag), uint(truth || flag), 6u, 1u)};
        for (uint row = 0u; row < 3u; ++row) {
            for (uint lane = 0u; lane < 4u; ++lane) {
                if (actual[i * 3u + row][lane] != expected[row][lane]) {
                    std::cerr << "conversion row=" << i * 3u + row << " lane=" << lane
                              << " actual=" << actual[i * 3u + row][lane]
                              << " expected=" << expected[row][lane] << std::endl;
                    return EXIT_FAILURE;
                }
            }
        }
    }
    std::cout << "Slang conversion diagnostics and value checks passed" << std::endl;
}
