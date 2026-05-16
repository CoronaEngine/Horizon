#include "example_env/example_env.h"
#include "example_default/example_default.h"
#include "example_baseline/example_baseline.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        // 每一个方法是一个独立的示例，需要注释掉不需要的示例，目前只能同时运行一个示例，后续会改进为可以同时运行多个示例

        run_example_env();
        //run_example_default();
        //run_example_baseline_tutorial();
        //run_example_baseline_glsl();
        //run_example_baseline_edsl();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}
