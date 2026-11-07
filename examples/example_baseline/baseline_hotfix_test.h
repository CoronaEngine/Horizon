#pragma once

#include "hotfix/hotfix.h"

#include <cstdint>

namespace horizon::example_baseline {

class BaselineHotfixTest : public vision::RuntimeObject
{
public:
    void restore(vision::RuntimeObject* oldObject) noexcept override;
    virtual void print() const noexcept;
#if defined(HORIZON_BASELINE_HOTFIX_WINDOW_TEST)
    virtual const char* live_label() const noexcept;
#endif

private:
    std::uint32_t reloadCount = 0;
};

using BaselineHotfixTestPtr = vision::SP<BaselineHotfixTest>;

class BaselineHotfixSlot final : public vision::HotfixSlot<BaselineHotfixTestPtr>
{
public:
    using Super = vision::HotfixSlot<BaselineHotfixTestPtr>;
    using Super::Super;

    void update_runtime_object(const vision::IObjectConstructor* constructor) noexcept override;
};

} // namespace horizon::example_baseline
