#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void write_utf8(std::wstring_view value) {
    const auto size = static_cast<int>(value.size());
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size,
                                           nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size,
                            utf8.data(), bytes, nullptr, nullptr);
    }
    std::cout << utf8;
}

void write_environment_value(const wchar_t *name) {
    wchar_t value[128]{};
    const auto size = GetEnvironmentVariableW(name, value, 128);
    if (size < 128) {
        write_utf8(std::wstring_view(value, size));
    }
    std::cout.put('\n');
}

int write_environment() {
    write_environment_value(L"HORIZON_PROCESS_TEST_INHERITED");
    write_environment_value(L"Horizon_Process_Test_Override");

    auto *environment = GetEnvironmentStringsW();
    if (!environment) {
        return 1;
    }
    constexpr std::wstring_view name = L"Horizon_Process_Test_Override=";
    int matches = 0;
    for (const auto *entry = environment; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
        if (_wcsnicmp(entry, name.data(), name.size()) == 0) {
            ++matches;
        }
    }
    FreeEnvironmentStringsW(environment);
    std::cout << matches;
    return 0;
}

void write_streams() {
    std::cout.write("stdout\0\r\n", 9);
    std::cerr.write("stderr\0\r\n", 9);
    const std::string out(64 * 1024, 'o');
    const std::string err(64 * 1024, 'e');
    // Each stream fills a pipe while the child still has more data for the other.
    for (int chunk = 0; chunk < 8; ++chunk) {
        std::cout << out;
        std::cerr << err;
    }
}

int launch_descendant(const wchar_t *executable, const wchar_t *ready_name, const wchar_t *release_name) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    auto command = L"\"" + std::wstring(executable) + L"\" hold-pipes " + ready_name + L" " + release_name;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &child)) {
        return 1;
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);

    std::cout << "direct child stdout\n";
    std::cerr << "direct child stderr\n";
    return std::cout && std::cerr ? 0 : 1;
}

int hold_output_pipes(const wchar_t *ready_name, const wchar_t *release_name) {
    DWORD flags{};
    if (!GetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), &flags) ||
        !GetHandleInformation(GetStdHandle(STD_ERROR_HANDLE), &flags)) {
        return 1;
    }
    const HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name);
    const HANDLE release = OpenEventW(SYNCHRONIZE, FALSE, release_name);
    const bool signaled = ready && release && SetEvent(ready);
    const auto status = signaled ? WaitForSingleObject(release, 10000) : WAIT_FAILED;
    if (release) {
        CloseHandle(release);
    }
    if (ready) {
        CloseHandle(ready);
    }
    return status == WAIT_OBJECT_0 ? 0 : 1;
}

}// namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2 || _setmode(_fileno(stdout), _O_BINARY) == -1 ||
        _setmode(_fileno(stderr), _O_BINARY) == -1) {
        return 1;
    }

    const std::wstring_view mode = argv[1];
    if (mode == L"arguments") {
        for (int index = 2; index < argc; ++index) {
            write_utf8(argv[index]);
            std::cout.put('\0');
        }
    } else if (mode == L"working-directory") {
        write_utf8(std::filesystem::current_path().native());
    } else if (mode == L"environment") {
        return write_environment();
    } else if (mode == L"streams") {
        write_streams();
    } else if (mode == L"launch-descendant" && argc == 4) {
        return launch_descendant(argv[0], argv[2], argv[3]);
    } else if (mode == L"hold-pipes" && argc == 4) {
        return hold_output_pipes(argv[2], argv[3]);
    } else if (mode == L"exit-259") {
        return 259;
    } else {
        return 1;
    }
    return std::cout && std::cerr ? 0 : 1;
}
