//
// Created by Zero on 2024/8/17.
//

#pragma once

#include "core/stl.h"
#include "hotfix/object.h"
#include "hotfix/hotfix.h"

#define HOTFIX_FLAG

namespace vision::inline hotfix {

struct Test : RuntimeObject {
public:
    int attr_int{};
    float attr_float{};
#ifdef HOTFIX_FLAG
    double attr_double{};
#endif
public:
    virtual void clear() noexcept;
    [[nodiscard]] string get_string() const;
    void restore(vision::RuntimeObject *old_obj) noexcept override;
    virtual void fill() noexcept;
    virtual void print() const noexcept;
};

}// namespace vision::inline hotfix
