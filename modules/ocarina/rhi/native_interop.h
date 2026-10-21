#pragma once

#include "core/stl.h"

namespace ocarina {

enum class NativeInteropBackend : uint8_t {
    VulkanCompute
};

// Valid only during a native callback. The backend-specific public interop
// header defines the object pointed to by native. No ownership is transferred.
struct NativeInteropContext {
    NativeInteropBackend backend;
    const void *native;
};

using NativeCallbackFunction = std::function<void(const NativeInteropContext &)>;

}// namespace ocarina
