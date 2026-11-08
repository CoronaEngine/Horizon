#include "legacy_example_launcher.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace {
std::filesystem::path find_legacy_executable(const char* launcher)
{
    if (const char* override_path = std::getenv("HORIZON_LEGACY_EXAMPLES_EXECUTABLE");
        override_path && *override_path)
        return std::filesystem::absolute(override_path);

    std::filesystem::path launcher_path = std::filesystem::absolute(launcher);
#ifdef _WIN32
    std::wstring module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length > 0 && length < module_path.size()) {
        module_path.resize(length);
        launcher_path = module_path;
    }
    constexpr auto legacy_name = "HorizonExamplesOld.exe";
#else
    constexpr auto legacy_name = "HorizonExamplesOld";
#endif
    const auto sibling = launcher_path.parent_path() / legacy_name;
    if (std::filesystem::is_regular_file(sibling)) return sibling;
    return std::filesystem::path(HORIZON_LEGACY_EXAMPLES_EXECUTABLE);
}

#ifdef _WIN32
struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit Handle(HANDLE handle) : value(handle) {}
};

void check_win32(BOOL result, const char* operation)
{
    if (!result) throw std::system_error(GetLastError(), std::system_category(), operation);
}

// Quote each argument for the Windows C runtime, including trailing backslashes.
std::wstring quote_argument(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++backslashes; continue; }
        result.append(backslashes * (c == L'"' ? 2 : 1), L'\\');
        backslashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(backslashes * 2, L'\\');
    return result + L'"';
}

int launch_legacy(const std::filesystem::path& executable, int argc, char** argv)
{
    // A temperature watchdog may terminate the launcher. Keep the child in a
    // kill-on-close job so that it cannot continue rendering after that happens.
    Handle job(CreateJobObjectW(nullptr, nullptr));
    check_win32(job.value != nullptr, "CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check_win32(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
                "SetInformationJobObject");

    std::wstring command = quote_argument(executable.wstring());
    for (int i = 1; i < argc; ++i)
        command += L' ' + quote_argument(std::filesystem::path(argv[i]).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    check_win32(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                              CREATE_SUSPENDED, nullptr, nullptr, &startup, &child), "CreateProcessW");
    Handle process(child.hProcess), thread(child.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
        const DWORD error = GetLastError();
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, INFINITE);
        throw std::system_error(error, std::system_category(), "AssignProcessToJobObject");
    }
    check_win32(ResumeThread(thread.value) != static_cast<DWORD>(-1), "ResumeThread");
    check_win32(WaitForSingleObject(process.value, INFINITE) == WAIT_OBJECT_0, "WaitForSingleObject");
    DWORD exit_code{};
    check_win32(GetExitCodeProcess(process.value, &exit_code), "GetExitCodeProcess");
    return static_cast<int>(exit_code);
}
#else
int launch_legacy(const std::filesystem::path& executable, int argc, char** argv)
{
    std::string path = executable.string();
    std::vector<char*> arguments{path.data()};
    for (int i = 1; i < argc; ++i) arguments.push_back(argv[i]);
    arguments.push_back(nullptr);
    // Replacing this process preserves the PID used by external watchdogs.
    execv(path.c_str(), arguments.data());
    throw std::system_error(errno, std::generic_category(), "execv");
}
#endif
}

int run_legacy_example(int argc, char** argv)
{
    try {
        const auto executable = find_legacy_executable(argv[0]);
        if (!std::filesystem::is_regular_file(executable)) {
            std::cerr << "Legacy example executable not found: " << executable << '\n'
                      << "Build HorizonExamplesOld with HORIZON_USE_LEGACY_BACKEND=ON, or set\n"
                         "HORIZON_LEGACY_EXAMPLES_EXECUTABLE to an existing executable.\n";
            return 1;
        }
        return launch_legacy(executable, argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Cannot launch Legacy example: " << error.what() << '\n';
        return 1;
    }
}
