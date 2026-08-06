#include "baseline_hotfix_test.h"

#include <iostream>

namespace horizon::example_baseline {



void BaselineHotfixTest::restore(vision::RuntimeObject* oldObject) noexcept
{
    const auto* oldTest = dynamic_cast<const BaselineHotfixTest*>(oldObject);
    if (oldTest == nullptr)
    {
        std::cout << "[baseline hotfix] state restore skipped: incompatible object" << std::endl;
        return;
    }

    reloadCount = oldTest->reloadCount + 1;
}

void BaselineHotfixTest::print() const noexcept
{
    std::cout << "[baseline hotfix] implementation=v1 reload_count=" << reloadCount << std::endl;
}

void BaselineHotfixSlot::update_runtime_object(const vision::IObjectConstructor* constructor) noexcept
{
    const bool matchesCurrentObject = get() != nullptr && constructor->match(get());
    Super::update_runtime_object(constructor);

    if (matchesCurrentObject)
    {
        std::cout << "[baseline hotfix] reload applied" << std::endl;
        impl()->print();
    }
}

} // namespace horizon::example_baseline

VS_REGISTER_CURRENT_PATH(0, "examples\\HorizonExamples.exe")
VS_REGISTER_HOTFIX(horizon::example_baseline, BaselineHotfixTest)
