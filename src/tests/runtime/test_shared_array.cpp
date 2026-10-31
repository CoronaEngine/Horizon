#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <cstdlib>
#include <iostream>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "shared array: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

}// namespace

int main() {
    auto device = create_test_device();
    constexpr uint width = 32u, height = 8u, lanes = 128u;
    auto values = device.create_buffer<float4>(width * height);
    auto identities = device.create_buffer<uint4>(width * height);
    Kernel kernel = [](BufferVar<float4> out_values, BufferVar<uint4> out_identities) {
        SharedArray<float4, lanes> shared_values;
        SharedArray<uint4, lanes> shared_identities;
        Uint lane = thread_idx().y * 16u + thread_idx().x;
        Uint index = dispatch_id();
        Float value = cast<float>(index);
        shared_values.write(lane, make_float4(value, value + 0.5f, -value, 1.f));
        shared_identities.write(lane, make_uint4(index, lane, block_idx().x, 7u));
        synchronize_block();
        Uint next = (lane + 1u) % lanes;
        out_values.write(index, shared_values.read(next));
        out_identities.write(index, shared_identities.read(next));
    };
    kernel.function()->set_block_dim(16u, 8u);
    require(kernel.function()->shared_vars().size() == 2u, "shared allocations missing from AST");
    for (const auto &var : kernel.function()->shared_vars()) {
        require(var.tag() == Variable::Tag::SHARED, "allocation has per-thread storage class");
    }
    auto shader = device.compile(kernel, "shared-array-neighbor-exchange");
    const auto source = RHIContext::instance().read_global_cache(kernel.function()->func_name() + ".slang");
    const auto main = source.find("void main");
    require(main != string::npos, "generated entry point missing");
    for (const auto &var : kernel.function()->shared_vars()) {
        const string declaration = string("groupshared ") +
                                   (var.type()->element() == Type::of<float4>() ? "float4[128u] " : "uint4[128u] ") +
                                   var.final_name() + ";";
        require(source.find(declaration) < main, "shared allocation is not an uninitialized module-scope declaration");
        for (const auto &local : kernel.function()->body()->local_vars()) {
            require(local.uid() != var.uid(), "shared allocation also appears in per-thread locals");
        }
    }
    vector<float4> actual_values(width * height);
    vector<uint4> actual_identities(width * height);
    auto stream = device.create_stream();
    stream << shader(values, identities).dispatch(width, height)
           << values.download(actual_values.data()) << identities.download(actual_identities.data())
           << synchronize() << commit();
    for (uint y = 0u; y < height; ++y) {
        for (uint x = 0u; x < width; ++x) {
            uint lane = y * 16u + x % 16u;
            uint next = (lane + 1u) % lanes;
            uint group = x / 16u;
            uint expected_index = (next / 16u) * width + group * 16u + next % 16u;
            auto expected_values = make_float4(float(expected_index), float(expected_index) + 0.5f,
                                               -float(expected_index), 1.f);
            auto expected_identities = make_uint4(expected_index, next, group, 7u);
            require(all(actual_values[y * width + x] == expected_values), "float4 neighbor exchange mismatch");
            require(all(actual_identities[y * width + x] == expected_identities), "uint4 neighbor exchange mismatch");
        }
    }
    Kernel small_kernel = [] { [[maybe_unused]] SharedArray<float4, 4u> data; };
    Kernel large_kernel = [] { [[maybe_unused]] SharedArray<float4, 8u> data; };
    require(small_kernel.function()->hash() != large_kernel.function()->hash(), "shared extent missing from function hash");
    std::cout << "shared float4/uint4 declarations and two-workgroup neighbor exchange passed" << std::endl;
}
