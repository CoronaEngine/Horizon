#pragma once

#include "HardwareWrapperVulkan/DisplayVulkan/DisplayManager.h"

// Remaining shared control blocks; reference counts are owner-thread only.
struct DisplayerHardwareWrap
{
    void *displaySurface = nullptr;
    std::shared_ptr<DisplayManager> displayManager;
    uint64_t refCount = 1;
};

struct PushConstantWrap
{
    uint8_t *data{nullptr};
    uint64_t size{0};
    uint64_t refCount{1};
    bool isSub{false};
};
