#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ocarina {

struct VkTraceDispatchLimits {
    std::array<uint64_t, 3> max_dimensions;
    uint32_t max_invocations;

    [[nodiscard]] constexpr std::optional<size_t> exceeded_axis(std::array<uint32_t, 3> dim) const noexcept {
        for (size_t axis = 0; axis < dim.size(); ++axis)
            if (dim[axis] > max_dimensions[axis]) return axis;
        return {};
    }

    [[nodiscard]] constexpr bool exceeds_invocation_count(std::array<uint32_t, 3> dim) const noexcept {
        // The first two factors fit in 64 bits. Divide before considering the third.
        return dim[2] != 0 && uint64_t(dim[0]) * dim[1] > max_invocations / dim[2];
    }
};

}// namespace ocarina
