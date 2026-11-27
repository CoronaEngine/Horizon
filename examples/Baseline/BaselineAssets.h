#pragma once

#include "Horizon.h"

struct BaselineAssets {
    HardwareBuffer vertices, indices;
    HardwareImage texture;
};

BaselineAssets loadBaselineAssets(HardwareExecutor& executor, HardwareBuffer::Usage vertexUsage,
                                  HardwareBuffer::Usage indexUsage);
ktm::fmat4x4 baselineProjection();
