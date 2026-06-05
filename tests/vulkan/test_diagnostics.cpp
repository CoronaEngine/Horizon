#include "hardware_wrapper/diagnostics.h"
#include "test_registry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr const char* diagnostics_env = "HORIZON_VULKAN_DIAGNOSTICS_PATH";

    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void set_env(const std::string& value)
    {
#if defined(_WIN32) || defined(_WIN64)
        _putenv_s(diagnostics_env, value.c_str());
#else
        setenv(diagnostics_env, value.c_str(), 1);
#endif
    }

    void clear_env()
    {
#if defined(_WIN32) || defined(_WIN64)
        _putenv_s(diagnostics_env, "");
#else
        unsetenv(diagnostics_env);
#endif
    }

    struct ScopedEnv
    {
        explicit ScopedEnv(std::string value)
        {
            if (const char* existing = std::getenv(diagnostics_env))
            {
                previous = existing;
                had_previous = true;
            }

            set_env(value);
        }

        ~ScopedEnv()
        {
            if (had_previous)
            {
                set_env(previous);
            }
            else
            {
                clear_env();
            }

            Corona::Horizon::Diagnostics::reset_for_tests();
        }

        bool had_previous { false };
        std::string previous;
    };

    [[nodiscard]] std::filesystem::path temp_file(const char* name)
    {
        return std::filesystem::temp_directory_path() / name;
    }

    [[nodiscard]] std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult require_diagnostics()
    {
        if (!Corona::Horizon::Diagnostics::enabled())
        {
            return Corona::Horizon::Tests::TestResult::skip("Debug diagnostics are compiled only with HORIZON_ENABLE_VALIDATION and CABBAGE_ENGINE_DEBUG.");
        }

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_env_path_and_overwrite()
    {
        const auto required = require_diagnostics();
        if (required.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return required;
        }

        const std::filesystem::path path = temp_file("horizon-diagnostics-overwrite-test.txt");
        std::filesystem::remove(path);
        ScopedEnv env(path.string());
        Corona::Horizon::Diagnostics::reset_for_tests();

        Corona::Horizon::Diagnostics::write(Corona::Horizon::Diagnostics::Level::Info, "TEST", "first message");
        Corona::Horizon::Diagnostics::write(Corona::Horizon::Diagnostics::Level::Warning, "TEST", "second message");
        std::string contents = read_file(path);
        expect(contents.find("Horizon Vulkan Diagnostics") != std::string::npos, "Diagnostics file should include the header.");
        expect(contents.find("first message") != std::string::npos, "Diagnostics file should contain the first write.");
        expect(contents.find("second message") != std::string::npos, "Diagnostics file should contain the second write.");

        Corona::Horizon::Diagnostics::reset_for_tests();
        Corona::Horizon::Diagnostics::write(Corona::Horizon::Diagnostics::Level::Error, "TEST", "after reset");
        contents = read_file(path);
        expect(contents.find("after reset") != std::string::npos, "Diagnostics reset should allow a new write.");
        expect(contents.find("first message") == std::string::npos, "Diagnostics reset should overwrite the previous file.");

        std::filesystem::remove(path);
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_concurrent_writes()
    {
        const auto required = require_diagnostics();
        if (required.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return required;
        }

        const std::filesystem::path path = temp_file("horizon-diagnostics-concurrent-test.txt");
        std::filesystem::remove(path);
        ScopedEnv env(path.string());
        Corona::Horizon::Diagnostics::reset_for_tests();

        constexpr int thread_count = 8;
        constexpr int write_count = 20;
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (int thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            threads.emplace_back([thread_index] {
                for (int write_index = 0; write_index < write_count; ++write_index)
                {
                    Corona::Horizon::Diagnostics::write(Corona::Horizon::Diagnostics::Level::Info,
                                                        "THREAD",
                                                        "thread " + std::to_string(thread_index) + " write " + std::to_string(write_index));
                }
            });
        }

        for (std::thread& thread : threads)
        {
            thread.join();
        }

        const std::string contents = read_file(path);
        for (int thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            const std::string expected = "thread " + std::to_string(thread_index) + " write " + std::to_string(write_count - 1);
            expect(contents.find(expected) != std::string::npos, "Diagnostics file should contain writes from every thread.");
        }

        std::filesystem::remove(path);
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_unwritable_path_does_not_throw()
    {
        const auto required = require_diagnostics();
        if (required.status == Corona::Horizon::Tests::TestStatus::Skipped)
        {
            return required;
        }

        ScopedEnv env(std::filesystem::temp_directory_path().string());
        Corona::Horizon::Diagnostics::reset_for_tests();
        Corona::Horizon::Diagnostics::write(Corona::Horizon::Diagnostics::Level::Info, "TEST", "this should be ignored");
        expect(Corona::Horizon::Diagnostics::file_path().empty(), "Unwritable diagnostics paths should disable the sink without throwing.");
        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> diagnostics_tests()
    {
        return {
            {
                "diagnostics.env_path_and_overwrite",
                "Debug diagnostics use HORIZON_VULKAN_DIAGNOSTICS_PATH and overwrite on reset.",
                test_env_path_and_overwrite,
            },
            {
                "diagnostics.concurrent_writes",
                "Debug diagnostics serialize concurrent writes into one text report.",
                test_concurrent_writes,
            },
            {
                "diagnostics.unwritable_path",
                "Debug diagnostics disable themselves without throwing when the report path cannot be opened.",
                test_unwritable_path_does_not_throw,
            },
        };
    }
}
