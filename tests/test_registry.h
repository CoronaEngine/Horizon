#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Corona::Horizon::Tests
{
    enum class TestStatus
    {
        Passed,
        Skipped
    };

    struct TestResult
    {
        TestStatus status { TestStatus::Passed };
        std::string message;

        [[nodiscard]] static TestResult pass()
        {
            return {};
        }

        [[nodiscard]] static TestResult skip(std::string reason)
        {
            return { TestStatus::Skipped, std::move(reason) };
        }
    };

    using TestFunction = TestResult (*)();

    struct TestCase
    {
        std::string_view name;
        std::string_view description;
        TestFunction run;
    };

    [[nodiscard]] std::vector<TestCase> hardware_context_tests();
    [[nodiscard]] std::vector<TestCase> execution_system_tests();
}
