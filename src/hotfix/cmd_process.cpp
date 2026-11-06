//
// Created by Zero on 2024/8/24.
//

#include "cmd_process.h"
#include "core/util/logging.h"

#include <iostream>
#include <string>

namespace vision::inline hotfix {
namespace {

constexpr std::string_view success_marker = "_HORIZON_HOTFIX_RESULT_=0";
constexpr std::string_view failure_marker = "_HORIZON_HOTFIX_RESULT_=1";

void close_handle(HANDLE &handle) noexcept {
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

}// namespace

bool CmdProcess::initialise() noexcept {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE child_input{};
    HANDLE child_output{};
    // Leave room for a cmd.exe command line and its status query so the write
    // can finish before we start draining the compiler output pipe.
    bool ready = CreatePipe(&child_input, &input_write_, &security, 16 * 1024) &&
                 CreatePipe(&output_read_, &child_output, &security, 0) &&
                 SetHandleInformation(input_write_, HANDLE_FLAG_INHERIT, 0) &&
                 SetHandleInformation(output_read_, HANDLE_FLAG_INHERIT, 0);
    if (ready) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = child_input;
        startup.hStdOutput = child_output;
        startup.hStdError = child_output;
        PROCESS_INFORMATION process{};
        wchar_t command[] = L"cmd.exe /d /q /e:on /k @PROMPT $";
        ready = CreateProcessW(nullptr, command, nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        if (ready) {
            process_ = process.hProcess;
            CloseHandle(process.hThread);
        }
    }
    close_handle(child_input);
    close_handle(child_output);
    if (!ready) {
        OC_WARNING("Failed to start the hotfix command shell");
        cleanup_process();
    }
    return ready;
}

bool CmdProcess::run(std::string_view command) {
    if (!process_ && !initialise()) {
        return false;
    }

    // Evaluate ERRORLEVEL on the next input line, after the command exits.
    // IF ERRORLEVEL compares with >=, so the last branch catches negative exits.
    // The blank line separates the marker from any unterminated tool output.
    std::string input{command};
    input += "\r\nif errorlevel 1 (echo. & echo _HORIZON_HOTFIX_RESULT_=1) "
             "else if errorlevel 0 (echo. & echo _HORIZON_HOTFIX_RESULT_=0) "
             "else (echo. & echo _HORIZON_HOTFIX_RESULT_=1)\r\n";
    DWORD written{};
    if (!WriteFile(input_write_, input.data(), static_cast<DWORD>(input.size()), &written, nullptr) ||
        written != input.size()) {
        OC_WARNING("Failed to write a hotfix command");
        cleanup_process();
        return false;
    }

    char buffer[4096];
    std::string pending;
    DWORD received{};
    while (ReadFile(output_read_, buffer, sizeof(buffer), &received, nullptr) && received != 0) {
        pending.append(buffer, received);
        size_t start = 0;
        for (auto end = pending.find('\n', start); end != std::string::npos;
             end = pending.find('\n', start)) {
            auto line = std::string_view{pending}.substr(start, end - start);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            if (line == success_marker || line == failure_marker) {
                std::cout.flush();
                return line == success_marker;
            }
            std::cout << line << '\n';
            start = end + 1;
        }
        pending.erase(0, start);
    }
    OC_WARNING("Hotfix command shell closed before reporting its result");
    cleanup_process();
    return false;
}

void CmdProcess::cleanup_process() noexcept {
    if (process_) {
        TerminateProcess(process_, 0);
    }
    close_handle(process_);
    close_handle(input_write_);
    close_handle(output_read_);
}

CmdProcess::~CmdProcess() {
    cleanup_process();
}

}// namespace vision::inline hotfix
