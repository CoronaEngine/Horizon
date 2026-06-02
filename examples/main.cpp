
#include "example_baseline/example_baseline.h"
#include "example_default/example_default.h"
#include "example_glsl/example_glsl.h"
#include "example_edsl/example_edsl.h"
#include "example_include/example_include.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        // 每一个方法都代表一个独立的示例，展示了不同的功能或者后端实现。可以根据需要选择运行其中一个或者多个示例。

        //run_example_default();
        //run_example_baseline();
        //run_example_glsl();
        run_example_edsl();
        //run_example_include();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    return 0;
}
