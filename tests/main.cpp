#include "test_registry.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    constexpr int skip_return_code = 77;

    void append_tests(std::vector<Corona::Horizon::Tests::TestCase>& tests,
                      std::vector<Corona::Horizon::Tests::TestCase> next_tests)
    {
        tests.insert(tests.end(), next_tests.begin(), next_tests.end());
    }

    [[nodiscard]] std::vector<Corona::Horizon::Tests::TestCase> collect_tests()
    {
        std::vector<Corona::Horizon::Tests::TestCase> tests;
        append_tests(tests, Corona::Horizon::Tests::hardware_context_tests());
        return tests;
    }

    void print_header()
    {
        std::cout << "HorizonTests\n"
                  << "Runs correctness checks for Horizon subsystems. Each test prints its purpose before it runs.\n\n";
        std::cout.flush();
    }

    void print_test_list(const std::vector<Corona::Horizon::Tests::TestCase>& tests)
    {
        print_header();
        std::cout << "Available tests:\n";

        for (const auto& test : tests)
        {
            std::cout << "  " << test.name << "\n"
                      << "    " << test.description << "\n";
        }
    }

    [[nodiscard]] int run_tests(const std::vector<Corona::Horizon::Tests::TestCase>& tests)
    {
        int passed = 0;
        int skipped = 0;
        int failed = 0;

        print_header();

        for (const auto& test : tests)
        {
            std::cout << "[RUN] " << test.name << "\n"
                      << "      " << test.description << "\n";
            std::cout.flush();

            try
            {
                const Corona::Horizon::Tests::TestResult result = test.run();
                if (result.status == Corona::Horizon::Tests::TestStatus::Skipped)
                {
                    ++skipped;
                    std::cout << "[SKIP] " << test.name;
                    if (!result.message.empty())
                    {
                        std::cout << ": " << result.message;
                    }
                    std::cout << "\n\n";
                    continue;
                }

                ++passed;
                std::cout << "[PASS] " << test.name << "\n\n";
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << "[FAIL] " << test.name << ": " << error.what() << "\n\n";
            }
            catch (...)
            {
                ++failed;
                std::cerr << "[FAIL] " << test.name << ": unknown exception\n\n";
            }
        }

        std::cout << "Summary: " << passed << " passed, " << skipped << " skipped, " << failed << " failed.\n";

        if (failed > 0)
        {
            return EXIT_FAILURE;
        }

        if (passed == 0 && skipped > 0)
        {
            return skip_return_code;
        }

        return EXIT_SUCCESS;
    }
}

int main(int argc, char** argv)
{
    const auto tests = collect_tests();

    if (argc > 1)
    {
        const std::string_view command { argv[1] };
        if (command == "--list")
        {
            print_test_list(tests);
            return EXIT_SUCCESS;
        }

        std::cerr << "Unknown argument: " << command << "\n"
                  << "Use --list to show what this test executable covers.\n";
        return EXIT_FAILURE;
    }

    return run_tests(tests);
}
