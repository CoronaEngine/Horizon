#include "example_baseline/example_baseline.h"
#include "example_edsl/example_edsl.h"
#include "example_glsl/example_glsl.h"
#include "example_ibl/example_ibl.h"
#include "example_drawstress/example_drawstress.h"
#include "example_edsl_ibl/example_edsl_ibl.h"
#include "example_raymarch/example_raymarch.h"
#include "example_bump/example_bump.h"
#include "example_deferred/example_deferred.h"
#include "example_shadowmaps/example_shadowmaps.h"
#include "example_shadowvolumes/example_shadowvolumes.h"
#include "example_assao/example_assao.h"

#include <iostream>
#include <string_view>

int main(int argc, char **argv)
{
    const std::string_view mode = argc > 1 ? std::string_view(argv[1]) : std::string_view("assao");

    if (mode == "baseline")
    {
        run_example_baseline();
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
    if (mode == "raymarch")
    {
        run_example_raymarch();
        return 0;
    }
    if (mode == "bump")
    {
        run_example_bump();
        return 0;
    }
    if (mode == "deferred")
    {
        run_example_deferred();
        return 0;
    }
    if (mode == "shadowmaps")
    {
        run_example_shadowmaps();
        return 0;
    }
    if (mode == "shadowvolumes")
    {
        run_example_shadowvolumes();
        return 0;
    }
    if (mode == "assao")
    {
        run_example_assao();
        return 0;
    }

    std::cerr << "Usage: HorizonExamples.exe [baseline|default|edsl|glsl|branch_pruning|ibl|drawstress|raymarch|bump|deferred|shadowmaps|shadowvolumes|assao]\n";
    return 0;
}
