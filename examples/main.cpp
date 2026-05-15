#include "example_env/example_env.h"
#include "example_default/example_default.h"
#include "example_baseline/example_baseline.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        //run_example_env();
        //run_example_default();
        run_example_baseline_tutorial();
        //run_example_baseline_glsl();
        //run_example_baseline_edsl();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}
