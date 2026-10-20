#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

using namespace ocarina;

namespace {

template<typename T>
void check_value(T actual, T expected, const char *test, uint index) {
    if (actual != expected) {
        std::cerr << test << " index=" << index << " actual=" << actual
                  << " expected=" << expected << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

template<typename T>
T initial_value(uint i) {
    if constexpr (std::is_same_v<T, ulong>) return (ulong(1) << 40u) + ulong(i * 9u);
    else if constexpr (std::is_same_v<T, int>) return -200 + int(i * 9u);
    else if constexpr (std::is_same_v<T, float>) return -100.25f + float(i) * 0.5f;
    else return i == 3u ? T(~uint(0) - 1u) : T(100u + i * 9u);
}

// Each invocation owns one location: the exact return values can be checked
// without assuming which invocation wins a race. Both descriptor-based atomic
// methods and the free functions taking a resource reference are exercised.
template<typename T>
void test_atomic_views(Device &device, const char *type_name) {
    constexpr uint count = 37u;
    constexpr uint padding = 2u;
    constexpr bool integral = !std::is_floating_point_v<T>;
    constexpr uint rows = integral ? 8u : 6u;
    constexpr T sentinel = T(777);
    auto typed = device.create_buffer<T>(count + 2u * padding);
    auto bytes = device.create_byte_buffer((count + 2u * padding) * sizeof(T));
    auto returns = device.create_buffer<T>(count * rows * 2u);
    auto observed = device.create_buffer<T>(count * 2u);
    vector<T> initial(count + 2u * padding, sentinel);
    for (uint i = 0; i < count; ++i) initial[padding + i] = initial_value<T>(i);

    Kernel kernel = [](BufferVar<T> values, ByteBufferVar raw, BufferVar<T> out,
                       Var<T> add_delta, Var<T> sub_delta) {
        Uint i = dispatch_id();
        Uint byte_offset = i * uint(sizeof(T));
        Var<T> replacement = cast<T>(i) + T(1000);
        Uint row = i * (rows * 2u);
        out.write(row, values.atomic(i).fetch_add(T(3)));
        out.write(row + 1u, values.atomic(i).fetch_sub(T(1)));
        out.write(row + 2u, values.atomic(i).exchange(replacement));
        out.write(row + 3u, atomic_add(values.at(i), add_delta));
        out.write(row + 4u, atomic_sub(values.at(i), sub_delta));
        out.write(row + 5u, atomic_exch(values.at(i), replacement));
        if constexpr (integral) {
            out.write(row + 6u, atomic_CAS(values.at(i), replacement, replacement + T(7)));
            out.write(row + 7u, atomic_CAS(values.at(i), replacement, replacement + T(99)));
        }
        row += rows;
        out.write(row, raw.template atomic<T>(byte_offset).fetch_add(T(3)));
        out.write(row + 1u, raw.template atomic<T>(byte_offset).fetch_sub(T(1)));
        out.write(row + 2u, raw.template atomic<T>(byte_offset).exchange(replacement));
        out.write(row + 3u, atomic_add(raw.template load_as<T>(byte_offset), add_delta));
        out.write(row + 4u, atomic_sub(raw.template load_as<T>(byte_offset), sub_delta));
        out.write(row + 5u, atomic_exch(raw.template load_as<T>(byte_offset), replacement));
        if constexpr (integral) {
            out.write(row + 6u, atomic_CAS(raw.template load_as<T>(byte_offset), replacement, replacement + T(7)));
            out.write(row + 7u, atomic_CAS(raw.template load_as<T>(byte_offset), replacement, replacement + T(99)));
        }
    };
    Kernel observe = [](BufferVar<T> values, ByteBufferVar raw, BufferVar<T> out) {
        Uint i = dispatch_id();
        out.write(i * 2u, values.read(i));
        out.write(i * 2u + 1u, raw.template load_as<T>(i * uint(sizeof(T))));
    };
    auto shader = device.compile(kernel, ocarina::format("compute-atomic-views-{}", type_name));
    auto reader = device.compile(observe, ocarina::format("compute-atomic-observe-{}", type_name));
    vector<T> actual_returns(count * rows * 2u), actual_observed(count * 2u);
    vector<T> actual_typed(initial.size()), actual_bytes(initial.size());
    auto typed_view = typed.view(padding, count);
    auto byte_view = bytes.view(padding * sizeof(T), count * sizeof(T));
    auto stream = device.create_stream();
    // Commands in one stream are ordered. The reader is a separate dispatch;
    // it must see the preceding atomic writes without a CPU round trip.
    stream << typed.upload(initial.data()) << bytes.upload(initial.data())
           << shader(typed_view, byte_view, returns, T(4), T(2)).dispatch(count)
           << reader(typed_view, byte_view, observed).dispatch(count)
           << returns.download(actual_returns.data()) << observed.download(actual_observed.data())
           << typed.download(actual_typed.data()) << bytes.download(actual_bytes.data())
           << synchronize() << commit();
    for (uint i = 0; i < count; ++i) {
        T start = initial[padding + i], replacement = T(i) + T(1000);
        T expected[]{start, T(start + T(3)), T(start + T(2)), replacement,
                     T(replacement + T(4)), T(replacement + T(2)), replacement, T(replacement + T(7))};
        for (uint resource = 0; resource < 2u; ++resource)
            for (uint row = 0; row < rows; ++row)
                check_value(actual_returns[i * rows * 2u + resource * rows + row], expected[row],
                            "atomic view return", i * rows * 2u + resource * rows + row);
        T final_value = integral ? T(replacement + T(7)) : replacement;
        check_value(actual_observed[i * 2u], final_value, "typed next dispatch", i);
        check_value(actual_observed[i * 2u + 1u], final_value, "byte next dispatch", i);
        initial[padding + i] = final_value;
    }
    for (uint i = 0; i < initial.size(); ++i) {
        check_value(actual_typed[i], initial[i], "typed contents and sentinels", i);
        check_value(actual_bytes[i], initial[i], "byte contents and sentinels", i);
    }
    std::cout << "atomic views / references / next dispatch: " << type_name << std::endl;
}

// All increments are exactly representable, including the floating-point case.
// Sorting old values checks atomicity without relying on invocation scheduling.
template<typename T>
void test_contended_add(Device &device, const char *type_name) {
    constexpr uint count = 513u;
    T start = initial_value<T>(0u);
    T increment = std::is_floating_point_v<T> ? T(0.5f) : T(1);
    auto counter = device.create_buffer<T>(3u);
    auto bytes = device.create_byte_buffer(3u * sizeof(T));
    auto returns = device.create_buffer<T>(count * 2u);
    auto observed = device.create_buffer<T>(2u);
    array<T, 3> initial{T(123), start, T(456)};
    Kernel kernel = [](BufferVar<T> counter, ByteBufferVar raw, BufferVar<T> out, Var<T> delta) {
        Uint i = dispatch_id();
        out.write(i, counter.atomic(0u).fetch_add(delta));
        out.write(count + i, raw.template atomic<T>(0u).fetch_add(delta));
    };
    Kernel observe = [](BufferVar<T> counter, ByteBufferVar raw, BufferVar<T> out) {
        out.write(0u, counter.read(0u));
        out.write(1u, raw.template load_as<T>(0u));
    };
    auto shader = device.compile(kernel, ocarina::format("compute-atomic-contention-{}", type_name));
    auto reader = device.compile(observe, ocarina::format("compute-atomic-contention-read-{}", type_name));
    vector<T> actual(count * 2u);
    array<T, 2> actual_observed{};
    array<T, 3> final_typed{}, final_bytes{};
    auto view = counter.view(1u, 1u);
    auto raw_view = bytes.view(sizeof(T), sizeof(T));
    auto stream = device.create_stream();
    stream << counter.upload(initial.data()) << bytes.upload(initial.data())
           << shader(view, raw_view, returns, increment).dispatch(count)
           << reader(view, raw_view, observed).dispatch(1u)
           << returns.download(actual.data()) << observed.download(actual_observed.data())
           << counter.download(final_typed.data()) << bytes.download(final_bytes.data())
           << synchronize() << commit();
    std::sort(actual.begin(), actual.begin() + count);
    std::sort(actual.begin() + count, actual.end());
    for (uint i = 0; i < count; ++i) {
        T expected = start + T(i) * increment;
        check_value(actual[i], expected, "typed contended old value", i);
        check_value(actual[count + i], expected, "byte contended old value", i);
    }
    initial[1] = start + T(count) * increment;
    check_value(actual_observed[0], initial[1], "typed contended next dispatch", 0u);
    check_value(actual_observed[1], initial[1], "byte contended next dispatch", 0u);
    for (uint i = 0; i < 3u; ++i) {
        check_value(final_typed[i], initial[i], "typed contended sentinels", i);
        check_value(final_bytes[i], initial[i], "byte contended sentinels", i);
    }
    std::cout << "contended add return permutation: " << type_name << std::endl;
}

template<typename T>
void test_contended_cas(Device &device, const char *type_name) {
    constexpr uint count = 257u;
    auto counter = device.create_buffer<T>(1u);
    auto returns = device.create_buffer<T>(count);
    Kernel kernel = [](BufferVar<T> counter, BufferVar<T> out, Var<T> compare) {
        Uint i = dispatch_id();
        out.write(i, atomic_CAS(counter.at(0u), compare, cast<T>(i + 1u)));
    };
    auto shader = device.compile(kernel, ocarina::format("compute-atomic-cas-contention-{}", type_name));
    T zero = T(0), winner = T(0);
    vector<T> actual(count);
    auto stream = device.create_stream();
    stream << counter.upload(&zero) << shader(counter, returns, T(0)).dispatch(count)
           << counter.download(&winner) << returns.download(actual.data()) << synchronize() << commit();
    if (winner < T(1) || winner > T(count)) {
        std::cerr << "CAS produced an invalid winner: " << winner << std::endl;
        std::exit(EXIT_FAILURE);
    }
    for (uint i = 0; i < count; ++i)
        check_value(actual[i], T(i + 1u) == winner ? T(0) : winner, "contended CAS old value", i);
    std::cout << "contended CAS unique winner: " << type_name << std::endl;
}

void test_bindless_byte_atomics(Device &device) {
    constexpr uint count = 19u;
    constexpr uint view_prefix = 3u;
    constexpr uint element_prefix = 2u;
    constexpr uint suffix = 4u;
    constexpr uint total = view_prefix + element_prefix + count + suffix;
    constexpr uint sentinel = 0x13579bdfu;
    constexpr uint dummy_sentinel = 0x2468ace0u;
    auto dummy = device.create_byte_buffer(total * sizeof(uint), "atomic-bindless-dummy");
    auto storage = device.create_byte_buffer(total * sizeof(uint), "atomic-bindless-storage");
    auto returns = device.create_buffer<uint>(count * 3u, "atomic-bindless-returns");
    auto observed = device.create_buffer<uint>(count, "atomic-bindless-observed");
    auto bindless = device.create_bindless_array();
    auto dummy_slot = bindless.emplace(dummy);
    auto slot = bindless.emplace(storage.view(view_prefix * sizeof(uint),
                                             (element_prefix + count) * sizeof(uint)));
    check_value(uint(dummy_slot), 0u, "bindless dummy slot", 0u);
    check_value(uint(slot), 1u, "bindless atomic nonzero slot", 0u);
    vector<uint> initial(total, sentinel), dummy_initial(total, dummy_sentinel);
    for (uint i = 0; i < count; ++i) initial[view_prefix + element_prefix + i] = 100u + 7u * i;

    Kernel kernel = [&bindless](BufferVar<uint> out, Uint selected_slot) {
        Uint i = dispatch_id();
        Uint offset = (i + element_prefix) * uint(sizeof(uint));
        Uint compare = 100u + 7u * i;
        Uint replacement = 1000u + i;
        auto raw = bindless.byte_buffer_var(selected_slot);
        out.write(i * 3u, atomic_CAS(raw.load_as<uint>(offset), compare, replacement));
        out.write(i * 3u + 1u, atomic_CAS(raw.load_as<uint>(offset), compare, replacement + 99u));
        out.write(i * 3u + 2u, atomic_add(raw.load_as<uint>(offset), 5u));
    };
    Kernel observe = [&bindless](BufferVar<uint> out, Uint selected_slot) {
        Uint i = dispatch_id();
        Uint offset = (i + element_prefix) * uint(sizeof(uint));
        auto raw = bindless.byte_buffer_var(selected_slot);
        out.write(i, raw.load_as<uint>(offset));
    };
    auto shader = device.compile(kernel, "compute-atomic-bindless-byte");
    auto reader = device.compile(observe, "compute-atomic-bindless-byte-observe");
    vector<uint> actual_returns(count * 3u), actual_observed(count), final_storage(total), final_dummy(total);
    auto stream = device.create_stream();
    stream << dummy.upload(dummy_initial.data()) << storage.upload(initial.data())
           << bindless.upload_handles()
           << shader(returns, uint(slot)).dispatch(count)
           << reader(observed, uint(slot)).dispatch(count)
           << returns.download(actual_returns.data()) << observed.download(actual_observed.data())
           << storage.download(final_storage.data()) << dummy.download(final_dummy.data())
           << synchronize() << commit();
    for (uint i = 0; i < count; ++i) {
        uint replacement = 1000u + i;
        check_value(actual_returns[i * 3u], 100u + 7u * i, "bindless CAS success old value", i);
        check_value(actual_returns[i * 3u + 1u], replacement, "bindless CAS failure old value", i);
        check_value(actual_returns[i * 3u + 2u], replacement, "bindless add old value", i);
        check_value(actual_observed[i], replacement + 5u, "bindless next dispatch", i);
        initial[view_prefix + element_prefix + i] = replacement + 5u;
    }
    // The dummy slot plus both padding regions distinguish slot selection,
    // descriptor view offset, and byte offset within the selected view.
    for (uint i = 0; i < total; ++i) {
        check_value(final_storage[i], initial[i], "bindless view contents and sentinels", i);
        check_value(final_dummy[i], dummy_initial[i], "bindless unselected slot", i);
    }
    std::cout << "bindless byte atomics / offsets / next dispatch passed" << std::endl;
}

// A 64-bit literal must remain 64-bit before atomicSub negates it, including
// on Windows where unsigned long is only 32 bits.
void test_wide_literal_diagnostic(Device &device) {
    auto counter = device.create_buffer<ulong>(1u);
    auto bytes = device.create_byte_buffer(sizeof(ulong));
    auto returns = device.create_buffer<ulong>(2u);
    Kernel kernel = [](BufferVar<ulong> counter, ByteBufferVar raw, BufferVar<ulong> out) {
        out.write(0u, atomic_sub(counter.at(0u), ulong(2)));
        out.write(1u, atomic_sub(raw.load_as<ulong>(0u), ulong(2)));
    };
    auto shader = device.compile(kernel, "compute-atomic-wide-literal-diagnostic");
    ulong initial = 1004u, final_typed = 0u, final_bytes = 0u;
    array<ulong, 2> old_values{};
    auto stream = device.create_stream();
    stream << counter.upload(&initial) << bytes.upload(&initial)
           << shader(counter, bytes, returns).dispatch(1u)
           << returns.download(old_values.data())
           << counter.download(&final_typed) << bytes.download(&final_bytes)
           << synchronize() << commit();
    check_value(old_values[0], initial, "wide literal typed old value", 0u);
    check_value(old_values[1], initial, "wide literal byte old value", 0u);
    check_value(final_typed, initial - ulong(2), "wide literal typed subtraction", 0u);
    check_value(final_bytes, initial - ulong(2), "wide literal byte subtraction", 0u);
    std::cout << "wide literal subtraction diagnostic passed" << std::endl;
}

}// namespace

int main() {
    auto device = create_test_device();
    const char *wide_literal = std::getenv("OCARINA_TEST_ATOMIC_WIDE_LITERAL");
    if (wide_literal && std::strcmp(wide_literal, "1") == 0) {
        test_wide_literal_diagnostic(device);
        return 0;
    }
    test_atomic_views<int>(device, "int");
    test_atomic_views<uint>(device, "uint");
    test_atomic_views<ulong>(device, "ulong");
    test_atomic_views<float>(device, "float");
    test_contended_add<uint>(device, "uint");
    test_contended_add<ulong>(device, "ulong");
    test_contended_add<float>(device, "float");
    test_contended_cas<uint>(device, "uint");
    test_contended_cas<ulong>(device, "ulong");
    test_bindless_byte_atomics(device);
    test_wide_literal_diagnostic(device);
    std::cout << "compute atomic parity checks passed" << std::endl;
}
