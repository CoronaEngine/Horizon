#pragma once

#include <typeinfo>

namespace ocarina {

class RTTI {
public:
    [[nodiscard]] virtual const char *class_name() const noexcept {
        return typeid(*this).name();
    }
};

}// namespace ocarina
