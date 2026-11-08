#pragma once

#include <cstdint>

// Remaining shared control block; reference counts are owner-thread only.
struct PushConstantWrap
{
    uint8_t *data{nullptr};
    uint64_t size{0};
    uint64_t refCount{1};
    bool isSub{false};
};
