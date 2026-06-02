#pragma once

#include <cstdint>

enum class ExampleDefaultThreadMode
{
    SingleThreaded,
    MeshRenderDisplay
};

enum class ExampleDefaultMode
{
    Default,
    Glsl,
    Edsl,
    Texture,
    Compute,
    MultiWindow
};

void run_example_default(uint32_t frame_count = 180,
                         ExampleDefaultThreadMode thread_mode = ExampleDefaultThreadMode::SingleThreaded,
                         ExampleDefaultMode mode = ExampleDefaultMode::Default);
