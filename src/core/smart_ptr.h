#pragma once

#include <memory>

namespace ocarina {

template<typename... Ts>
using UP = std::unique_ptr<Ts...>;

template<typename T>
using SP = std::shared_ptr<T>;

}// namespace ocarina
