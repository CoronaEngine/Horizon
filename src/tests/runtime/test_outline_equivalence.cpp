#include "tests/backend_test.h"
#include "dsl/dsl.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace ocarina;

namespace {

constexpr std::array seeds{0u, 1u, 2u, 7u, 19u, 43u, 255u, 1023u};

template<size_t field_count>
using Results = std::array<uint, seeds.size() * field_count>;

template<size_t field_count, typename KernelType>
Results<field_count> execute(Device &device, const KernelType &kernel, const char *name, uint unwritten) {
    auto input = device.create_buffer<uint>(seeds.size());
    auto output = device.create_buffer<uint>(seeds.size() * field_count);
    auto shader = device.compile(kernel, name);
    Results<field_count> initial;
    initial.fill(unwritten);
    Results<field_count> actual{};
    auto stream = device.create_stream();
    stream << input.upload(seeds.data()) << output.upload(initial.data())
           << shader(input, output).dispatch(static_cast<uint>(seeds.size()))
           << output.download(actual.data()) << synchronize() << commit();
    return actual;
}

template<size_t field_count, typename KernelFactory>
bool compare(Device &device, KernelFactory make_kernel, const Results<field_count> &expected) {
    auto direct_kernel = make_kernel(false);
    auto outlined_kernel = make_kernel(true);
    const auto has_callable = [](const auto &kernel) {
        bool found = false;
        kernel.function()->for_each_custom_func([&](const Function *) { found = true; });
        return found;
    };
    if (has_callable(direct_kernel) || !has_callable(outlined_kernel)) {
        std::cerr << "expected a kernel without callables and a kernel with outlined callables\n";
        return false;
    }
    auto direct = execute<field_count>(device, direct_kernel, "direct", 0xfffffffdu);
    auto outlined = execute<field_count>(device, outlined_kernel, "outlined", 0xfffffffeu);
    bool passed = true;
    for (size_t i = 0; i < expected.size(); ++i) {
        if (direct[i] != expected[i] || outlined[i] != direct[i]) {
            std::cerr << "seed=" << seeds[i / field_count] << " field=" << i % field_count
                      << " expected=" << expected[i] << " direct=" << direct[i]
                      << " outlined=" << outlined[i] << '\n';
            passed = false;
        }
    }
    return passed;
}

bool repeated_output(Device &device) {
    static constexpr uint field_count = 8;
    const auto make_kernel = [](bool outlined) {
        return Kernel{[outlined](BufferVar<uint> input, BufferVar<uint> output) {
            Uint lane = dispatch_id();
            Float state = cast<float>(input.read(lane, false));
            const Expression *produced = nullptr;
            auto body = [&](const Uint &delta) {
                state = state * 2.f + cast<float>(delta);
                Uint local = cast<uint>(state) * 3u + 7u;
                produced = local.expression();
            };
            auto run = [&](auto &&invoke) {
                auto save = [&](uint checkpoint) {
                    Uint base = lane * field_count + checkpoint * 2u;
                    output.write(base, cast<uint>(state), false);
                    output.write(base + 1u, Uint{produced}, false);
                };
                invoke(Uint{1u});
                save(0);
                invoke(Uint{3u});
                save(1);
                auto pair = [&] {
                    invoke(Uint{5u});
                    invoke(Uint{7u});
                };
                auto last = [&] { invoke(Uint{11u}); };
                if (outlined) {
                    outline(pair);
                    save(2);
                    outline([&] { outline(last); });
                } else {
                    pair();
                    save(2);
                    last();
                }
                save(3);
            };
            if (outlined) {
                Callable shared{body, detail::allow_capture_tag};
                shared.function()->set_allow_dsl_capture(true);
                run(shared);
            } else {
                run(body);
            }
        }};
    };

    Results<field_count> expected{};
    for (size_t i = 0; i < seeds.size(); ++i) {
        const uint seed = seeds[i];
        const std::array states{2u * seed + 1u, 4u * seed + 5u, 16u * seed + 37u, 32u * seed + 85u};
        for (size_t checkpoint = 0; checkpoint < states.size(); ++checkpoint) {
            expected[i * field_count + checkpoint * 2u] = states[checkpoint];
            expected[i * field_count + checkpoint * 2u + 1u] = 3u * states[checkpoint] + 7u;
        }
    }
    return compare<field_count>(device, make_kernel, expected);
}

bool returned_output(Device &device) {
    static constexpr uint field_count = 2;
    const auto make_kernel = [](bool outlined) {
        return Kernel{[outlined](BufferVar<uint> input, BufferVar<uint> output) {
            Uint lane = dispatch_id();
            Uint seed = input.read(lane, false);
            const Expression *escaped = nullptr;
            auto body = [&] {
                Uint local = seed * 3u + 5u;
                escaped = local.expression();
                return local + 11u;
            };
            Uint returned = outlined ? outline(body) : body();
            output.write(lane * field_count, returned, false);
            output.write(lane * field_count + 1u, Uint{escaped}, false);
        }};
    };

    Results<field_count> expected{};
    for (size_t i = 0; i < seeds.size(); ++i) {
        expected[i * field_count] = seeds[i] * 3u + 16u;
        expected[i * field_count + 1u] = seeds[i] * 3u + 5u;
    }
    return compare<field_count>(device, make_kernel, expected);
}

bool control_flow_resources(Device &device) {
    static constexpr uint field_count = 2;
    const auto make_kernel = [](bool outlined) {
        return Kernel{[outlined](BufferVar<uint> input, BufferVar<uint> output) {
            Uint lane = dispatch_id();
            Uint seed = input.read(lane, false);
            Uint value = seed;
            auto body = [&] {
                $if ((seed & 1u) != 0u) {
                    value += 3u;
                }
                $else {
                    value *= 2u;
                };
                for_range(seed % 4u, [&](Uint index, Continue, Break) { value += index + 1u; });
                output.write(lane * field_count, value, false);
            };
            if (outlined) {
                outline([&] { outline(body); });
            } else {
                body();
            }
            output.write(lane * field_count + 1u, value + 13u, false);
        }};
    };

    Results<field_count> expected{};
    for (size_t i = 0; i < seeds.size(); ++i) {
        const uint seed = seeds[i];
        const uint iterations = seed % 4u;
        const uint value = (seed % 2u == 0 ? seed * 2u : seed + 3u) + iterations * (iterations + 1u) / 2u;
        expected[i * field_count] = value;
        expected[i * field_count + 1u] = value + 13u;
    }
    return compare<field_count>(device, make_kernel, expected);
}

}// namespace

int main(int argc, char **argv) {
    if (argc != 3 || std::string_view{argv[1]} != "--case") {
        std::cerr << "usage: --case repeated-output|returned-output|control-flow-resources\n";
        return EXIT_FAILURE;
    }
    const std::string_view name{argv[2]};
    auto device = create_test_device();
    if (name == "repeated-output") {
        return repeated_output(device) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (name == "returned-output") {
        return returned_output(device) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (name == "control-flow-resources") {
        return control_flow_resources(device) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    std::cerr << "unknown case: " << name << '\n';
    return EXIT_FAILURE;
}
