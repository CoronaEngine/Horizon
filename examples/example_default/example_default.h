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
    MultiWindow,
    Stress
};

struct ExampleDefaultStressConfig
{
    uint32_t window_count { 8 };
    uint32_t render_thread_count { 4 };
};

void run_example_default(uint32_t frame_count = 180,
                         ExampleDefaultThreadMode thread_mode = ExampleDefaultThreadMode::SingleThreaded,
                         ExampleDefaultMode mode = ExampleDefaultMode::Default,
                         ExampleDefaultStressConfig stress_config = {});
