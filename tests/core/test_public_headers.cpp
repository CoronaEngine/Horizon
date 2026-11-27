#include "horizon/core/stack_trace.h"

#include <iostream>

int main()
{
    if (horizon::core::capture_stack_trace_light(0, 4).empty())
    {
        std::cerr << "Stack trace diagnostics returned no result\n";
        return 1;
    }
    return 0;
}
