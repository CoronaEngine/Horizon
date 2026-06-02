#pragma once

#include <cstdint>

enum class ExampleDefaultThreadMode
{
    SingleThreaded,
    MeshRenderDisplay
};

void run_example_default(uint32_t frame_count = 180,
                         ExampleDefaultThreadMode thread_mode = ExampleDefaultThreadMode::SingleThreaded);
