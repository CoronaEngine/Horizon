#pragma once

#include "core/header.h"

#include <cstddef>

namespace horizon::test {

using FreeFunction = void (*)(void *);

[[nodiscard]] OC_CORE_API void *allocator_crt_allocate(size_t size, bool use_allocator) noexcept;
[[nodiscard]] OC_CORE_API bool allocator_crt_free(void *pointer, size_t size) noexcept;
[[nodiscard]] OC_CORE_API FreeFunction allocator_crt_free_function() noexcept;

}// namespace horizon::test
