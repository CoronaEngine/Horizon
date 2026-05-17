
#include "example_default/example_default.h"
#include "example_baseline/example_baseline.h"
#include "example_glsl/example_glsl.h"
#include "example_edsl/example_edsl.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        run_example_default();
        //run_example_baseline();
        //run_example_glsl();
        //run_example_edsl();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}
