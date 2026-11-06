#pragma once

#include "hotfix/object.h"

namespace horizon::hotfix_test {

class FixtureObject : public vision::RuntimeObject {
public:
    [[nodiscard]] virtual int generation() const noexcept = 0;

    void restore(vision::RuntimeObject *previous) noexcept override {
        // HotfixSlot matches the registered concrete type before restoring it.
        const auto *object = static_cast<const FixtureObject *>(previous);
        state = object->state;
        restores = object->restores + 1;
    }

    int state{};
    int restores{};
};

class FixtureImplementation final : public FixtureObject {
public:
    [[nodiscard]] int generation() const noexcept override;
};

}// namespace horizon::hotfix_test
