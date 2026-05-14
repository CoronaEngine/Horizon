#include "example_env/example_env.hpp"
#include "example_default/default_test.hpp"
#include "example_baseline/example_baseline.hpp"

int main()
{
    try
    {
        //run_example_default();
        run_example_env();
        //run_example_baseline();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        
    }

    return 0;
}
