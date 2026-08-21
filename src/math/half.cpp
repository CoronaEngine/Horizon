//
// Created by Z on 2026/4/14.
//

#include "half.h"
#include "real.h"

namespace horizon::math {
using namespace horizon::core;

half::operator real() const { return static_cast<real>(half_to_float(bits())); }

}// namespace horizon::math