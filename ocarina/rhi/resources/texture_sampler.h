//
// Created by Zero on 2023/12/29.
//

#pragma once

#include "math/basic_types.h"
#include "core/util/hash.h"

namespace ocarina {

class TextureSampler : Hashable {
public:
    enum struct Filter : uint8_t {
        POINT,
        LINEAR_POINT,
        LINEAR_LINEAR,
        ANISOTROPIC
    };

    enum struct Address : uint8_t {
        EDGE,
        REPEAT,
        MIRROR,
        CLAMP
    };

private:
    Filter filter_{Filter::POINT};
    Address u_address_{Address::EDGE};
    Address v_address_{Address::EDGE};
    Address w_address_{Address::EDGE};

protected:
    [[nodiscard]] uint64_t compute_hash() const noexcept override {
        return hash64(filter_, u_address_, v_address_, w_address_);
    }

public:
    constexpr TextureSampler() noexcept = default;
    constexpr TextureSampler(Filter filter, Address address) noexcept
        : filter_{filter},
          u_address_{address},
          v_address_{address},
          w_address_{address} {}
    constexpr TextureSampler(Filter filter, Address u_address, Address v_address, Address w_address) noexcept
        : filter_{filter},
          u_address_{u_address},
          v_address_{v_address},
          w_address_{w_address} {}
    constexpr TextureSampler(Filter filter, Address u_address, Address v_address) noexcept
        : filter_{filter},
          u_address_{u_address},
          v_address_{v_address},
          w_address_{v_address} {}

    OC_MAKE_MEMBER_GETTER_SETTER(filter,)
    OC_MAKE_MEMBER_GETTER_SETTER(u_address,)
    OC_MAKE_MEMBER_GETTER_SETTER(v_address,)
    OC_MAKE_MEMBER_GETTER_SETTER(w_address,)

    [[nodiscard]] bool operator==(TextureSampler rhs) const noexcept {
        return hash() == rhs.hash();
    }
};

}// namespace ocarina