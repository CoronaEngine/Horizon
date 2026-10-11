#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace ocarina;

namespace {
constexpr uint sentinel = 0xcdcdcdcdu;
constexpr uint record_count = 5u;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "compute dispatch parity: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

bool valid_dim(uint3 dim) { return dim.x && dim.y && dim.z; }
uint volume(uint3 dim) { return dim.x * dim.y * dim.z; }
uint3 ceil_grid(uint3 dim, uint3 block) {
    return make_uint3(dim.x / block.x + uint(dim.x % block.x != 0u),
                     dim.y / block.y + uint(dim.y % block.y != 0u),
                     dim.z / block.z + uint(dim.z % block.z != 0u));
}
void expect_sentinel(uint4 value) {
    for (uint channel = 0u; channel < 4u; ++channel)
        require(value[channel] == sentinel, "unlaunched coordinate or buffer guard was overwritten");
}

void test_layout_cases(Device &device, const char *backend, const char *name,
                       uint3 grid, uint3 block, const vector<uint3> &dimensions, bool fit = false) {
    // Callables must see the caller's dispatch/group coordinates too.
    Callable coordinates = [] {
        return make_uint4(block_idx().x, thread_idx().y, thread_id(), dispatch_id());
    };
    Kernel kernel = [&coordinates](BufferVar<uint4> out, Uint stamp) {
        Uint i = dispatch_id();
        out.write(i * record_count, make_uint4(dispatch_idx(), i));
        out.write(i * record_count + 1u, make_uint4(dispatch_dim(), stamp));
        out.write(i * record_count + 2u, make_uint4(block_idx(), thread_id()));
        out.write(i * record_count + 3u, make_uint4(thread_idx(), dispatch_id(dispatch_idx())));
        out.write(i * record_count + 4u, coordinates());
    };
    kernel.function()->configure(grid, block);
    auto shader = device.compile(kernel, string("compute-dispatch-") + name);
    if (fit) shader.compute_fit_size();
    const auto actual_configured_block = kernel.function()->block_dim();
    const auto actual_configured_grid = kernel.function()->grid_dim();
    // Auto choices are observable backend policy, not a shared block-shape
    // contract. Validate exact block/thread IDs only for an explicit block.
    const bool exact_topology = valid_dim(block) && !fit;
    auto stream = device.create_stream();
    uint stamp = 101u;
    for (uint3 dim : dimensions) {
        ++stamp;
        const uint count = volume(dim);
        const uint payload = std::max(1u, count * record_count);
        auto output = device.create_buffer<uint4>(payload + 2u);
        vector<uint4> host(payload + 2u, make_uint4(sentinel));
        stream << output.byte_set(0xcdu)
               << shader(output.view(1u, payload), stamp).dispatch(dim)
               << output.download(host.data()) << synchronize() << commit();
        expect_sentinel(host.front());
        expect_sentinel(host.back());
        uint3 expected_grid{};
        if (exact_topology) expected_grid = valid_dim(grid) ? grid : ceil_grid(dim, block);
        uint3 max_group{}, max_local{};
        uint visited = 0u, max_thread = 0u;
        for (uint i = 0u; i < count; ++i) {
            const auto index = make_uint3(i % dim.x, (i / dim.x) % dim.y, i / (dim.x * dim.y));
            const uint base = 1u + i * record_count;
            const bool launched = !exact_topology ||
                (index.x < expected_grid.x * block.x && index.y < expected_grid.y * block.y &&
                 index.z < expected_grid.z * block.z);
            if (!launched) {
                for (uint record = 0u; record < record_count; ++record) expect_sentinel(host[base + record]);
                continue;
            }
            ++visited;
            const auto position = host[base];
            const auto shape = host[base + 1u];
            const auto group = host[base + 2u];
            const auto local = host[base + 3u];
            const auto callable = host[base + 4u];
            require(position.x == index.x && position.y == index.y && position.z == index.z && position.w == i,
                    "global dispatch coordinate or linear index differs");
            require(shape.x == dim.x && shape.y == dim.y && shape.z == dim.z && shape.w == stamp,
                    "dispatch dimensions or per-invocation argument snapshot differs");
            require(local.w == i, "dispatch_id(dispatch_idx()) does not round-trip");
            require(callable.x == group.x && callable.y == local.y && callable.z == group.w && callable.w == i,
                    "callable cannot observe the caller's builtin coordinates");
            if (exact_topology) {
                const auto g = make_uint3(index.x / block.x, index.y / block.y, index.z / block.z);
                const auto lane = make_uint3(index.x % block.x, index.y % block.y, index.z % block.z);
                const uint expected_thread = ((g.z * expected_grid.y + g.y) * expected_grid.x + g.x) * volume(block) +
                    (lane.z * block.y + lane.y) * block.x + lane.x;
                require(group.x == g.x && group.y == g.y && group.z == g.z && group.w == expected_thread,
                        "configured block or block-linear thread ID differs");
                require(local.x == lane.x && local.y == lane.y && local.z == lane.z,
                        "configured local thread coordinate differs");
            }
            for (uint axis = 0u; axis < 3u; ++axis) {
                max_group[axis] = std::max(max_group[axis], group[axis]);
                max_local[axis] = std::max(max_local[axis], local[axis]);
            }
            max_thread = std::max(max_thread, group.w);
        }
        if (count == 0u) expect_sentinel(host[1]);
        // These are observed active-lane maxima, not a claim about inactive
        // lanes in the backend's automatically chosen workgroup dimensions.
        std::cout << "COMPUTE_TOPOLOGY backend=" << backend << " case=" << name
                  << " dispatch=" << dim.x << ',' << dim.y << ',' << dim.z
                  << " function_block=" << actual_configured_block.x << ',' << actual_configured_block.y << ',' << actual_configured_block.z
                  << " function_grid=" << actual_configured_grid.x << ',' << actual_configured_grid.y << ',' << actual_configured_grid.z
                  << " active_group_max=" << max_group.x << ',' << max_group.y << ',' << max_group.z
                  << " active_local_max=" << max_local.x << ',' << max_local.y << ',' << max_local.z
                  << " max_thread_id=" << max_thread << " visited=" << visited << '\n';
    }
}

void test_full_group_barriers(Device &device) {
    constexpr uint bx = 4u, by = 3u, bz = 2u, group_size = bx * by * bz;
    const auto block = make_uint3(bx, by, bz);
    const auto grid = make_uint3(3u, 2u, 2u);
    const auto dim = make_uint3(12u, 6u, 4u);
    const uint count = volume(dim);
    auto scratch = device.create_buffer<uint>(count);
    auto output = device.create_buffer<uint4>(count);
    Kernel kernel = [](BufferVar<uint> scratch, BufferVar<uint4> out) {
        Uint3 group = block_idx();
        Uint3 local = thread_idx();
        Uint group_id = group.x + 3u * (group.y + 2u * group.z);
        Uint lane = local.x + bx * (local.y + by * local.z);
        Uint base = group_id * group_size;
        scratch.write(base + lane, 1000u + group_id * 100u + lane);
        synchronize_block();
        Uint next = scratch.read(base + (lane + 1u) % group_size);
        // Finish every neighbour read before any lane overwrites round one.
        synchronize_block();
        scratch.write(base + lane, next + 10u);
        synchronize_block();
        out.write(dispatch_id(), make_uint4(next, scratch.read(base + (lane + 1u) % group_size), group_id, lane));
    };
    kernel.function()->configure(grid, block);
    auto shader = device.compile(kernel, "compute-dispatch-full-3d-group-barriers");
    vector<uint4> host(count);
    auto stream = device.create_stream();
    // Every launched lane is inside dispatch_dim and reaches all three
    // barriers. Partial workgroups and divergent barrier control flow are
    // deliberately excluded. Scratch is a global buffer: no public shared
    // variable allocation API currently exists in this DSL.
    stream << shader(scratch, output).dispatch(dim) << output.download(host.data()) << synchronize() << commit();
    for (uint z = 0u; z < dim.z; ++z)
        for (uint y = 0u; y < dim.y; ++y)
            for (uint x = 0u; x < dim.x; ++x) {
                const uint group = x / bx + grid.x * (y / by + grid.y * (z / bz));
                const uint lane = x % bx + bx * (y % by + by * (z % bz));
                const auto value = host[(z * dim.y + y) * dim.x + x];
                require(value.x == 1000u + group * 100u + (lane + 1u) % group_size &&
                            value.y == 1010u + group * 100u + (lane + 2u) % group_size &&
                            value.z == group && value.w == lane,
                        "block barrier lost cross-lane global-memory visibility or group isolation");
            }
}

void test_warp_active_count(Device &device, const char *backend, uint block_size) {
    const uint count = block_size * 2u + 13u;
    auto output = device.create_buffer<uint4>(count + 2u);
    Kernel kernel = [](BufferVar<uint4> out) {
        Uint i = dispatch_id();
        Uint active = warp_active_count_bits(Bool(true));
        Uint empty = warp_active_count_bits(Bool(false));
        Uint selected = warp_active_count_bits(i % 3u == 1u);
        out.write(i, make_uint4(active, empty, selected, thread_idx().x));
    };
    kernel.function()->set_block_dim(block_size);
    auto shader = device.compile(kernel, string("compute-dispatch-warp-count-") + std::to_string(block_size));
    vector<uint4> host(count + 2u);
    auto stream = device.create_stream();
    stream << output.byte_set(0xcdu) << shader(output.view(1u, count)).dispatch(count)
           << output.download(host.data()) << synchronize() << commit();
    expect_sentinel(host.front());
    expect_sentinel(host.back());
    // For this 1D launch, verify contiguous subgroup partitions using the
    // observed full subgroup's active count; do not call the unsupported
    // public warp_size wrapper or assume both devices select 32 lanes.
    const uint observed_width = host[1u].x;
    require(observed_width != 0u && observed_width <= block_size && block_size % observed_width == 0u,
            "warp active count reported an invalid full subgroup size");
    for (uint i = 0u; i < count; ++i) {
        const uint lane = i % block_size;
        const uint first = (i / block_size) * block_size + (lane / observed_width) * observed_width;
        const uint end = std::min(first + observed_width, count);
        uint selected = 0u;
        for (uint j = first; j < end; ++j) selected += uint(j % 3u == 1u);
        const auto value = host[1u + i];
        require(value.x == end - first && value.y == 0u && value.z == selected && value.w == lane,
                "warp_active_count_bits differs for full or partially active subgroups");
    }
    std::cout << "COMPUTE_WARP_COUNT backend=" << backend << " block=" << block_size
              << " observed_full_subgroup_active_lanes=" << observed_width << " tail=13\n";
}

void probe_zero_dispatch(Device &device, const char *backend) {
    auto output = device.create_buffer<uint>(1u);
    Kernel kernel = [](BufferVar<uint> out) { out.write(0u, 123u); };
    auto shader = device.compile(kernel, "compute-dispatch-automatic-zero-probe");
    auto stream = device.create_stream();
    uint result = sentinel;
    stream << output.byte_set(0xcdu) << synchronize() << commit();
    // This isolated diagnostic exercises automatic sizing of an empty grid.
    // A backend may fail before or during its native launch and terminate;
    // callers must run the probe separately with a timeout and retain its
    // exit status. It is never part of the normal regression path.
    std::cout << "COMPUTE_ZERO_DISPATCH backend=" << backend << " phase=attempt" << std::endl;
    stream << shader(output).dispatch(0u) << output.download(&result) << synchronize() << commit();
    require(result == sentinel, "automatic zero dispatch wrote outside its empty domain");
    std::cout << "COMPUTE_ZERO_DISPATCH backend=" << backend
              << " result=no-op sentinel_unchanged=1" << std::endl;
}
}

int main(int argc, char **argv) {
    const bool zero_probe = argc == 2 && std::string_view(argv[1]) == "--zero-dispatch";
    require(argc == 1 || zero_probe, "usage: test-compute-dispatch-parity [--zero-dispatch]");
    const char *backend = test_backend_name();
    auto device = RHIContext::instance().create_device(backend);
    if (zero_probe) {
        probe_zero_dispatch(device, backend);
        return EXIT_SUCCESS;
    }
    test_layout_cases(device, backend, "automatic", make_uint3(0u), make_uint3(0u),
                      {make_uint3(1u, 1u, 1u), make_uint3(63u, 1u, 1u), make_uint3(65u, 1u, 1u),
                       make_uint3(13u, 7u, 1u), make_uint3(9u, 5u, 3u)});
    test_layout_cases(device, backend, "automatic-fit", make_uint3(0u), make_uint3(0u),
                      {make_uint3(65u, 1u, 1u), make_uint3(13u, 7u, 1u), make_uint3(9u, 5u, 3u)}, true);
    test_layout_cases(device, backend, "explicit-1d-tail", make_uint3(0u), make_uint3(8u, 1u, 1u),
                      {make_uint3(7u, 1u, 1u), make_uint3(8u, 1u, 1u), make_uint3(9u, 1u, 1u),
                       make_uint3(64u, 1u, 1u), make_uint3(65u, 1u, 1u)});
    test_layout_cases(device, backend, "explicit-3d-tail", make_uint3(0u), make_uint3(4u, 3u, 2u),
                      {make_uint3(9u, 7u, 5u)});
    test_layout_cases(device, backend, "explicit-grid-insufficient", make_uint3(1u), make_uint3(4u, 3u, 2u),
                      {make_uint3(9u, 7u, 5u)});
    test_layout_cases(device, backend, "explicit-grid-excess", make_uint3(4u), make_uint3(4u, 3u, 2u),
                      {make_uint3(9u, 7u, 5u)});
    test_layout_cases(device, backend, "partial-zero-grid-fallback", make_uint3(3u, 0u, 2u), make_uint3(4u, 3u, 2u),
                      {make_uint3(9u, 7u, 5u)});
    // A nonzero native launch with an empty logical domain is defined on both
    // backends: the generated bounds guard returns before every user write.
    // Automatic zero dispatch is separate: CUDA's current automatic sizing
    // produces a zero block width, then divides by it while deriving the grid.
    test_layout_cases(device, backend, "zero-domain-explicit-grid", make_uint3(1u), make_uint3(4u, 3u, 2u),
                      {make_uint3(0u, 7u, 5u), make_uint3(5u, 0u, 3u), make_uint3(5u, 3u, 0u), make_uint3(0u)});
    test_full_group_barriers(device);
    test_warp_active_count(device, backend, 32u);
    test_warp_active_count(device, backend, 64u);
    std::cout << "compute dispatch parity checks passed" << std::endl;
}
