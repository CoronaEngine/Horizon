#include <algorithm>
#include <cstdlib>
#include <iostream>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

void log_value_mismatch(const char *label, uint index, int actual, int expected) {
    std::cerr << "  FAIL [" << label << "] at index " << index
              << " expected=" << expected
              << " actual=" << actual
              << std::endl;
}

int verify_vector_equals(const char *label, const vector<int> &actual, const vector<int> &expected) {
    int failures = 0;
    if (actual.size() != expected.size()) {
        std::cerr << "  FAIL [" << label << "] size mismatch expected=" << expected.size()
                  << " actual=" << actual.size() << std::endl;
        return 1;
    }
    for (uint index = 0; index < actual.size(); ++index) {
        if (actual[index] != expected[index]) {
            log_value_mismatch(label, index, actual[index], expected[index]);
            ++failures;
        }
    }
    return failures;
}

int verify_permutation(const char *label, const vector<int> &actual, int start_value) {
    vector<int> sorted = actual;
    std::sort(sorted.begin(), sorted.end());
    int failures = 0;
    for (uint index = 0; index < sorted.size(); ++index) {
        int expected = start_value + static_cast<int>(index);
        if (sorted[index] != expected) {
            log_value_mismatch(label, index, sorted[index], expected);
            ++failures;
        }
    }
    return failures;
}

int verify_scalar(const char *label, int actual, int expected) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "  FAIL [" << label << "] expected=" << expected
              << " actual=" << actual
              << std::endl;
    return 1;
}

int test_buffer_fetch_add(Device &device, Stream &stream) {
    std::cout << "=== test_buffer_fetch_add ===" << std::endl;
    constexpr uint count = 128u;
    auto counter = device.create_buffer<int>(1u, "test_atomic_buffer_fetch_add_counter");
    auto old_values = device.create_buffer<int>(count, "test_atomic_buffer_fetch_add_old_values");

    int counter_init = 0;
    int host_counter = -1;
    vector<int> host_old_values(count, -1);
    counter.upload_immediately(&counter_init);

    Kernel kernel = [&](BufferVar<int> dst_counter, BufferVar<int> dst_old_values, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Int old_value = dst_counter.atomic(0u).fetch_add(1);
            dst_old_values.write(index, old_value);
        };
    };

    auto shader = device.compile(kernel, "test_atomic_buffer_fetch_add");
    stream << shader(counter, old_values, count).dispatch(count)
           << counter.download(&host_counter)
           << old_values.download(host_old_values.data())
           << synchronize()
           << commit();

    int failures = 0;
    failures += verify_scalar("buffer_fetch_add_counter", host_counter, static_cast<int>(count));
    failures += verify_permutation("buffer_fetch_add_old_values", host_old_values, 0);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_buffer_fetch_sub(Device &device, Stream &stream) {
    std::cout << "=== test_buffer_fetch_sub ===" << std::endl;
    constexpr uint count = 96u;
    auto counter = device.create_buffer<int>(1u, "test_atomic_buffer_fetch_sub_counter");
    auto old_values = device.create_buffer<int>(count, "test_atomic_buffer_fetch_sub_old_values");

    int counter_init = static_cast<int>(count * 2u);
    int host_counter = -1;
    vector<int> host_old_values(count, -1);
    counter.upload_immediately(&counter_init);

    Kernel kernel = [&](BufferVar<int> dst_counter, BufferVar<int> dst_old_values, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Int old_value = dst_counter.atomic(0u).fetch_sub(1);
            dst_old_values.write(index, old_value);
        };
    };

    auto shader = device.compile(kernel, "test_atomic_buffer_fetch_sub");
    stream << shader(counter, old_values, count).dispatch(count)
           << counter.download(&host_counter)
           << old_values.download(host_old_values.data())
           << synchronize()
           << commit();

    int failures = 0;
    failures += verify_scalar("buffer_fetch_sub_counter", host_counter, static_cast<int>(count));
    failures += verify_permutation("buffer_fetch_sub_old_values", host_old_values, static_cast<int>(count + 1u));
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_buffer_exchange(Device &device, Stream &stream) {
    std::cout << "=== test_buffer_exchange ===" << std::endl;
    constexpr uint count = 64u;
    auto values = device.create_buffer<int>(count, "test_atomic_buffer_exchange_values");
    auto old_values = device.create_buffer<int>(count, "test_atomic_buffer_exchange_old_values");

    vector<int> host_initial(count, 0);
    vector<int> host_values(count, -1);
    vector<int> host_old_values(count, -1);
    vector<int> expected_final(count, 0);
    for (uint index = 0; index < count; ++index) {
        host_initial[index] = static_cast<int>(index * 7u + 3u);
        expected_final[index] = 1000 + static_cast<int>(index);
    }
    values.upload_immediately(host_initial.data());

    Kernel kernel = [&](BufferVar<int> dst_values, BufferVar<int> dst_old_values, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Int new_value = 1000 + cast<int>(index);
            Int old_value = dst_values.atomic(index).exchange(new_value);
            dst_old_values.write(index, old_value);
        };
    };

    auto shader = device.compile(kernel, "test_atomic_buffer_exchange");
    stream << shader(values, old_values, count).dispatch(count)
           << values.download(host_values.data())
           << old_values.download(host_old_values.data())
           << synchronize()
           << commit();

    int failures = 0;
    failures += verify_vector_equals("buffer_exchange_old_values", host_old_values, host_initial);
    failures += verify_vector_equals("buffer_exchange_final_values", host_values, expected_final);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_atomic_cas(Device &device, Stream &stream) {
    std::cout << "=== test_atomic_cas ===" << std::endl;
    constexpr uint count = 64u;
    auto values = device.create_buffer<int>(count, "test_atomic_cas_values");
    auto success_old_values = device.create_buffer<int>(count, "test_atomic_cas_success_old_values");
    auto failed_old_values = device.create_buffer<int>(count, "test_atomic_cas_failed_old_values");

    vector<int> host_initial(count, 0);
    vector<int> host_values(count, -1);
    vector<int> host_success_old_values(count, -1);
    vector<int> host_failed_old_values(count, -1);
    vector<int> expected_final(count, 0);
    vector<int> expected_failed_old_values(count, 0);
    for (uint index = 0; index < count; ++index) {
        host_initial[index] = static_cast<int>(index);
        expected_final[index] = static_cast<int>(index + 100u);
        expected_failed_old_values[index] = expected_final[index];
    }
    values.upload_immediately(host_initial.data());

    Kernel kernel = [&](BufferVar<int> dst_values,
                        BufferVar<int> dst_success_old_values,
                        BufferVar<int> dst_failed_old_values,
                        Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Int compare_value = cast<int>(index);
            Int expected_value = compare_value + 100;
            Int success_old_value = atomic_CAS(dst_values.at(index), compare_value, expected_value);
            Int failed_old_value = atomic_CAS(dst_values.at(index), compare_value, compare_value + 200);
            dst_success_old_values.write(index, success_old_value);
            dst_failed_old_values.write(index, failed_old_value);
        };
    };

    auto shader = device.compile(kernel, "test_atomic_cas");
    stream << shader(values, success_old_values, failed_old_values, count).dispatch(count)
           << values.download(host_values.data())
           << success_old_values.download(host_success_old_values.data())
           << failed_old_values.download(host_failed_old_values.data())
           << synchronize()
           << commit();

    int failures = 0;
    failures += verify_vector_equals("atomic_cas_success_old_values", host_success_old_values, host_initial);
    failures += verify_vector_equals("atomic_cas_failed_old_values", host_failed_old_values, expected_failed_old_values);
    failures += verify_vector_equals("atomic_cas_final_values", host_values, expected_final);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_byte_buffer_fetch_add(Device &device, Stream &stream) {
    std::cout << "=== test_byte_buffer_fetch_add ===" << std::endl;
    constexpr uint count = 128u;
    auto counter = device.create_byte_buffer(sizeof(int), "test_atomic_byte_buffer_fetch_add_counter");
    auto old_values = device.create_buffer<int>(count, "test_atomic_byte_buffer_fetch_add_old_values");

    int counter_init = 0;
    int host_counter = -1;
    vector<int> host_old_values(count, -1);
    counter.upload_immediately(&counter_init);

    Kernel kernel = [&](ByteBufferVar dst_counter, BufferVar<int> dst_old_values, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Int old_value = dst_counter.atomic<int>(0u).fetch_add(1);
            dst_old_values.write(index, old_value);
        };
    };

    auto shader = device.compile(kernel, "test_atomic_byte_buffer_fetch_add");
    stream << shader(counter, old_values, count).dispatch(count)
           << counter.download(&host_counter)
           << old_values.download(host_old_values.data())
           << synchronize()
           << commit();

    int failures = 0;
    failures += verify_scalar("byte_buffer_fetch_add_counter", host_counter, static_cast<int>(count));
    failures += verify_permutation("byte_buffer_fetch_add_old_values", host_old_values, 0);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

}// namespace

int main() {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();

    int total_failures = 0;
    total_failures += test_buffer_fetch_add(device, stream);
    total_failures += test_buffer_fetch_sub(device, stream);
    total_failures += test_buffer_exchange(device, stream);
    total_failures += test_atomic_cas(device, stream);
    total_failures += test_byte_buffer_fetch_add(device, stream);

    if (total_failures != 0) {
        std::cerr << "[test-atomic] failed with " << total_failures << " mismatches" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "[test-atomic] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}