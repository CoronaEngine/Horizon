//
// Created by Z on 2026/4/14.
//

#include "half.h"
#include "real.h"

namespace ocarina {

half::operator real() const { return static_cast<real>(half_to_float(bits())); }

}// namespace ocarina