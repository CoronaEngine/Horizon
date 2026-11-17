#include "tests/backend_test.h"
#include "dsl/dsl.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace ocarina;

namespace {
constexpr uint sentinel = 0xcdcdcdcdu;
constexpr uint records = 6u;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "warp32 fallback: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void run_case(Device &device, uint3 block) {
    const uint3 dim = make_uint3(block.x * 2u + 3u, block.y + 1u, block.z);
    const uint count = dim.x * dim.y * dim.z;
    auto input = device.create_buffer<uint>(count);
    auto output = device.create_buffer<uint4>(count * records + 2u);
    Kernel kernel = [](BufferVar<uint> in, BufferVar<uint4> out) {
        Uint i = dispatch_id();
        // Tail invocations must participate in collectives without reading in[i].
        Uint value = in.read(i);
        Bool selected = value % 3u == 1u;
        out.write(i * records, make_uint4(warp_lane_id(), warp_size(), warp_first_active_lane(),
                                          cast<uint>(warp_is_first_active_lane())));
        out.write(i * records + 1u,
                  make_uint4(warp_prefix_count_bits(selected), warp_active_count_bits(selected),
                             warp_prefix_count_bits(Bool(true)), warp_active_count_bits(Bool(true))));
        out.write(i * records + 2u, warp_active_bit_mask(selected));
        synchronize_block();
        $if(value % 2u == 1u) {
            out.write(i * records + 3u,
                      make_uint4(warp_first_active_lane(), cast<uint>(warp_is_first_active_lane()),
                                 warp_active_count_bits(Bool(true)), warp_prefix_count_bits(Bool(true))));
            $if(value % 5u == 0u) {
                out.write(i * records + 4u, warp_active_bit_mask(Bool(true)));
            } $else {
                out.write(i * records + 4u, warp_active_bit_mask(Bool(true)));
            };
        };
        $if(value % 7u == 0u) { $return(); };
        out.write(i * records + 5u, warp_active_bit_mask(Bool(true)));
    };
    kernel.function()->set_block_dim(block);
    auto shader = device.compile(kernel, "warp32-fallback-" + std::to_string(block.x) + "-" +
                                            std::to_string(block.y) + "-" + std::to_string(block.z));
    const auto source = RHIContext::instance().read_global_cache(kernel.function()->func_name() + ".slang");
    require(source.find("oc_warp32_ballot") != string::npos, "forced fallback was not emitted");
    vector<uint> values(count);
    for (uint i = 0; i < count; ++i) values[i] = i;
    vector<uint4> actual(count * records + 2u);
    auto stream = device.create_stream();
    for (uint repeat = 0u; repeat < 2u; ++repeat) {
        stream << input.upload(values.data()) << output.byte_set(0xcdu)
               << shader(input, output.view(1u, count * records)).dispatch(dim)
               << output.download(actual.data()) << synchronize() << commit();
        require(all(actual.front() == make_uint4(sentinel)) && all(actual.back() == make_uint4(sentinel)),
                "buffer boundary overwritten");
        for (uint i = 0u; i < count; ++i) {
            const uint3 p = make_uint3(i % dim.x, i / dim.x % dim.y, i / (dim.x * dim.y));
            const uint3 group = p / block;
            const uint3 local = p % block;
            const uint flat = local.x + block.x * (local.y + block.y * local.z);
            const uint lane = flat % 32u, first = flat / 32u * 32u;
            uint active_mask = 0u, selected_mask = 0u, odd_mask = 0u, nested_mask = 0u, live_mask = 0u;
            for (uint j = first; j < std::min(first + 32u, block.x * block.y * block.z); ++j) {
                const auto q = group * block + make_uint3(j % block.x, j / block.x % block.y,
                                                          j / (block.x * block.y));
                if (q.x >= dim.x || q.y >= dim.y || q.z >= dim.z) continue;
                const uint value = q.x + dim.x * (q.y + dim.y * q.z), bit = 1u << (j - first);
                active_mask |= bit;
                if (value % 3u == 1u) selected_mask |= bit;
                if (value % 2u == 1u) odd_mask |= bit;
                if (value % 2u == 1u && value % 5u == 0u) nested_mask |= bit;
                if (value % 7u != 0u) live_mask |= bit;
            }
            const uint preceding = (1u << lane) - 1u;
            auto popcount = [](uint bits) { return uint(std::popcount(bits)); };
            const uint first_active = uint(std::countr_zero(active_mask));
            require(all(actual[1u + i * records] == make_uint4(lane, 32u, first_active, uint(lane == first_active))),
                    "lane/size/first-active mismatch");
            require(all(actual[2u + i * records] == make_uint4(popcount(selected_mask & preceding), popcount(selected_mask),
                                                              popcount(active_mask & preceding), popcount(active_mask))),
                    "prefix/count mismatch");
            require(all(actual[3u + i * records] == make_uint4(selected_mask, 0u, 0u, 0u)), "ballot mismatch");
            const uint first_odd = uint(std::countr_zero(odd_mask));
            require(all(actual[4u + i * records] == (i % 2u == 1u
                        ? make_uint4(first_odd, uint(lane == first_odd), popcount(odd_mask), popcount(odd_mask & preceding))
                        : make_uint4(sentinel))), "divergent participation mismatch");
            require(all(actual[5u + i * records] == (i % 2u == 1u
                        ? make_uint4(i % 5u == 0u ? nested_mask : odd_mask & ~nested_mask, 0u, 0u, 0u)
                        : make_uint4(sentinel))), "nested if/else branch mismatch");
            require(all(actual[6u + i * records] == (i % 7u != 0u
                        ? make_uint4(live_mask, 0u, 0u, 0u) : make_uint4(sentinel))), "early-return mask mismatch");
        }
    }
    std::cout << "WARP32_FALLBACK block=" << block.x << ',' << block.y << ',' << block.z
              << " invocations=" << count << " repeats=2 passed" << std::endl;
}
}

int main(int argc, char **argv) {
    auto device = create_test_device();
    if (argc == 3 && std::string_view(argv[1]) == "--case") {
        const std::string_view name{argv[2]};
        auto output = device.create_buffer<uint>(64u);
        if (name == "loop") {
            Kernel kernel = [](BufferVar<uint> out) {
                Uint n = 0u;
                $loop {
                    out.write(dispatch_id(), warp_active_count_bits(Bool(true)));
                    n += 1u;
                    $if(n == 2u) { $break; };
                };
            };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-loop");
        } else if (name == "callable") {
            Callable collective = [] { return warp_active_count_bits(Bool(true)); };
            Kernel kernel = [&collective](BufferVar<uint> out) { out.write(dispatch_id(), collective()); };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-callable");
        } else if (name == "plain-loop") {
            Kernel kernel = [](BufferVar<uint> out) {
                Uint count = warp_active_count_bits(Bool(true));
                Uint n = 0u;
                $loop {
                    n += 1u;
                    $if(n == 2u) { $break; };
                };
                out.write(dispatch_id(), count + n);
            };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-plain-loop");
        } else if (name == "callable-loop") {
            Callable loop = [] {
                Uint n = 0u;
                $for(i, 2u) { n += i; };
                return n;
            };
            Kernel kernel = [&loop](BufferVar<uint> out) {
                out.write(dispatch_id(), warp_active_count_bits(Bool(true)) + loop());
            };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-callable-loop");
        } else if (name == "barrier") {
            Kernel kernel = [](BufferVar<uint> out) {
                out.write(dispatch_id(), warp_active_count_bits(Bool(true)));
                $if(dispatch_id() % 2u == 1u) { synchronize_block(); };
            };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-conditional-barrier");
        } else if (name == "short-circuit") {
            Kernel kernel = [] {
                // Public DSL operators materialize their operands eagerly.
                // Build a truly nested lazy expression to exercise the AST boundary.
                auto f = Function::current();
                auto vote = f->call_builtin(Type::of<uint>(), CallOp::WARP_ACTIVE_COUNT_BITS,
                                            {f->literal(Type::of<bool>(), true)});
                auto nonzero = f->binary(Type::of<bool>(), BinaryOp::GREATER, vote,
                                        f->literal(Type::of<uint>(), 0u));
                f->expr_statement(f->binary(Type::of<bool>(), BinaryOp::AND,
                                            f->literal(Type::of<bool>(), false), nonzero));
            };
            [[maybe_unused]] auto shader = device.compile(kernel, "warp32-unsupported-short-circuit");
        } else {
            require(false, "unknown rejection case");
        }
        require(false, "unsupported fallback shader was accepted");
    }
    require(argc == 1, "usage: test-runtime-warp32-fallback [--case loop|plain-loop|callable|callable-loop|barrier|short-circuit]");
    for (uint3 block : {make_uint3(8u, 1u, 1u), make_uint3(32u, 1u, 1u), make_uint3(40u, 1u, 1u), make_uint3(64u, 1u, 1u),
                        make_uint3(8u, 4u, 2u)}) run_case(device, block);
    std::cout << "warp32 fallback checks passed" << std::endl;
}
