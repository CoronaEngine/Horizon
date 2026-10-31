//
// Created by Zero on 10/08/2022.
//

#pragma once

#include "util.h"

namespace ocarina {

class CUDADevice;

class Function;

class CUDACompiler {
private:
    CUDADevice *device_;

public:
    explicit CUDACompiler(CUDADevice *device);
    [[nodiscard]] ocarina::string compile(const Function &function, int sm) const noexcept;
};

}// namespace ocarina
