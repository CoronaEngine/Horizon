#include "example_baseline/example_baseline.h"
#include "example_default/example_default.h"
#include "example_edsl/example_edsl.h"
#include "example_glsl/example_glsl.h"
#include "example_branch_pruning/example_branch_pruning.h"
#include "example_ibl/example_ibl.h"
#include "example_drawstress/example_drawstress.h"
#include "example_edsl_ibl/example_edsl_ibl.h"

#include <iostream>
#include <string_view>

int main(int argc, char **argv)
{
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view("default");

    if (mode == "baseline")
    {
        run_example_baseline();
        return 0;
    }
    if (mode == "default")
    {
        run_example_default();
        return 0;
    }
    if (mode == "edsl")
    {
        run_example_edsl();
        return 0;
    }
    if (mode == "glsl")
    {
        run_example_glsl();
        return 0;
    }
    if (mode == "branch_pruning")
    {
        run_example_branch_pruning();
        return 0;
    }

    if (mode == "ibl")
    {
        run_example_ibl();
        return 0;
    }

    if (mode == "edsl_ibl")
    {
        run_example_edsl_ibl();
        return 0;
    }

    if (mode == "drawstress")
    {
        run_example_drawstress();
        return 0;
    }

    std::cerr << "Usage: HorizonExamples.exe [baseline|default|edsl|glsl|branch_pruning|ibl|drawstress]\n";
    return 0;
}
