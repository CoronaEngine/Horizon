#pragma once

#include "hotfix/object.h"

namespace horizon::hotfix_test {

class FixtureObject : public vision::RuntimeObject {
public:
    [[nodiscard]] virtual int generation() const noexcept = 0;

    void restore(vision::RuntimeObject *old_obj) noexcept override {
        VS_HOTFIX_MOVE_ATTRS(state, restores)
        ++restores;
    }

    int state{};
    int restores{};
};

class FixtureImplementation final : public FixtureObject {
public:
    [[nodiscard]] int generation() const noexcept override;
};

}// namespace horizon::hotfix_test
